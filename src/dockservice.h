// org.kdock.Dock on the session bus: the few things about a running dock that
// cannot be done from outside its process.
//
// kdock had no D-Bus service until kdock-controlmanager needed three things:
//
//   - **dark mode**, live. The state lives in the .conf, but writing it from
//     another process repaints nothing: there is no file watcher, and the mode
//     is resolved at paint time by every DockConfig instance (see AGENTS.md →
//     Modo Dark). It has to be a call into this process.
//   - **the settings dialog**, which opens from the dock's own menu and from
//     nowhere else.
//   - **restart**, which relaunches the process with its original arguments.
//
// Deliberately small, and deliberately without a "quit": leaving the user with
// no dock and no way back is not something a stray D-Bus call should be able to
// do. Everything here is idempotent and safe to call twice.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class DockManager;

class DockService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kdock.Dock")
public:
    static QString serviceName();
    static QString objectPath();

    explicit DockService(DockManager *manager, QObject *parent = nullptr);

    // Claims the name and exports the slots. False when the name is taken (a
    // second kdock) or there is no bus — neither is fatal, the dock just does
    // not answer.
    bool registerOnBus();

public slots:
    // Empty dockId = the primary dock (and, if that one is not on screen right
    // now, whichever dock is).
    Q_SCRIPTABLE void openSettings(const QString &dockId);
    // The same dialog, opened straight on the Redes tab: the control panel's
    // network section offers "Configurar redes…" and the full connection editor
    // lives here, in the dock's process.
    Q_SCRIPTABLE void openNetworkSettings(const QString &dockId);
    // Restarts the whole kdock process, i.e. every dock it draws.
    Q_SCRIPTABLE void restart();

    // ColorAuto's "generate a color from the wallpaper and apply it". It lives
    // here rather than in the panel's own process because the engine keeps
    // state that must exist exactly once: which of the two generated schemes is
    // current (plasma-apply-colorscheme ignores a re-apply of the same name, so
    // they alternate). Two processes ping-ponging the same two names would step
    // on each other.
    Q_SCRIPTABLE void generateColorScheme();
    // Keep the generated scheme permanently as kdock-<n>; returns its id, or
    // empty when there was nothing generated yet.
    Q_SCRIPTABLE QString saveColorScheme();

    Q_SCRIPTABLE bool darkMode();
    Q_SCRIPTABLE void setDarkMode(bool on);
    Q_SCRIPTABLE void toggleDarkMode();

    // LXQt wallpaper advance (Fondo de Escritorio QT): delegates to
    // WallpaperControl/LxqtWallpapers in this process so the panel does not
    // need its own renderer. The panel's new card uses this when kdock is
    // running; otherwise it falls back to a local renderer.
    Q_SCRIPTABLE void nextWallpaper(const QString &screenName);
    Q_SCRIPTABLE void nextWallpaperAll();

    // Three parallel lists rather than one a(ssb): plain `as` needs no
    // qDBusRegisterMetaType on either side (see CLAUDE.md on struct properties).
    Q_SCRIPTABLE QStringList dockIds();
    Q_SCRIPTABLE QStringList dockScreens();
    Q_SCRIPTABLE QString primaryDockId();

signals:
    Q_SCRIPTABLE void darkModeChanged(bool on);

private:
    DockManager *m_manager;
    // Last value announced, so the notifier does not re-emit on every repaint.
    bool m_lastDarkMode = false;
};
