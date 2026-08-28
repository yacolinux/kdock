#include "docklink.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QVariantMap>

namespace {
const auto kService = QStringLiteral("org.kdock.Dock");
const auto kPath = QStringLiteral("/Dock");
const auto kIface = QStringLiteral("org.kdock.Dock");
} // namespace

DockLink::DockLink(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        bus.connect(kService, kPath, kIface, QStringLiteral("darkModeChanged"), this,
                    SLOT(onDarkModeChanged(bool)));
        // ColorAuto moves from three places the panel cannot see: the dock's
        // settings tab, its widget, and dark mode suspending it.
        bus.connect(kService, kPath, kIface, QStringLiteral("colorAutoChanged"), this,
                    SLOT(onColorAutoChanged(bool)));
        // kdock restarts often during development, and its own menu has a
        // "Reiniciar" item: follow it in and out instead of going stale.
        bus.connect(QStringLiteral("org.freedesktop.DBus"),
                    QStringLiteral("/org/freedesktop/DBus"),
                    QStringLiteral("org.freedesktop.DBus"),
                    QStringLiteral("NameOwnerChanged"), QStringList{kService}, QString(), this,
                    SLOT(refresh()));
    }
    refresh();
}

void DockLink::refresh()
{
    const bool wasAvailable = m_available;
    const bool wasDark = m_darkMode;
    const bool wasColorAuto = m_colorAutoEnabled;
    const bool wasCanRead = m_colorAutoCanRead;
    const bool wasKnown = m_colorAutoKnown;
    const QVariantList wasDocks = m_docks;

    m_available = false;
    m_docks.clear();
    // No dock means no answer about ColorAuto either. Cleared rather than left
    // stale, and `known` goes with them so the card cannot read a leftover
    // "false" as a real answer.
    m_colorAutoCanRead = false;
    m_colorAutoKnown = false;

    QDBusConnection bus = QDBusConnection::sessionBus();
    auto *iface = bus.interface();
    if (!iface || !iface->isServiceRegistered(kService)) {
        if (wasAvailable != m_available || wasDocks != m_docks
            || wasCanRead != m_colorAutoCanRead || wasKnown != m_colorAutoKnown)
            emit changed();
        return;
    }

    QDBusInterface dock(kService, kPath, kIface, bus);
    if (!dock.isValid()) {
        if (wasAvailable != m_available || wasDocks != m_docks
            || wasCanRead != m_colorAutoCanRead || wasKnown != m_colorAutoKnown)
            emit changed();
        return;
    }
    m_available = true;

    const QDBusReply<bool> dark = dock.call(QStringLiteral("darkMode"));
    if (dark.isValid())
        m_darkMode = dark.value();

    // A dock older than these two methods answers with an error, which is not
    // the same thing as answering "no" — hence `known`. Without it the card
    // could not tell "there is no wallpaper to read" from "this dock has never
    // heard of the question", and it would show the first over the second.
    const QDBusReply<bool> ca = dock.call(QStringLiteral("colorAutoEnabled"));
    const QDBusReply<bool> canRead = dock.call(QStringLiteral("colorAutoCanRead"));
    m_colorAutoKnown = ca.isValid() && canRead.isValid();
    if (m_colorAutoKnown) {
        m_colorAutoEnabled = ca.value();
        m_colorAutoCanRead = canRead.value();
    }

    // The dock list is a(ssb); asking for it as a{sv} list would need a
    // registered metatype, so the service returns three parallel string lists
    // instead — plain `as`, which Qt demarshals with no help.
    const QDBusReply<QStringList> ids = dock.call(QStringLiteral("dockIds"));
    const QDBusReply<QStringList> screens = dock.call(QStringLiteral("dockScreens"));
    const QDBusReply<QString> primary = dock.call(QStringLiteral("primaryDockId"));
    if (ids.isValid()) {
        const QStringList idList = ids.value();
        const QStringList screenList = screens.isValid() ? screens.value() : QStringList();
        for (int i = 0; i < idList.size(); ++i) {
            QVariantMap m;
            m[QStringLiteral("id")] = idList.at(i);
            m[QStringLiteral("screen")] = screenList.value(i);
            m[QStringLiteral("primary")] = primary.isValid() && primary.value() == idList.at(i);
            m_docks.append(m);
        }
    }

    if (wasAvailable != m_available || wasDark != m_darkMode || wasDocks != m_docks
        || wasColorAuto != m_colorAutoEnabled || wasCanRead != m_colorAutoCanRead
        || wasKnown != m_colorAutoKnown)
        emit changed();
}

