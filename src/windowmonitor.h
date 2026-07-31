// Backend-neutral view of the compositor's window list. Two pure-Qt
// implementations exist, picked at runtime by WindowMonitor::create():
//   - wlr-foreign-toplevel-management-v1 (wlroots: sway, hyprland, ...)
//   - org_kde_plasma_window_management   (KWin)
// Both speak the wire protocol directly through qtwaylandscanner bindings.

#pragma once

#include <QList>
#include <QObject>

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

    virtual void activate() = 0;
    virtual void minimize() = 0;
    virtual void unminimize() {}
    virtual void requestClose() = 0;

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
