#include "activewindowcontrol.h"

#include "windowmonitor.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QMap>
#include <QVariant>

namespace {
constexpr auto kService = "org.kde.KWin";
constexpr auto kPath = "/VirtualDesktopManager";
constexpr auto kIface = "org.kde.KWin.VirtualDesktopManager";
} // namespace

ActiveWindowControl::ActiveWindowControl(WindowMonitor *monitor, QObject *parent)
    : QObject(parent)
    , m_monitor(monitor)
{
}

AbstractWindow *ActiveWindowControl::activeWindow() const
{
    if (!m_monitor)
        return nullptr;
    for (AbstractWindow *w : m_monitor->windows) {
        if (w && w->activated)
            return w;
    }
    return nullptr;
}

void ActiveWindowControl::closeActive()
{
    if (AbstractWindow *w = activeWindow())
        w->requestClose();
}

QVariant ActiveWindowControl::desktopProperty(const char *name)
{
    // Read through Properties.Get instead of QDBusInterface::property():
    // `desktops` is an a(iss), and QDBusInterface refuses to hand over a struct
    // type that was never registered with qDBusRegisterMetaType — it logs
    // "type QDBusRawType<...> must be registered" and returns an invalid
    // QVariant, so the widget would silently do nothing (caught by a probe
    // before this ever shipped, 2026-08-01).
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

QStringList ActiveWindowControl::desktopIds()
{
    const QVariant value = desktopProperty("desktops");
    if (!value.canConvert<QDBusArgument>())
        return {};

    // a(iss) = (position, uuid, name), walked by hand — see desktopProperty().
    // The local **must be const**: the non-const beginArray()/beginStructure()
    // are the *writing* overloads, and using them on a reply desyncs the
    // demarshalling until libdbus aborts the whole process ("type struct not a
    // basic type"). That is a crash of the dock, not a wrong value.
    const QDBusArgument arg = value.value<QDBusArgument>();
    QMap<int, QString> byPosition;
    arg.beginArray();
    while (!arg.atEnd()) {
        int position = 0;
        QString id;
        QString name;
        arg.beginStructure();
        arg >> position >> id >> name;
        arg.endStructure();
        if (!id.isEmpty())
            byPosition.insert(position, id);
    }
    arg.endArray();
    return QStringList(byPosition.cbegin(), byPosition.cend());
}

QString ActiveWindowControl::currentDesktopId()
{
    return desktopProperty("current").toString();
}

void ActiveWindowControl::sendActiveToNextDesktop()
{
    AbstractWindow *w = activeWindow();
    if (!w || !w->canChangeDesktop())
        return;

    const QStringList ids = desktopIds();
    if (ids.size() < 2) // Nowhere to send it.
        return;

    // The window we are moving is the active one, so it is on the current
    // desktop by definition: that is the one to leave. (A window pinned to all
    // desktops only loses the current one, which is the same thing KDE's task
    // manager does.)
    const QString current = currentDesktopId();
    int index = ids.indexOf(current);
    if (index < 0)
        index = 0;
    const QString next = ids.at((index + 1) % ids.size());

    w->moveToDesktop(next, current);
}