void DockLink::onDarkModeChanged(bool on)
{
    if (m_darkMode == on)
        return;
    m_darkMode = on;
    emit changed();
}

void DockLink::onColorAutoChanged(bool enabled)
{
    if (m_colorAutoEnabled == enabled)
        return;
    m_colorAutoEnabled = enabled;
    emit changed();
}

void DockLink::setColorAutoEnabled(bool on)
{
    if (!m_available)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kIface,
                                                      QStringLiteral("setColorAutoEnabled"));
    msg.setArguments({on});
    QDBusConnection::sessionBus().asyncCall(msg);
    // NOT optimistic, unlike dark mode: the dock can refuse. setEnabled() is a
    // no-op while dark mode owns the appearance, so a switch that flipped
    // itself here would show "on" over a feature that stayed off. The
    // colorAutoChanged signal is the only thing that moves it.
}

void DockLink::refreshColorAuto()
{
    if (!m_available || !m_colorAutoKnown)
        return;
    const bool was = m_colorAutoCanRead;
    QDBusInterface dock(kService, kPath, kIface, QDBusConnection::sessionBus());
    const QDBusReply<bool> canRead = dock.call(QStringLiteral("colorAutoCanRead"));
    if (!canRead.isValid())
        return; // the dock went away mid-question; refresh() will sort it out
    m_colorAutoCanRead = canRead.value();
    if (was != m_colorAutoCanRead)
        emit changed();
}

void DockLink::generateColorScheme()
{
    if (!m_available)
        return;
    QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall(kService, kPath, kIface,
                                       QStringLiteral("generateColorScheme")));
}

QString DockLink::saveColorScheme()
{
    if (!m_available)
        return {};
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kIface,
                                                      QStringLiteral("saveColorScheme"));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 4000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    return reply.arguments().constFirst().toString();
}

void DockLink::setDarkMode(bool on)
{
    if (!m_available)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kIface,
                                                      QStringLiteral("setDarkMode"));
    msg.setArguments({on});
    QDBusConnection::sessionBus().asyncCall(msg);
    // Optimistic: the darkModeChanged signal confirms a moment later, and a
    // switch that waits for a round trip reads as broken.
    onDarkModeChanged(on);
}

void DockLink::openSettings(const QString &dockId)
{
    if (!m_available)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kIface,
                                                      QStringLiteral("openSettings"));
    msg.setArguments({dockId});
    QDBusConnection::sessionBus().asyncCall(msg);
}

void DockLink::openWallpaperSettings(const QString &dockId)
{
    if (!m_available)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kIface,
                                                      QStringLiteral("openWallpaperSettings"));
    msg.setArguments({dockId});
    QDBusConnection::sessionBus().asyncCall(msg);
}

void DockLink::openNetworkSettings(const QString &dockId)
{
    if (!m_available)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kIface,
                                                      QStringLiteral("openNetworkSettings"));
    msg.setArguments({dockId});
    QDBusConnection::sessionBus().asyncCall(msg);
}

void DockLink::restartDock()
{
    if (!m_available)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kIface,
                                                      QStringLiteral("restart"));
    QDBusConnection::sessionBus().asyncCall(msg);
}

void DockLink::nextWallpaper(const QString &screenName)
{
    if (!m_available)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kIface,
                                                      QStringLiteral("nextWallpaper"));
    msg.setArguments({screenName});
    QDBusConnection::sessionBus().asyncCall(msg);
}

void DockLink::nextWallpaperAll()
{
    if (!m_available)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kIface,
                                                      QStringLiteral("nextWallpaperAll"));
    QDBusConnection::sessionBus().asyncCall(msg);
}

void DockLink::activateScreensaver(const QString &screenName, int engine,
                                   const QString &page)
{
    if (!m_available)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kIface,
                                                      QStringLiteral("activateScreensaver"));
    msg.setArguments({screenName, engine, page});
    QDBusConnection::sessionBus().asyncCall(msg);
}
