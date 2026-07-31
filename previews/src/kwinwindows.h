// KWin window list for the preview strips: org_kde_plasma_window_management,
// spoken directly through the qtwaylandscanner bindings.
//
// Why not reuse kdock's WindowMonitor/PlasmaWindow (src/plasmawindowmonitor.cpp):
//   - The previews need three things kdock's backend-neutral AbstractWindow does
//     not carry: the window's **uuid** (the handle ScreenShot2 wants), its
//     geometry (which monitor it is on, and the card's aspect ratio) and its
//     virtual desktops.
//   - Adding the `geometry` event there would make PlasmaWindow emit changed()
//     on every window move, and DockModel rebuilds on that signal — a free
//     regression in the dock that works today. So the signals here are granular
//     on purpose.
//   - Previews is KWin-only anyway: both ScreenShot2 and zkde_screencast are.
//
// KWin treats this interface as privileged: the installed .desktop must declare
//   X-KDE-Wayland-Interfaces=org_kde_plasma_window_management
// and its Exec= must be the absolute path of this binary.

#pragma once

#include <QObject>
#include <QRect>
#include <QStringList>
#include <QtWaylandClient/QWaylandClientExtension>

#include "qwayland-plasma-window-management.h"

class KWinWindows;

class KWinWindow : public QObject, public QtWayland::org_kde_plasma_window
{
    Q_OBJECT
public:
    KWinWindow(struct ::org_kde_plasma_window *object, const QString &uuid, QObject *parent);
    ~KWinWindow() override;

    // KWin's internal window id, as sent by window_with_uuid. This is also the
    // `handle` argument of org.kde.KWin.ScreenShot2.CaptureWindow.
    QString uuid() const { return m_uuid; }
    QString appId() const { return m_appId; }
    QString title() const { return m_title; }
    QString themedIconName() const { return m_themedIconName; }
    // Absolute coordinates, decorations included. Null until KWin sends the
    // first geometry event.
    QRect geometry() const { return m_geometry; }
    // Virtual desktop ids. Empty means "on all desktops" (per the protocol).
    QStringList desktops() const { return m_desktops; }
    bool onDesktop(const QString &id) const
    {
        return m_desktops.isEmpty() || m_desktops.contains(id);
    }

    bool active() const { return m_active; }
    bool minimized() const { return m_minimized; }
    bool skipTaskbar() const { return m_skipTaskbar; }
    bool ready() const { return m_ready; } // initial_state received

    // Raise + focus. Unminimizes first: activating a minimized window through
    // set_state(ACTIVE) alone leaves it minimized on KWin.
    void activate();
    void minimize();
    void unminimize();
    void requestClose();

signals:
    void initialStateDone();
    // Title / app id / icon name: the card's text and icon.
    void metadataChanged();
    // Active / minimized / skip-taskbar: filters, highlight and how often the
    // thumbnail is refreshed.
    void stateChanged();
    // Which monitor the card belongs to, and the thumbnail's aspect ratio.
    void geometryChanged();
    void desktopsChanged();
    void closed();

protected:
    void org_kde_plasma_window_title_changed(const QString &title) override;
    void org_kde_plasma_window_app_id_changed(const QString &appId) override;
    void org_kde_plasma_window_themed_icon_name_changed(const QString &name) override;
    void org_kde_plasma_window_state_changed(uint32_t flags) override;
    void org_kde_plasma_window_geometry(int32_t x, int32_t y, uint32_t width,
                                        uint32_t height) override;
    void org_kde_plasma_window_virtual_desktop_entered(const QString &id) override;
    void org_kde_plasma_window_virtual_desktop_left(const QString &id) override;
    void org_kde_plasma_window_initial_state() override;
    void org_kde_plasma_window_unmapped() override;

private:
    QString m_uuid;
    QString m_appId;
    QString m_title;
    QString m_themedIconName;
    QRect m_geometry;
    QStringList m_desktops;
    bool m_active = false;
    bool m_minimized = false;
    bool m_skipTaskbar = false;
    bool m_ready = false;
};

class KWinWindows : public QWaylandClientExtensionTemplate<KWinWindows>,
                    public QtWayland::org_kde_plasma_window_management
{
    Q_OBJECT
public:
    explicit KWinWindows(QObject *parent = nullptr);

    bool available() const { return isActive(); }
    // Windows whose initial state burst has completed, in the order KWin
    // announced them.
    QList<KWinWindow *> windows() const { return m_windows; }

signals:
    void windowAdded(KWinWindow *window);
    void windowRemoved(KWinWindow *window);

protected:
    void org_kde_plasma_window_management_window_with_uuid(uint32_t id,
                                                           const QString &uuid) override;

private:
    QList<KWinWindow *> m_windows;
};
