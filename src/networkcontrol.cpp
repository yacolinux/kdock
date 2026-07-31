#include "networkcontrol.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDebug>

static const QString NM_SERVICE = QStringLiteral("org.freedesktop.NetworkManager");
static const QString NM_PATH = QStringLiteral("/org/freedesktop/NetworkManager");
static const QString NM_IFACE = QStringLiteral("org.freedesktop.NetworkManager");
static const QString NM_SETTINGS_PATH = QStringLiteral("/org/freedesktop/NetworkManager/Settings");
static const QString NM_SETTINGS_IFACE = QStringLiteral("org.freedesktop.NetworkManager.Settings");
static const QString NM_CONN_IFACE = QStringLiteral("org.freedesktop.NetworkManager.Settings.Connection");
static const QString NM_ACTIVE_IFACE = QStringLiteral("org.freedesktop.NetworkManager.Connection.Active");
static const QString NM_DEVICE_IFACE = QStringLiteral("org.freedesktop.NetworkManager.Device");
static const QString NM_WIRELESS_IFACE = QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless");
static const QString NM_AP_IFACE = QStringLiteral("org.freedesktop.NetworkManager.AccessPoint");
static const QString PROPS_IFACE = QStringLiteral("org.freedesktop.DBus.Properties");

// GetSettings returns a{sa{sv}} (setting group -> key -> value).
using SettingsMap = QMap<QString, QVariantMap>;

// Properties.GetAll(iface) on an object -> its property map.
static QVariantMap getAll(const QString &path, const QString &iface)
{
    QDBusMessage call = QDBusMessage::createMethodCall(NM_SERVICE, path, PROPS_IFACE,
                                                       QStringLiteral("GetAll"));
    call.setArguments({iface});
    QDBusMessage reply = QDBusConnection::systemBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    QVariantMap map;
    reply.arguments().first().value<QDBusArgument>() >> map;
    return map;
}

static QVariant getProp(const QString &path, const QString &iface, const QString &prop)
{
    QDBusMessage call = QDBusMessage::createMethodCall(NM_SERVICE, path, PROPS_IFACE,
                                                       QStringLiteral("Get"));
    call.setArguments({iface, prop});
    QDBusMessage reply = QDBusConnection::systemBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    return reply.arguments().first().value<QDBusVariant>().variant();
}

static QString wifiIconFor(int strength)
{
    const char *lvl = strength >= 80 ? "excellent"
                    : strength >= 55 ? "good"
                    : strength >= 30 ? "ok"
                    : strength >= 5  ? "weak"
                                     : "none";
    return QStringLiteral("network-wireless-signal-%1").arg(QLatin1String(lvl));
}

NetworkControl::NetworkControl(QObject *parent)
    : QObject(parent)
{
    m_rescanDebounce.setSingleShot(true);
    m_rescanDebounce.setInterval(500);
    connect(&m_rescanDebounce, &QTimer::timeout, this, &NetworkControl::rescan);

    // Any NM property change (state, active connection, wifi toggle, signal
    // strength) surfaces as a PropertiesChanged on some object under the NM
    // service. Empty path = match any object.
    QDBusConnection::systemBus().connect(NM_SERVICE, QString(), PROPS_IFACE,
                                         QStringLiteral("PropertiesChanged"),
                                         this, SLOT(scheduleRescan()));

    rescan();
}

void NetworkControl::scheduleRescan()
{
    m_rescanDebounce.start();
}

void NetworkControl::rescan()
{
    const QVariantMap nm = getAll(NM_PATH, NM_IFACE);
    if (nm.isEmpty()) {
        if (m_available) {
            m_available = false;
            m_iconName = QStringLiteral("network-offline");
            m_primaryName.clear();
            emit changed();
        }
        return;
    }

    m_available = true;
    m_wifiEnabled = nm.value(QStringLiteral("WirelessEnabled")).toBool();
    const bool networking = nm.value(QStringLiteral("NetworkingEnabled")).toBool();
    const uint state = nm.value(QStringLiteral("State")).toUInt();
    m_primaryType = nm.value(QStringLiteral("PrimaryConnectionType")).toString();

    const QString primaryActive =
        nm.value(QStringLiteral("PrimaryConnection")).value<QDBusObjectPath>().path();
    m_primaryName.clear();
    if (!primaryActive.isEmpty() && primaryActive != QLatin1String("/"))
        m_primaryName = getProp(primaryActive, NM_ACTIVE_IFACE, QStringLiteral("Id")).toString();

    // Wi-Fi signal of the active access point (if the primary link is wireless).
    m_signal = 0;
    if (m_primaryType.contains(QLatin1String("wireless"))) {
        const QVariant devsVar = nm.value(QStringLiteral("Devices"));
        QList<QDBusObjectPath> devs;
        if (devsVar.canConvert<QDBusArgument>())
            devsVar.value<QDBusArgument>() >> devs;
        for (const QDBusObjectPath &d : devs) {
            const QString ap = getProp(d.path(), NM_WIRELESS_IFACE,
                                       QStringLiteral("ActiveAccessPoint"))
                                   .value<QDBusObjectPath>().path();
            if (!ap.isEmpty() && ap != QLatin1String("/")) {
                m_signal = getProp(ap, NM_AP_IFACE, QStringLiteral("Strength")).toInt();
                break;
            }
        }
    }

    // Derive the status icon.
    if (!networking || state <= 20) {
        m_iconName = QStringLiteral("network-offline");
    } else if (m_primaryType.contains(QLatin1String("wireless"))) {
        m_iconName = wifiIconFor(m_signal);
    } else if (!m_primaryType.isEmpty()) {
        m_iconName = QStringLiteral("network-wired-activated");
    } else {
        m_iconName = QStringLiteral("network-idle");
    }

    emit changed();
}

