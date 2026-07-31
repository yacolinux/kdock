// org.kdock.Previews on the session bus: single-instance guard plus the handful
// of commands kdock needs to drive this process (its Previews tab has a checkbox
// and a "Configurar" button, nothing more).
//
// Owning the bus name *is* the single-instance lock: a second launch fails to
// register it, forwards its request to the running instance and exits.

#pragma once

#include <QObject>

class PreviewManager;

class PreviewsService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kdock.Previews")
public:
    static QString serviceName();
    static QString objectPath();

    // Whether another instance already owns the bus name.
    static bool alreadyRunning();
    // Fire-and-forget calls into the running instance (used by the second launch
    // and by kdock's PreviewsLauncher).
    static void callShowSettings();
    static void callQuit();
    static void callReload();

    explicit PreviewsService(PreviewManager *manager, QObject *parent = nullptr);

    // Claims the name and exports the slots. False when the name was taken
    // between alreadyRunning() and here (or when there is no bus).
    bool registerOnBus();

public slots:
    Q_SCRIPTABLE void showSettings();
    // Re-read the master switch and the per-monitor list, then apply. Called by
    // kdock right after it flips "Activar Dock Preview" on an already running
    // instance.
    Q_SCRIPTABLE void reload();
    Q_SCRIPTABLE void quit();

private:
    PreviewManager *m_manager;
};
