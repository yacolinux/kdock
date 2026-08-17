// KWin backend: org_kde_plasma_window_management.
// Note: KWin treats this interface as privileged. The app's installed
// .desktop file must declare it:
//   X-KDE-Wayland-Interfaces=org_kde_plasma_window_management

#pragma once

#include "windowmonitor.h"

#include <QtWaylandClient/QWaylandClientExtension>

#include "qwayland-plasma-window-management.h"

class PlasmaWindow : public AbstractWindow, public QtWayland::org_kde_plasma_window
{
    Q_OBJECT
public:
    // `uuid` is the handle KWin announced this window under; it is carried on
    // AbstractWindow because the screenshot API takes nothing else.
    PlasmaWindow(struct ::org_kde_plasma_window *object, const QString &uuid, QObject *parent);
    ~PlasmaWindow() override;

    bool ready = false; // initial_state received

    void activate() override;
    void minimize() override;
    void unminimize() override;
    void requestClose() override;
    bool canMaximize() const override { return true; }
    void maximize() override;
    void setMaximized(bool on) override;
    // Available since version 8 of the interface; the client binds 20.
    bool canChangeDesktop() const override { return true; }
    void moveToDesktop(const QString &enterId, const QString &leaveId) override;
    void moveToOnlyDesktop(const QString &enterId) override;

signals:
    void initialStateDone();

protected:
    void org_kde_plasma_window_title_changed(const QString &title) override;
    void org_kde_plasma_window_app_id_changed(const QString &appId) override;
    void org_kde_plasma_window_state_changed(uint32_t flags) override;
    void org_kde_plasma_window_initial_state() override;
    void org_kde_plasma_window_unmapped() override;
    // The compositor reports desktop membership one desktop at a time, and a
    // window on *all* of them is reported as being on none (see
    // AbstractWindow::desktops).
    void org_kde_plasma_window_virtual_desktop_entered(const QString &id) override;
    void org_kde_plasma_window_virtual_desktop_left(const QString &id) override;
    void org_kde_plasma_window_parent_window(struct ::org_kde_plasma_window *parent) override;
    void org_kde_plasma_window_geometry(int32_t x, int32_t y, uint32_t width, uint32_t height) override;
};

class PlasmaWindowMonitor : public QWaylandClientExtensionTemplate<PlasmaWindowMonitor>,
                            public QtWayland::org_kde_plasma_window_management
{
    Q_OBJECT
public:
    explicit PlasmaWindowMonitor(WindowMonitor *owner);

    bool active() const { return isActive(); }
    void requestShowDesktop(bool show);

protected:
    void org_kde_plasma_window_management_window_with_uuid(uint32_t id, const QString &uuid) override;
    void org_kde_plasma_window_management_show_desktop_changed(uint32_t state) override;

private:
    void wrapWindow(struct ::org_kde_plasma_window *window, const QString &uuid);
    WindowMonitor *m_owner;
};

class PlasmaBackend : public WindowMonitor
{
    Q_OBJECT
public:
    explicit PlasmaBackend(QObject *parent = nullptr);
    bool active() const { return m_extension.active(); }

    bool showDesktopSupported() const override { return true; }
    bool showDesktopActive() const override { return m_showDesktopActive; }
    void toggleShowDesktop() override;

private:
    friend class PlasmaWindowMonitor;
    void setShowDesktopActive(bool active);
    PlasmaWindowMonitor m_extension;
    bool m_showDesktopActive = false;
};
