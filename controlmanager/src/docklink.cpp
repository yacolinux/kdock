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
    const QVariantList wasDocks = m_docks;

    m_available = false;
    m_docks.clear();

    QDBusConnection bus = QDBusConnection::sessionBus();
    auto *iface = bus.interface();
    if (!iface || !iface->isServiceRegistered(kService)) {
        if (wasAvailable != m_available || wasDocks != m_docks)
            emit changed();
        return;
    }

    QDBusInterface dock(kService, kPath, kIface, bus);
    if (!dock.isValid()) {
        if (wasAvailable != m_available || wasDocks != m_docks)
            emit changed();
        return;
    }
    m_available = true;

    const QDBusReply<bool> dark = dock.call(QStringLiteral("darkMode"));
    if (dark.isValid())
        m_darkMode = dark.value();

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

    if (wasAvailable != m_available || wasDark != m_darkMode || wasDocks != m_docks)
        emit changed();
}

void DockLink::onDarkModeChanged(bool on)
{
    if (m_darkMode == on)
        return;
    m_darkMode = on;
    emit changed();
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
