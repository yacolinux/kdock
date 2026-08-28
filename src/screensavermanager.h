// Screensaver surfaces and compositor-backed idle detection.
//
// Wayland deliberately does not expose global pointer/keyboard events to
// clients. ext-idle-notify-v1 is the compositor API intended for this job: it
// reports input-idle/resume without grabbing input or needing a privileged
// desktop integration. The notification is session-wide; the configured list
// decides which outputs are covered when that session becomes idle.

#pragma once

#include <QObject>
#include <QHash>
#include <QString>

#include <QtWaylandClient/QWaylandClientExtension>

#include "qwayland-ext-idle-notify-v1.h"

class QScreen;
class QTimer;
class VirtualDesktops;
class ScreensaverWindow;

class ScreensaverIdleNotification : public QObject,
                                     public QtWayland::ext_idle_notification_v1
{
    Q_OBJECT
public:
    ScreensaverIdleNotification(struct ::ext_idle_notification_v1 *object,
                                 QObject *parent);
    ~ScreensaverIdleNotification() override;

signals:
    void idled();
    void resumed();

protected:
    void ext_idle_notification_v1_idled() override;
    void ext_idle_notification_v1_resumed() override;
};

class ScreensaverManager : public QWaylandClientExtensionTemplate<ScreensaverManager>,
                           public QtWayland::ext_idle_notifier_v1
{
    Q_OBJECT
public:
    explicit ScreensaverManager(VirtualDesktops *desktops = nullptr,
                                QObject *parent = nullptr);
    ~ScreensaverManager() override;

    // Manual activation intentionally ignores the per-monitor enabled flag.
    // engine < 0 uses the configured engine; page selects one After Dark page.
    void activate(const QString &screenName = QString(), int engine = -1,
                  const QString &page = QString());
    void hideAll();
    void reload();

private slots:
    void ensureIdleNotification();

private:
    void activateConfigured();
    void ensureWindow(const QString &screenName);
    void removeMissingWindows();

    VirtualDesktops *m_desktops = nullptr;
    ScreensaverIdleNotification *m_idle = nullptr;
    QHash<QString, ScreensaverWindow *> m_windows;
    QTimer *m_configTimer = nullptr;
    int m_idleTimeoutSeconds = 300;
    bool m_idleState = false;
};
