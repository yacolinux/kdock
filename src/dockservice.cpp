#include "dockservice.h"

#include "apprestart.h"
#include "dockconfig.h"
#include "autocolorscheme.h"
#include "dockmanager.h"
#include "dockwindow.h"
#include "lxqtwallpapers.h"
#include "wallpapercontrol.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QGuiApplication>
#include <QProcess>
#include <QScreen>
#include <QTimer>

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

    // Same idea for ColorAuto's switch. AutoColorScheme::changed covers every
    // way it moves — the settings tab, the dock widget, and dark mode
    // suspending and resuming it — but it also fires on things that leave the
    // switch alone (a generation, a re-capture of the defaults), so the value
    // is compared before announcing.
    m_lastColorAuto = AutoColorScheme::enabled();
    if (m_manager && m_manager->autoColorScheme()) {
        connect(m_manager->autoColorScheme(), &AutoColorScheme::changed, this, [this] {
            const bool now = AutoColorScheme::enabled();
            if (now == m_lastColorAuto)
                return;
            m_lastColorAuto = now;
            emit colorAutoChanged(now);
        });
    }
}

bool DockService::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    // Export the object once: it is local and does not depend on owning the
    // name, so clients simply cannot reach it until the name is acquired below.
    if (!m_objectExported) {
        if (!bus.registerObject(objectPath(), this,
                                QDBusConnection::ExportScriptableSlots
                                    | QDBusConnection::ExportScriptableSignals))
            return false;
        m_objectExported = true;
    }

    if (bus.registerService(serviceName()))
        return true;

    // Name momentarily taken — the restart race (see the header). Keep trying on
    // the event loop until the outgoing instance releases it, bounded so a real
    // second kdock stops instead of spinning forever.
    if (!m_registerRetry) {
        m_registerRetry = new QTimer(this);
        m_registerRetry->setInterval(kRegisterRetryMs);
        connect(m_registerRetry, &QTimer::timeout, this, [this] {
            if (QDBusConnection::sessionBus().registerService(serviceName())
                || ++m_registerAttempts >= kRegisterMaxTries)
                m_registerRetry->stop();
        });
    }
    m_registerAttempts = 0;
    m_registerRetry->start();
    return false;
}

void DockService::openSettings(const QString &dockId)
{
    if (!m_manager)
        return;
    if (DockWindow *window = m_manager->windowFor(dockId))
        window->openSettings();
}

void DockService::openWallpaperSettings(const QString &dockId)
{
    if (!m_manager)
        return;
    if (DockWindow *window = m_manager->windowFor(dockId))
        window->openWallpaperSettings();
}

void DockService::openNetworkSettings(const QString &dockId)
{
    if (!m_manager)
        return;
    if (DockWindow *window = m_manager->windowFor(dockId))
        window->openNetworkSettings();
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

void DockService::generateColorScheme()
{
    if (!m_manager || !m_manager->autoColorScheme())
        return;
    m_manager->autoColorScheme()->generateNow();
}

QString DockService::saveColorScheme()
{
    if (!m_manager || !m_manager->autoColorScheme())
        return {};
    return m_manager->autoColorScheme()->saveCurrentScheme();
}

bool DockService::colorAutoEnabled()
{
    return AutoColorScheme::enabled();
}

void DockService::setColorAutoEnabled(bool on)
{
    if (!m_manager || !m_manager->autoColorScheme())
        return;
    // Straight through, guards and all: switching on captures the defaults the
    // first time (there has to be a way back) and applies immediately, and
    // switching off restores them and clears the docks. Writing the key from
    // the panel's process would do none of that.
    m_manager->autoColorScheme()->setEnabled(on);
}

bool DockService::colorAutoCanRead()
{
    return m_manager && m_manager->autoColorScheme()
           && m_manager->autoColorScheme()->canRead();
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

void DockService::nextWallpaper(const QString &screenName)
{
    if (!m_manager || !m_manager->sharedWallpaperControl())
        return;
    m_manager->sharedWallpaperControl()->nextWallpaper(screenName);
}

void DockService::nextWallpaperAll()
{
    if (!m_manager || !m_manager->sharedWallpaperControl())
        return;
    m_manager->sharedWallpaperControl()->nextWallpaperAll();
}

bool DockService::setWallpaper(const QString &screenName, const QString &path)
{
    if (!m_manager || !m_manager->lxqtWallpapers())
        return false;

    QString screen = screenName;
    if (screen.isEmpty() && QGuiApplication::primaryScreen())
        screen = QGuiApplication::primaryScreen()->name();

    return m_manager->lxqtWallpapers()->setWallpaper(screen, path);
}