QVariantList NetworkControl::connections() const
{
    QVariantList result;
    if (!m_available)
        return result;

    // Map each active connection's underlying settings path -> its active path,
    // so saved connections can be flagged active.
    QMap<QString, QString> activeBySettings;
    {
        const QVariant acVar = getProp(NM_PATH, NM_IFACE, QStringLiteral("ActiveConnections"));
        QList<QDBusObjectPath> actives;
        if (acVar.canConvert<QDBusArgument>())
            acVar.value<QDBusArgument>() >> actives;
        for (const QDBusObjectPath &a : actives) {
            const QString conn = getProp(a.path(), NM_ACTIVE_IFACE, QStringLiteral("Connection"))
                                     .value<QDBusObjectPath>().path();
            if (!conn.isEmpty())
                activeBySettings.insert(conn, a.path());
        }
    }

    // List saved connections and read id/type from each.
    QDBusMessage call = QDBusMessage::createMethodCall(
        NM_SERVICE, NM_SETTINGS_PATH, NM_SETTINGS_IFACE, QStringLiteral("ListConnections"));
    QDBusReply<QList<QDBusObjectPath>> reply = QDBusConnection::systemBus().call(call);
    if (!reply.isValid())
        return result;

    for (const QDBusObjectPath &connPath : reply.value()) {
        QDBusMessage gs = QDBusMessage::createMethodCall(NM_SERVICE, connPath.path(),
                                                         NM_CONN_IFACE, QStringLiteral("GetSettings"));
        QDBusMessage gsReply = QDBusConnection::systemBus().call(gs);
        if (gsReply.type() != QDBusMessage::ReplyMessage || gsReply.arguments().isEmpty())
            continue;
        SettingsMap settings;
        gsReply.arguments().first().value<QDBusArgument>() >> settings;
        const QVariantMap conn = settings.value(QStringLiteral("connection"));
        const QString id = conn.value(QStringLiteral("id")).toString();
        const QString type = conn.value(QStringLiteral("type")).toString();
        if (id.isEmpty())
            continue;

        QVariantMap v;
        v[QStringLiteral("path")] = connPath.path();
        v[QStringLiteral("id")] = id;
        v[QStringLiteral("type")] = type;
        v[QStringLiteral("wifi")] = type.contains(QLatin1String("wireless"));
        const QString activePath = activeBySettings.value(connPath.path());
        v[QStringLiteral("active")] = !activePath.isEmpty();
        v[QStringLiteral("activePath")] = activePath;
        result.append(v);
    }

    // Active connections first, then alphabetical by id.
    std::sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap ma = a.toMap(), mb = b.toMap();
        if (ma.value(QStringLiteral("active")).toBool() != mb.value(QStringLiteral("active")).toBool())
            return ma.value(QStringLiteral("active")).toBool();
        return ma.value(QStringLiteral("id")).toString().localeAwareCompare(
                   mb.value(QStringLiteral("id")).toString()) < 0;
    });
    return result;
}

void NetworkControl::activate(const QString &connPath)
{
    if (connPath.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(NM_SERVICE, NM_PATH, NM_IFACE,
                                                       QStringLiteral("ActivateConnection"));
    call.setArguments({QVariant::fromValue(QDBusObjectPath(connPath)),
                       QVariant::fromValue(QDBusObjectPath(QStringLiteral("/"))),
                       QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")))});
    QDBusConnection::systemBus().call(call, QDBus::NoBlock);
    scheduleRescan();
}

void NetworkControl::deactivate(const QString &activePath)
{
    if (activePath.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(NM_SERVICE, NM_PATH, NM_IFACE,
                                                       QStringLiteral("DeactivateConnection"));
    call.setArguments({QVariant::fromValue(QDBusObjectPath(activePath))});
    QDBusConnection::systemBus().call(call, QDBus::NoBlock);
    scheduleRescan();
}

void NetworkControl::setWifiEnabled(bool on)
{
    QDBusMessage call = QDBusMessage::createMethodCall(NM_SERVICE, NM_PATH, PROPS_IFACE,
                                                       QStringLiteral("Set"));
    call.setArguments({NM_IFACE, QStringLiteral("WirelessEnabled"),
                       QVariant::fromValue(QDBusVariant(on))});
    QDBusConnection::systemBus().call(call, QDBus::NoBlock);
    scheduleRescan();
}
