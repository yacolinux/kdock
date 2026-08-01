// Actions on whatever window the compositor considers active, for the
// "closewindow" widget: close it (left click) or push it to the next virtual
// desktop while the user stays where they are (right click).
//
// The active window is read from the dock's own WindowMonitor, not from KWin:
// the dock already tracks `activated` for the task buttons, and its layer
// surface asks for no keyboard interactivity, so clicking a widget never makes
// the dock itself the active window.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

class WindowMonitor;
class AbstractWindow;

class ActiveWindowControl : public QObject
{
    Q_OBJECT
    // There is a window backend at all (false on X11 and on compositors that
    // offer neither toplevel protocol).
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    explicit ActiveWindowControl(WindowMonitor *monitor, QObject *parent = nullptr);

    bool available() const { return m_monitor != nullptr; }

    Q_INVOKABLE void closeActive();
    // No-op unless the backend can change desktops (plasma-window only) and
    // KWin answers on D-Bus with the desktop list.
    Q_INVOKABLE void sendActiveToNextDesktop();

signals:
    void availableChanged();

private:
    AbstractWindow *activeWindow() const;
    // One property off org.kde.KWin /VirtualDesktopManager, read the long way
    // round (see the .cpp for why QDBusInterface::property is not enough).
    static QVariant desktopProperty(const char *name);
    // Ordered uuids of KWin's virtual desktops, from
    // org.kde.KWin /VirtualDesktopManager. Empty when KWin is not reachable.
    static QStringList desktopIds();
    static QString currentDesktopId();

    WindowMonitor *m_monitor = nullptr;
};
