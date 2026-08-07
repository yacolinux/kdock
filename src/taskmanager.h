// wlroots backend: wlr-foreign-toplevel-management-v1
// (sway, hyprland, wayfire, ...). Uses only public QtWaylandClient API.

#pragma once

#include "windowmonitor.h"

#include <QPointer>
#include <QtWaylandClient/QWaylandClientExtension>

#include "qwayland-wlr-foreign-toplevel-management-unstable-v1.h"

class WlrWindow : public AbstractWindow, public QtWayland::zwlr_foreign_toplevel_handle_v1
{
    Q_OBJECT
public:
    WlrWindow(struct ::zwlr_foreign_toplevel_handle_v1 *object, QObject *parent);
    ~WlrWindow() override;

    bool ready = false; // first "done" received

    void activate() override; // raise + focus on the current seat
    void minimize() override { set_minimized(); }
    void unminimize() override { unset_minimized(); }
    void requestClose() override { zwlr_foreign_toplevel_handle_v1::close(); }
    bool canMaximize() const override { return true; }
    void maximize() override { set_maximized(); }
    void setMaximized(bool on) override { if (on) set_maximized(); else unset_maximized(); }

signals:
    void firstDone();

protected:
    void zwlr_foreign_toplevel_handle_v1_title(const QString &t) override;
    void zwlr_foreign_toplevel_handle_v1_app_id(const QString &id) override;
    void zwlr_foreign_toplevel_handle_v1_state(wl_array *state) override;
    void zwlr_foreign_toplevel_handle_v1_done() override;
    void zwlr_foreign_toplevel_handle_v1_closed() override;
};

class WlrTaskManager : public QWaylandClientExtensionTemplate<WlrTaskManager>,
                       public QtWayland::zwlr_foreign_toplevel_manager_v1
{
    Q_OBJECT
public:
    explicit WlrTaskManager(WindowMonitor *owner);
    ~WlrTaskManager() override;

    bool active() const { return isActive(); }

protected:
    void zwlr_foreign_toplevel_manager_v1_toplevel(struct ::zwlr_foreign_toplevel_handle_v1 *toplevel) override;

private:
    WindowMonitor *m_owner;
};

class WlrBackend : public WindowMonitor
{
    Q_OBJECT
public:
    explicit WlrBackend(QObject *parent = nullptr);
    bool active() const { return m_extension.active(); }

    // No wlr "show desktop" protocol: emulate by minimizing every window
    // and restoring the ones we minimized.
    bool showDesktopSupported() const override { return true; }
    bool showDesktopActive() const override { return m_showDesktopActive; }
    void toggleShowDesktop() override;

private:
    WlrTaskManager m_extension;
    bool m_showDesktopActive = false;
    QList<QPointer<AbstractWindow>> m_minimizedByUs;
};
