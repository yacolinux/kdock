// Backend-neutral view of the compositor's window list. Two pure-Qt
// implementations exist, picked at runtime by WindowMonitor::create():
//   - wlr-foreign-toplevel-management-v1 (wlroots: sway, hyprland, ...)
//   - org_kde_plasma_window_management   (KWin)
// Both speak the wire protocol directly through qtwaylandscanner bindings.

#pragma once

#include <QList>
#include <QObject>
#include <QRect>

class AbstractWindow : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    QString title;
    QString appId;
    bool activated = false;
    bool minimized = false;
    bool skipTaskbar = false;
    bool maximized = false;
    bool maximizable = false;
    bool transient = false; // has a parent window (dialog, popup)
    // uuids of the virtual desktops this window is on. **Empty means "on all
    // desktops"**, which is how the plasma-window protocol spells it — not
    // "on none". Only the plasma-window backend fills it in.
    QStringList desktops;
    // Absolute geometry, updated by org_kde_plasma_window_geometry (KWin-only).
    QRect geometry;

    virtual void activate() = 0;
    virtual void minimize() = 0;
    virtual void unminimize() {}
    virtual void requestClose() = 0;

    virtual bool canMaximize() const { return false; }
    virtual void maximize() {}
    virtual void setMaximized(bool on) { Q_UNUSED(on); }

    // Move the window between virtual desktops *without* switching the view to
    // it — the compositor is told which desktop to add and which to drop, and
    // never which one to show. Only the plasma-window backend implements it
    // (wlr-foreign-toplevel has no desktop requests), hence the capability
    // query; desktops are identified by the uuid strings KWin also publishes
    // over D-Bus on /VirtualDesktopManager.
    virtual bool canChangeDesktop() const { return false; }
    virtual void moveToDesktop(const QString &enterId, const QString &leaveId)
    {
        Q_UNUSED(enterId)
        Q_UNUSED(leaveId)
    }
    // Leave the window on exactly one desktop: enter `enterId` and drop every
    // other desktop it was on. Separate from moveToDesktop() because the caller
    // there knows the single desktop to leave, while here the answer is the
    // window's own state — which only the backend tracks.
    virtual void moveToOnlyDesktop(const QString &enterId) { Q_UNUSED(enterId) }

signals:
    void changed();
    void windowClosed();
};

class WindowMonitor : public QObject
{
    Q_OBJECT
    // "Show desktop" (peek): toggle minimizing every window. Exposed to QML
    // as the "showdesktop" context property.
    Q_PROPERTY(bool showDesktopSupported READ showDesktopSupported CONSTANT)
    Q_PROPERTY(bool showDesktopActive READ showDesktopActive NOTIFY showDesktopChanged)
public:
    using QObject::QObject;

    // Returns the first supported backend, or nullptr when the compositor
    // offers neither protocol (the dock then only shows launchers).
    static WindowMonitor *create(QObject *parent = nullptr);

    QList<AbstractWindow *> windows;

    // Called by the protocol extensions once a window's initial state is complete
    void registerWindow(AbstractWindow *window);

    virtual bool showDesktopSupported() const { return false; }
    virtual bool showDesktopActive() const { return false; }
    Q_INVOKABLE virtual void toggleShowDesktop() {}
    Q_INVOKABLE virtual void minimizeAllWindows();

signals:
    void windowAdded(AbstractWindow *window);
    void windowRemoved(AbstractWindow *window);
    void showDesktopChanged();
};
