// org.kdock.ControlManager on the session bus: single-instance guard plus the
// commands kdock's widget needs to drive this process.
//
// Owning the bus name *is* the single-instance lock: a second launch fails to
// register it, forwards its request to the running instance and exits. Same
// shape as tilemenu/src/tilemenuservice.h.

#pragma once

#include <QObject>
#include <QString>

class CmWindow;

class CmService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kdock.ControlManager")
public:
    static QString serviceName();
    static QString objectPath();

    // Whether another instance already owns the bus name.
    static bool alreadyRunning();
    // Fire-and-forget calls into the running instance (used by the second launch
    // and by kdock's ControlManagerLauncher).
    static void callToggle(const QString &screenName);
    static void callShow(const QString &screenName);
    static void callHide();
    static void callShowSettings();
    static void callShowSection(const QString &sectionId, const QString &screenName);
    static void callQuit();
    // Tell a running panel that its .conf changed underneath it.
    static void callReloadConfig();

    explicit CmService(CmWindow *window = nullptr, QObject *parent = nullptr);

    // Claim the single-instance name before the relatively expensive backends
    // and QML window are built, closing the startup race with duplicate launches.
    void setWindow(CmWindow *window) { m_window = window; }

    // Claims the name and exports the slots. False when the name was taken
    // between alreadyRunning() and here (or when there is no bus).
    bool registerOnBus();

public slots:
    // screenName is the connector of the dock that asked (e.g. "DP-1"). Unlike
    // the tile menu's toplevel, a layer surface really lands on that output.
    Q_SCRIPTABLE void toggle(const QString &screenName);
    Q_SCRIPTABLE void show(const QString &screenName);
    Q_SCRIPTABLE void hide();
    Q_SCRIPTABLE void reloadConfig();
    // Open the panel straight on one section's tab.
    Q_SCRIPTABLE void showSection(const QString &sectionId, const QString &screenName);
    Q_SCRIPTABLE void showSettings();
    Q_SCRIPTABLE void quit();

private:
    CmWindow *m_window;
};
