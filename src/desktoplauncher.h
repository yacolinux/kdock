// kdock's involvement with the desktop-widget canvas: find the binary, launch
// it, restart it, and open its settings dialog.
//
// kdock-desktop is a separate process with its own config (desktop.conf) and its
// own settings dialog — the same split as kdock-controlmanager, which this class
// mirrors. The difference is lifecycle: the canvas is *always on*, not a panel
// that toggles, so "preload" here means "run it with the session" and there is
// no toggle/hide path from the dock.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class DesktopLauncher : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    // Open the canvas' own settings dialog: over D-Bus when it is running,
    // otherwise by starting the process with --settings.
    Q_INVOKABLE void openSettings();
    // Bring the canvas up (visible) if it is not already.
    Q_INVOKABLE void launch();
    // Stop and start again, so a freshly installed binary takes over.
    Q_INVOKABLE void restart();

    static QString binaryPath();
    static bool installed();
    static bool running();
    static void quitRunning();

    // Whether kdock should start the canvas at startup. Stored in desktop.conf,
    // read here without constructing a CmConfig (same idiom as the control
    // panel's launcher).
    static bool preload();
    static void setPreload(bool on);
    // Called from main.cpp: start the canvas if preload is on and it is not up.
    static void startIfPreloading();

private:
    static bool start(const QStringList &args = {});
};
