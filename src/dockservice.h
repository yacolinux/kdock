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

    // The *automatic* mode: regenerate on every wallpaper change. Separate from
    // the two above, which are the manual path and work with this off.
    //
    // It is a call and not a config write for the same reason dark mode is:
    // switching it on captures the defaults, applies straight away and (under
    // dark mode) may refuse — none of which happens by writing the key from
    // another process, and the panel would show a switch that lies.
    Q_SCRIPTABLE bool colorAutoEnabled();
    Q_SCRIPTABLE void setColorAutoEnabled(bool on);
    // Whether generating right now would find a wallpaper to sample. The panel
    // gates its buttons on this: with no source every press is a silent no-op,
    // and a card that says "Generado" over nothing is worse than a greyed
    // button. See AutoColorScheme::canRead().
    Q_SCRIPTABLE bool colorAutoCanRead();

    Q_SCRIPTABLE bool darkMode();
    Q_SCRIPTABLE void setDarkMode(bool on);
    Q_SCRIPTABLE void toggleDarkMode();

    // LXQt wallpaper advance (Fondo de Escritorio QT): delegates to
    // WallpaperControl/LxqtWallpapers in this process so the panel does not
    // need its own renderer. The panel's new card uses this when kdock is
    // running; otherwise it falls back to a local renderer.
    Q_SCRIPTABLE void nextWallpaper(const QString &screenName);
    Q_SCRIPTABLE void nextWallpaperAll();

    // Set a concrete image as the wallpaper of one monitor for the current
    // desktop (the kdock-setwallpaper binary calls this). Empty screenName =
    // the primary monitor. Only meaningful while kdock is the one drawing the
    // wallpapers (LXQt): returns false otherwise, so the caller can fall back
    // to driving Plasma itself.
    Q_SCRIPTABLE bool setWallpaper(const QString &screenName, const QString &path);

    // Three parallel lists rather than one a(ssb): plain `as` needs no
    // qDBusRegisterMetaType on either side (see CLAUDE.md on struct properties).
    Q_SCRIPTABLE QStringList dockIds();
    Q_SCRIPTABLE QStringList dockScreens();
    Q_SCRIPTABLE QString primaryDockId();

signals:
    Q_SCRIPTABLE void darkModeChanged(bool on);
    // ColorAuto's switch moved. Not only from setColorAutoEnabled(): the
    // settings tab writes it too, and **dark mode switches it off by itself**
    // (it owns the desktop's appearance while it is on) and back afterwards. A
    // panel that only echoed its own presses would go stale on all three.
    Q_SCRIPTABLE void colorAutoChanged(bool enabled);

private:
    DockManager *m_manager;
    // Last value announced, so the notifier does not re-emit on every repaint.
    bool m_lastDarkMode = false;
    bool m_lastColorAuto = false;
};
