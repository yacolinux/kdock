// kdock's involvement with the desktop-widget canvas, now one instance per
// monitor: find the binary, launch/quit/restart a per-connector instance, and
// open that instance's settings dialog.
//
// Each monitor runs its own kdock-desktop process, bound to that output, with
// its own bus name (org.kdock.Desktop.<connector>) and its own independent
// config (desktop-<connector>.conf). Two switches govern them, both in the
// shared desktop.conf so kdock can read them without starting any instance:
//   - a master enable ("Activar widgets de escritorio"): off = none run;
//   - a per-monitor list (enabledScreens): which connected monitors get a canvas.
// applyState() reconciles the running processes with those two, and is what the
// Desktop tab, startup and monitor hot-plug all call.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class DesktopLauncher : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    static QString binaryPath();
    static bool installed();

    // --- the two switches, in the shared desktop.conf ---
    // Master on/off for the whole feature. Migrates from the legacy "preload".
    static bool masterEnabled();
    static void setMasterEnabled(bool on);
    // Connectors that should get a canvas (subject to the master and to being
    // connected right now).
    static QStringList enabledScreens();
    static bool screenEnabled(const QString &connector);
    static void setScreenEnabled(const QString &connector, bool on);

    // --- per-instance lifecycle (one process per connector) ---
    static QString serviceFor(const QString &connector);
    static bool runningOn(const QString &connector);
    static void launchOn(const QString &connector);
    static void quitOn(const QString &connector);
    static void restartOn(const QString &connector);
    // Open one monitor's canvas settings dialog (D-Bus if up, else --settings).
    static void openSettingsOn(const QString &connector);

    // Bring the running set in line with master + enabledScreens over the given
    // connected monitors: launch the ones that should run and are not, quit the
    // ones that should not. Pass the current connectors (QGuiApplication::screens
    // by default).
    static void applyState(const QStringList &connectedScreens = {});

    // --- aggregate helpers used by apprestart / startup ---
    static bool running();      // any instance up
    static void quitRunning();  // quit every instance
    static void startEnabled(); // called from main.cpp: applyState() if master

private:
    static bool start(const QStringList &args);
    static QStringList connectedScreensNow();
};
