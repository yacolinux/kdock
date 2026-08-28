#include "screensavermanager.h"

#include "dockconfig.h"
#include "screensaverwindow.h"
#include "virtualdesktops.h"

#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

#include <QtGui/qguiapplication_platform.h>

#include <utility>

ScreensaverIdleNotification::ScreensaverIdleNotification(
    struct ::ext_idle_notification_v1 *object, QObject *parent)
    : QObject(parent)
    , QtWayland::ext_idle_notification_v1(object)
{
}

ScreensaverIdleNotification::~ScreensaverIdleNotification()
{
    if (isInitialized())
        destroy();
}

void ScreensaverIdleNotification::ext_idle_notification_v1_idled()
{
    emit idled();
}

void ScreensaverIdleNotification::ext_idle_notification_v1_resumed()
{
    emit resumed();
}

ScreensaverManager::ScreensaverManager(VirtualDesktops *desktops, QObject *parent)
    : QWaylandClientExtensionTemplate<ScreensaverManager>(2)
    , m_desktops(desktops)
{
    setParent(parent);
    connect(this, &QWaylandClientExtension::activeChanged,
            this, &ScreensaverManager::ensureIdleNotification);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] { reload(); });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] { reload(); });
    m_configTimer = new QTimer(this);
    m_configTimer->setInterval(1000);
    connect(m_configTimer, &QTimer::timeout, this, &ScreensaverManager::reload);
    m_configTimer->start();
    initialize();
    ensureIdleNotification();
}

ScreensaverManager::~ScreensaverManager()
{
    hideAll();
    for (ScreensaverWindow *window : std::as_const(m_windows))
        delete window;
    m_windows.clear();
}

void ScreensaverManager::ensureIdleNotification()
{
    const bool enabled = DockConfig::screensaverEnabled();
    const int timeout = DockConfig::screensaverTimeoutSeconds();
    if (!enabled) {
        m_idleState = false;
        hideAll();
        delete m_idle;
        m_idle = nullptr;
        return;
    }
    if (!isActive())
        return;
    if (m_idle && timeout == m_idleTimeoutSeconds)
        return;

    delete m_idle;
    m_idle = nullptr;
    m_idleTimeoutSeconds = timeout;
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    wl_seat *seat = native ? native->seat() : nullptr;
    if (!seat)
        return;
    m_idle = new ScreensaverIdleNotification(
        get_input_idle_notification(uint(timeout) * 1000u, seat), this);
    connect(m_idle, &ScreensaverIdleNotification::idled, this, [this] {
        m_idleState = true;
        activateConfigured();
    });
    connect(m_idle, &ScreensaverIdleNotification::resumed, this, [this] {
        m_idleState = false;
        hideAll();
    });
}

void ScreensaverManager::reload()
{
    ensureIdleNotification();
    removeMissingWindows();
    for (ScreensaverWindow *window : std::as_const(m_windows)) {
        if (window->isVisible())
            window->refreshConfig();
    }
    if (!DockConfig::screensaverEnabled())
        return;
    if (m_idleState)
        activateConfigured();
}

void ScreensaverManager::ensureWindow(const QString &screenName)
{
    if (screenName.isEmpty() || m_windows.contains(screenName))
        return;
    int monitorIndex = 0;
    const auto screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        if (screens.at(i)->name() == screenName) {
            monitorIndex = i;
            break;
        }
    }
    auto *window = new ScreensaverWindow(screenName, m_desktops, nullptr, monitorIndex);
    connect(window, &ScreensaverWindow::userDismissed, this, [this] { hideAll(); });
    m_windows.insert(screenName, window);
}

void ScreensaverManager::removeMissingWindows()
{
    const QStringList connected = [&] {
        QStringList names;
        for (QScreen *screen : QGuiApplication::screens())
            names << screen->name();
        return names;
    }();
    for (auto it = m_windows.begin(); it != m_windows.end();) {
        if (connected.contains(it.key())) {
            ++it;
            continue;
        }
        delete it.value();
        it = m_windows.erase(it);
    }
}

void ScreensaverManager::activateConfigured()
{
    if (!DockConfig::screensaverEnabled())
        return;
    const QStringList selected = DockConfig::screensaverScreens();
    for (auto it = m_windows.cbegin(); it != m_windows.cend(); ++it) {
        if (!selected.contains(it.key()))
            it.value()->hideSaver();
    }
    for (const QString &screen : selected) {
        ensureWindow(screen);
        if (auto *window = m_windows.value(screen))
            window->showSaver();
    }
}

void ScreensaverManager::activate(const QString &screenName, int engine,
                                  const QString &page)
{
    const QString target = screenName.isEmpty()
        ? (QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->name() : QString())
        : screenName;
    if (target.isEmpty())
        return;
    ensureWindow(target);
    if (auto *window = m_windows.value(target))
        window->showSaver(engine, page);
}

void ScreensaverManager::hideAll()
{
    for (ScreensaverWindow *window : std::as_const(m_windows))
        window->hideSaver();
}
