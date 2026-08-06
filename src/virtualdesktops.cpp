#include "virtualdesktops.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QMap>

namespace {
constexpr auto kService = "org.kde.KWin";
constexpr auto kPath = "/VirtualDesktopManager";
constexpr auto kIface = "org.kde.KWin.VirtualDesktopManager";
} // namespace

VirtualDesktops::VirtualDesktops(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    // The switch itself. KWin sends the uuid; the position is looked up in the
    // list we cache below.
    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
                QStringLiteral("currentChanged"), this,
                SLOT(onCurrentChanged(QString)));
    // Adding or removing a desktop renumbers everything after it, so the whole
    // list (and the current position) has to be re-read. A rename only changes
    // a name, but that name is what the pager and the menus show.
    for (const char *signal : {"desktopCreated", "desktopRemoved", "desktopDataChanged"}) {
        bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
                    QString::fromLatin1(signal), this, SLOT(refreshDesktops()));
    }

    refreshDesktops();
}

QVariant VirtualDesktops::desktopProperty(const char *name)
{
    // Properties.Get by hand instead of QDBusInterface::property(): `desktops`
    // is an a(uss), and QDBusInterface refuses to demarshal a struct type that
    // was never registered with qDBusRegisterMetaType — it logs "type
    // QDBusRawType<...> must be registered" and hands back an invalid QVariant,
    // so the caller would silently see no desktops at all.
    QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kService), QLatin1String(kPath),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    msg << QLatin1String(kIface) << QLatin1String(name);
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    QVariant value = reply.arguments().constFirst();
    if (value.canConvert<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    return value;
}

void VirtualDesktops::refreshDesktops()
{
    const QStringList previousIds = m_ids;
    const QStringList previousNames = m_names;
    m_ids.clear();
    m_names.clear();

    const QVariant value = desktopProperty("desktops");
    if (value.canConvert<QDBusArgument>()) {
        // a(uss) = (position, uuid, name), walked by hand — see
        // desktopProperty(). The local **must be const**: the non-const
        // beginArray()/beginStructure() are the *writing* overloads, and using
        // them on a reply desyncs the demarshalling until libdbus aborts the
        // whole process ("type struct not a basic type"), which in the dock is
        // a crash rather than a wrong value.
        const QDBusArgument arg = value.value<QDBusArgument>();
        QMap<uint, QPair<QString, QString>> byPosition;
        arg.beginArray();
        while (!arg.atEnd()) {
            uint position = 0;
            QString id;
            QString name;
            arg.beginStructure();
            arg >> position >> id >> name;
            arg.endStructure();
            if (!id.isEmpty())
                byPosition.insert(position, {id, name});
        }
        arg.endArray();
        // KWin numbers positions from 0; ours are 1-based, so the list index is
        // the position and callers add one.
        for (const auto &entry : std::as_const(byPosition)) {
            m_ids << entry.first;
            m_names << entry.second;
        }
    }

    refreshCurrent();
    // Names too, not just ids: `names` notifies through this signal and a
    // rename leaves the uuids untouched.
    if (m_ids != previousIds || m_names != previousNames)
        emit countChanged();
}

void VirtualDesktops::refreshCurrent()
{
    const QString id = desktopProperty("current").toString();
    const int previous = m_current;
    m_currentId = id;
    const int index = m_ids.indexOf(id);
    m_current = index < 0 ? 0 : index + 1;
    if (m_current != previous)
        emit currentChanged(m_current);
}

void VirtualDesktops::onCurrentChanged(const QString &id)
{
    const int previous = m_current;
    m_currentId = id;
    const int index = m_ids.indexOf(id);
    if (index < 0) {
        // A uuid we don't know yet: the desktop list changed without us hearing
        // about it. Re-read everything rather than reporting "unknown".
        refreshDesktops();
        return;
    }
    m_current = index + 1;
    if (m_current != previous)
        emit currentChanged(m_current);
}

QString VirtualDesktops::nameOf(int position) const
{
    if (position < 1 || position > m_names.size())
        return {};
    return m_names.at(position - 1);
}

QString VirtualDesktops::currentDesktopId() const
{
    return m_currentId;
}

QString VirtualDesktops::idOf(int position) const
{
    if (position < 1 || position > m_ids.size())
        return {};
    return m_ids.at(position - 1);
}

void VirtualDesktops::switchTo(int position)
{
    const QString id = idOf(position);
    if (id.isEmpty() || id == m_currentId)
        return;

    // `current` is a writable property, so switching is a Properties.Set with
    // the uuid. KWin answers with currentChanged, which is what actually
    // updates our state — nothing is assumed here.
    QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kService), QLatin1String(kPath),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Set"));
    msg << QLatin1String(kIface) << QStringLiteral("current")
        << QVariant::fromValue(QDBusVariant(id));
    QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
}
