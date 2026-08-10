#include "dockservice.h"

#include "apprestart.h"
#include "dockconfig.h"
#include "dockmanager.h"
#include "dockwindow.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QProcess>

QString DockService::serviceName()
{
    return QStringLiteral("org.kdock.Dock");
}

QString DockService::objectPath()
{
    return QStringLiteral("/Dock");
}

DockService::DockService(DockManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
    m_lastDarkMode = DockConfig::anyDarkModeActive();
    // The process-wide ping, not a per-instance darkModeChanged(): with fifteen
    // docks that one fires fifteen times per toggle, and this is a bus signal.
    if (DarkModeNotifier *notifier = DockConfig::darkModeNotifier()) {
        connect(notifier, &DarkModeNotifier::changed, this, [this] {
            const bool now = DockConfig::anyDarkModeActive();
            if (now == m_lastDarkMode)
                return;
            m_lastDarkMode = now;
            emit darkModeChanged(now);
        });
    }
}

bool DockService::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (!bus.registerService(serviceName()))
        return false;
    return bus.registerObject(objectPath(), this,
                              QDBusConnection::ExportScriptableSlots
                                  | QDBusConnection::ExportScriptableSignals);
}

void DockService::openSettings(const QString &dockId)
{
    if (!m_manager)
        return;
    if (DockWindow *window = m_manager->windowFor(dockId))
        window->openSettings();
}

void DockService::restart()
{
    // Same thing DockWindow::restart() does — accessories included. Reached
    // even when no dock window exists right now (every dock hidden by the
    // virtual-desktop rule, for instance).
    kdock::restartAll();
}

bool DockService::darkMode()
{
    return DockConfig::anyDarkModeActive();
}

void DockService::setDarkMode(bool on)
{
    if (!m_manager)
        return;
    // setDarkModeActive() resolves the scope itself (app-wide vs. per dock) and
    // notifies every instance, so asking the primary dock's config is asking the
    // whole app. Without a primary (no dock on the primary monitor right now)
    // any config will do: with app-wide scope they all write the same key.
    QString target = m_manager->primaryDockId();
    if (target.isEmpty()) {
        const QStringList docks = m_manager->configuredDocks();
        if (docks.isEmpty())
            return;
        target = docks.first();
    }
    if (DockConfig *config = m_manager->configFor(target))
        config->setDarkModeActive(on);
}

void DockService::toggleDarkMode()
{
    setDarkMode(!darkMode());
}

QStringList DockService::dockIds()
{
    return m_manager ? m_manager->configuredDocks() : QStringList();
}

QStringList DockService::dockScreens()
{
    QStringList out;
    if (!m_manager)
        return out;
    const QStringList ids = m_manager->configuredDocks();
    out.reserve(ids.size());
    for (const QString &id : ids)
        out.append(DockConfig::screenOfDockId(id));
    return out;
}

QString DockService::primaryDockId()
{
    return m_manager ? m_manager->primaryDockId() : QString();
}
