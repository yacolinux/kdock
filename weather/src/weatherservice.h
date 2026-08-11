// org.kdock.Weather on the session bus: single-instance guard plus the two
// commands kdock's widget needs.
//
// Owning the bus name *is* the lock: a second launch fails to register it,
// forwards its request to the running instance and exits. Same shape as
// tilemenu/src/tilemenuservice.h, with one deliberate difference — this window
// **closes for real** (the process quits with it), so nothing of this binary
// survives an install and it stays out of kdock::restartAll().

#pragma once

#include <QObject>
#include <QString>

class WeatherWindow;

class WeatherService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kdock.Weather")
public:
    static QString serviceName();
    static QString objectPath();

    // Whether another instance already owns the bus name.
    static bool alreadyRunning();
    // Fire-and-forget calls into the running instance.
    static void callToggle(const QString &screenName);
    static void callShow(const QString &screenName);
    static void callShowSettings();
    static void callQuit();

    explicit WeatherService(WeatherWindow *window, QObject *parent = nullptr);

    bool registerOnBus();

public slots:
    // screenName is the connector of the dock that asked; Wayland does not let a
    // client place a toplevel, so it is a hint applied through setScreen().
    Q_SCRIPTABLE void toggle(const QString &screenName);
    Q_SCRIPTABLE void show(const QString &screenName);
    Q_SCRIPTABLE void showSettings();
    Q_SCRIPTABLE void quit();

private:
    WeatherWindow *m_window;
};
