#include "diskscontrol.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDebug>
#include <QProcess>

// UDisks2 bus name / well-known paths.
static const QString UD2_SERVICE = QStringLiteral("org.freedesktop.UDisks2");
static const QString UD2_ROOT = QStringLiteral("/org/freedesktop/UDisks2");
static const QString IFACE_BLOCK = QStringLiteral("org.freedesktop.UDisks2.Block");
static const QString IFACE_FS = QStringLiteral("org.freedesktop.UDisks2.Filesystem");
static const QString IFACE_DRIVE = QStringLiteral("org.freedesktop.UDisks2.Drive");

// GetManagedObjects returns a{oa{sa{sv}}}: path -> (interface -> (prop -> value)).
using InterfaceMap = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, InterfaceMap>;

// 'ay' bytestring (NUL-terminated device paths etc.) -> QByteArray.
static QByteArray demarshalAy(const QVariant &v)
{
    if (!v.canConvert<QDBusArgument>())
        return v.toByteArray();
    QDBusArgument a = v.value<QDBusArgument>();
    QByteArray out;
    a >> out;
    return out;
}

// 'aay' array of bytestrings (UDisks2 MountPoints) -> first entry as a path.
static QString firstMountPoint(const QVariant &v)
{
    if (!v.canConvert<QDBusArgument>())
        return {};
    QDBusArgument a = v.value<QDBusArgument>();
    a.beginArray();
    QString result;
    while (!a.atEnd()) {
        QByteArray b;
        a >> b;
        if (result.isEmpty() && !b.isEmpty())
            result = QString::fromLocal8Bit(b.constData()); // stop at trailing NUL
    }
    a.endArray();
    return result;
}

DisksControl::DisksControl(QObject *parent)
    : QObject(parent)
{
    m_rescanDebounce.setSingleShot(true);
    m_rescanDebounce.setInterval(300);
    connect(&m_rescanDebounce, &QTimer::timeout, this, &DisksControl::rescan);

    QDBusConnection bus = QDBusConnection::systemBus();
    // Device plug/unplug (adds/removes UDisks2 objects).
    bus.connect(UD2_SERVICE, UD2_ROOT,
                QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                QStringLiteral("InterfacesAdded"), this, SLOT(scheduleRescan()));
    bus.connect(UD2_SERVICE, UD2_ROOT,
                QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                QStringLiteral("InterfacesRemoved"), this, SLOT(scheduleRescan()));
    // Mount/unmount happen via PropertiesChanged on the Filesystem interface
    // (MountPoints), not InterfacesAdded/Removed. Empty path = match any object.
    bus.connect(UD2_SERVICE, QString(),
                QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"), this, SLOT(scheduleRescan()));

    rescan();
}

void DisksControl::scheduleRescan()
{
    m_rescanDebounce.start();
}

void DisksControl::rescan()
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        UD2_SERVICE, UD2_ROOT, QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("GetManagedObjects"));
    QDBusMessage reply = QDBusConnection::systemBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        if (m_available) {
            m_available = false;
            m_volumes.clear();
            emit changed();
        }
        return;
    }

    ManagedObjects objs;
    reply.arguments().first().value<QDBusArgument>() >> objs;

    QVariantList volumes;
    for (auto it = objs.constBegin(); it != objs.constEnd(); ++it) {
        const InterfaceMap &ifaces = it.value();
        if (!ifaces.contains(IFACE_BLOCK) || !ifaces.contains(IFACE_FS))
            continue; // only mountable filesystems

        const QVariantMap block = ifaces.value(IFACE_BLOCK);
        if (block.value(QStringLiteral("HintIgnore")).toBool())
            continue;
        if (block.value(QStringLiteral("IdUsage")).toString() != QLatin1String("filesystem"))
            continue;

        const QDBusObjectPath drivePath =
            block.value(QStringLiteral("Drive")).value<QDBusObjectPath>();
        const QVariantMap drive = objs.value(drivePath).value(IFACE_DRIVE);

        // Only surface external/removable media (like Plasma's device notifier);
        // skip internal system disks.
        const bool removable = drive.value(QStringLiteral("Removable")).toBool()
                               || drive.value(QStringLiteral("MediaRemovable")).toBool()
                               || drive.value(QStringLiteral("ConnectionBus")).toString()
                                      == QLatin1String("usb");
        if (!removable)
            continue;

        const QString mountPoint = firstMountPoint(block.value(QStringLiteral("MountPoints")).isValid()
                                                       ? block.value(QStringLiteral("MountPoints"))
                                                       : ifaces.value(IFACE_FS).value(QStringLiteral("MountPoints")));

        QString label = block.value(QStringLiteral("IdLabel")).toString();
        if (label.isEmpty())
            label = drive.value(QStringLiteral("Model")).toString();
        const QString device = QString::fromLocal8Bit(
            demarshalAy(block.value(QStringLiteral("Device"))).constData());
        if (label.isEmpty())
            label = device.section(QLatin1Char('/'), -1);

        QVariantMap v;
        v[QStringLiteral("path")] = it.key().path();
        v[QStringLiteral("drive")] = drivePath.path();
        v[QStringLiteral("label")] = label;
        v[QStringLiteral("device")] = device;
        v[QStringLiteral("mountPoint")] = mountPoint;
        v[QStringLiteral("mounted")] = !mountPoint.isEmpty();
        v[QStringLiteral("ejectable")] = drive.value(QStringLiteral("Ejectable")).toBool();
        v[QStringLiteral("size")] = block.value(QStringLiteral("Size")).toDouble();
        volumes.append(v);
    }

    m_available = true;
    m_volumes = volumes;
    emit changed();
}

void DisksControl::mount(const QString &path)
{
    if (path.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(UD2_SERVICE, path, IFACE_FS,
                                                       QStringLiteral("Mount"));
    call.setArguments({QVariant::fromValue(QVariantMap())}); // empty a{sv} options
    QDBusConnection::systemBus().call(call, QDBus::NoBlock);
    scheduleRescan();
}

void DisksControl::unmount(const QString &path)
{
    if (path.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(UD2_SERVICE, path, IFACE_FS,
                                                       QStringLiteral("Unmount"));
    call.setArguments({QVariant::fromValue(QVariantMap())});
    QDBusConnection::systemBus().call(call, QDBus::NoBlock);
    scheduleRescan();
}

void DisksControl::eject(const QString &drivePath)
{
    if (drivePath.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(UD2_SERVICE, drivePath, IFACE_DRIVE,
                                                       QStringLiteral("Eject"));
    call.setArguments({QVariant::fromValue(QVariantMap())});
    QDBusConnection::systemBus().call(call, QDBus::NoBlock);
    scheduleRescan();
}

void DisksControl::openMount(const QString &mountPoint)
{
    if (mountPoint.isEmpty())
        return;
    QProcess::startDetached(QStringLiteral("xdg-open"), {mountPoint});
}
