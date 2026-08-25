// kdock's whole involvement with the system tray: find the binary, toggle its
// window and open its settings.
//
// kdock-systray is a separate, resident process: it owns the StatusNotifierItem
// host and watcher for the session and draws the tray in its own window. Unlike
// weather it must NOT quit on close — the host has to keep collecting items — so
// closing the window only hides it, and this launcher never asks it to quit
// except on an explicit user action. The dock's "systray" widget is just a
// button that toggles that window, the same shape as ControlManagerLauncher.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class SystrayLauncher : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    // Show the window near the given dock screen, or hide it when already up.
    // Starts the process the first time.
    Q_INVOKABLE void toggle(const QString &screenName = QString());
    Q_INVOKABLE void openSettings();

    static QString binaryPath();
    static bool installed();
    static bool running();

    // Whether kdock should start it resident along with itself. Defaults to true:
    // the tray host must be up before session tray clients register.
    static bool preload();
    static void setPreload(bool on);
    // Bring it up resident but hidden (the session-start path).
    static void startIfPreloading();
    // Only on an explicit user request (it drops the whole session's tray).
    static void quitRunning();

private:
    static bool start(const QStringList &args = {});
};
