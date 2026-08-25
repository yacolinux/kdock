// org.kdock.Systray on the session bus: single-instance guard plus the commands
// kdock's widget needs to drive this process.
//
// Owning the bus name *is* the single-instance lock: a second launch fails to
// register it, forwards its request to the running instance and exits. Same
// shape as controlmanager/src/cmservice.h.

#pragma once

#include <QObject>
#include <QString>

class SystrayWindow;

class SystrayService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kdock.Systray")
public:
    static QString serviceName();
    static QString objectPath();

    // Whether another instance already owns the bus name.
    static bool alreadyRunning();
    // Fire-and-forget calls into the running instance (used by the second launch
    // and by kdock's SystrayLauncher).
    static void callToggle(const QString &screenName);
    static void callShow(const QString &screenName);
    static void callHide();
    static void callShowSettings();
    static void callQuit();
    static void callReloadConfig();

    explicit SystrayService(SystrayWindow *window, QObject *parent = nullptr);

    // Claims the name and exports the slots. False when the name was taken
    // between alreadyRunning() and here (or when there is no bus).
    bool registerOnBus();

public slots:
    // screenName is the connector of the dock that asked (e.g. "DP-1"). A layer
    // surface really lands on that output.
    Q_SCRIPTABLE void toggle(const QString &screenName);
    Q_SCRIPTABLE void show(const QString &screenName);
    Q_SCRIPTABLE void hide();
    Q_SCRIPTABLE void reloadConfig();
    Q_SCRIPTABLE void showSettings();
    Q_SCRIPTABLE void quit();

private:
    SystrayWindow *m_window;
};
