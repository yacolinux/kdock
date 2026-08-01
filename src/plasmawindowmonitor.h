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
    PlasmaWindow(struct ::org_kde_plasma_window *object, QObject *parent);
    ~PlasmaWindow() override;

    bool ready = false; // initial_state received

    void activate() override;
    void minimize() override;
    void unminimize() override;
    void requestClose() override;
    // Available since version 8 of the interface; the client binds 20.
    bool canChangeDesktop() const override { return true; }
    void moveToDesktop(const QString &enterId, const QString &leaveId) override;

signals:
    void initialStateDone();

protected:
    void org_kde_plasma_window_title_changed(const QString &title) override;
    void org_kde_plasma_window_app_id_changed(const QString &appId) override;
    void org_kde_plasma_window_state_changed(uint32_t flags) override;
    void org_kde_plasma_window_initial_state() override;
    void org_kde_plasma_window_unmapped() override;
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
    void wrapWindow(struct ::org_kde_plasma_window *window);
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
