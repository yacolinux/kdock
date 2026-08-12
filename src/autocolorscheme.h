// ColorAuto: keeps the desktop's color scheme following the wallpaper.
//
// The engine that turns an image into a KDE scheme is WallpaperColors, which is
// pure and knows nothing about this desktop. This class is everything else: the
// settings (app-wide, group [ColorAuto] of the shared kdock.conf — the feature
// is configured once for every dock, never per dock), reading the current
// wallpaper off Plasma, applying the result, and standing down while dark mode
// is on.
//
// Applying is where the one non-obvious constraint of the whole feature lives:
// **plasma-apply-colorscheme does nothing when the requested name is already the
// current one** (kcms/colors/plasma-apply-colorscheme.cpp: it prints "already
// set" and returns). So rewriting one .colors file in place and re-applying it
// is a silent no-op, and the scheme has to alternate between two names. That is
// what kde-material-you-colors does too, and its known cost is two leftover
// schemes in the System Settings list; here the one that is no longer current is
// deleted a few seconds later, so only one is ever visible.
//
// Nothing about the *system* side can be inferred from this process's memory: a
// scheme applied by a previous run outlives it. Hence the persisted `applied`
// flag, the same bookkeeping DockConfig::darkAppearanceApplied() does for dark
// mode — without it, a crash with our scheme up would never be undone.

#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "wallpapercolors.h"

class AppearanceControl;
class DockManager;
class Theme;
class VirtualDesktops;

class AutoColorScheme : public QObject
{
    Q_OBJECT

public:
    // Base names of the two generated schemes. Two, and not one, because of the
    // "already set" early return described above.
    static const QString kSchemeIdA;
    static const QString kSchemeIdB;

    AutoColorScheme(Theme *theme, AppearanceControl *appearance, DockManager *manager,
                    VirtualDesktops *desktops, QObject *parent = nullptr);

    // Injected after construction, because the two need each other: this object
    // has to exist *before* DockManager (its docks get it as a QML context
    // property the moment they are built, and the first ones are built inside
    // the manager's own constructor), while it needs the manager to reach those
    // docks. Everything here tolerates a null manager, so the gap is safe.
    void setManager(DockManager *manager) { m_manager = manager; }

    // ---- persisted settings (shared kdock.conf, group [ColorAuto]) ---------
    static bool enabled();
    static bool colorDocks();
    static void setColorDocks(bool on);
    static bool systemScheme();
    static void setSystemScheme(bool on);
    // Which monitor's wallpaper drives the *system* scheme. Empty = follow the
    // brightness widget's own target; "internal" = the built-in panel; anything
    // else is a connector name (eDP-1, DP-1…).
    static QString systemMonitor();
    static void setSystemMonitor(const QString &value);
    static const QString InternalMonitor;

    static int lightness();                    // WallpaperColors::Options::Lightness
    static void setLightness(int mode);
    static int selectionMode();                // WallpaperColors::Options::SelectionMode
    static void setSelectionMode(int mode);
    static QColor selectionLight();            // used when the scheme comes out LIGHT
    static void setSelectionLight(const QColor &c);
    static QColor selectionDark();             // used when the scheme comes out DARK
    static void setSelectionDark(const QColor &c);

    // Icon set per resulting scheme kind. Disabled = keep the saved default.
    static bool iconsetEnabled(bool dark);
    static void setIconsetEnabled(bool dark, bool on);
    static QString iconsetValue(bool dark);
    static void setIconsetValue(bool dark, const QString &id);

    // ---- saved defaults ----------------------------------------------------
    // Captured the first time the feature is switched on, restored when it is
    // switched off. The icon theme is kdock's own override if there is one,
    // otherwise KDE's — it is a single process-wide value (Theme::setIconTheme),
    // so there is nothing per dock to choose between.
    static bool defaultsSaved();
    static QString defaultColorScheme();
    static QString defaultIconTheme();
    // Re-capture from the live system (the tab's "Volver a capturar" button).
    void captureDefaults();

    // **"The system's color scheme is ours"**, not merely "something was
    // applied". Persisted, because it describes the system, which outlives this
    // process. It is what gates the restore: once the user saves a scheme of
    // their own (saveCurrentScheme) this goes false and nothing here overwrites
    // it any more.
    static bool applied();

    // True while the scheme on the desktop came from a manual "Generate color"
    // rather than from the automatic path. It is what stops the startup cleanup
    // from undoing an explicit choice of the user's on every dock restart.
    static bool manual();

    // True for either generated scheme id. DarkModeAppearance asks before
    // snapshotting "what the user had", or it would store one of ours and
    // restore it forever afterwards.
    static bool isOwnSchemeId(const QString &id);
    // What dark mode has to treat as the user's color scheme while ColorAuto
    // owns the system: the saved default, never the live (generated) one.
    static QString userColorScheme();

    // Turn the feature on or off. Enabling captures the defaults on first use
    // and applies straight away; disabling restores the defaults, clears the
    // docks and removes the generated files.
    void setEnabled(bool on);

    // Re-read the wallpapers and re-apply. Debounced, so every trigger can call
    // it freely. No-op while the feature is off.
    void refresh();
    // Same, without waiting for the debounce (the tab's "Aplicar ahora").
    void refreshNow();

    // "Generate color": re-read the wallpapers and apply the **next** candidate
    // color of the same image. Deliberately NOT gated on enabled() — this is the
    // dock widget, the panel card and the tab button, and they are meant to work
    // with the feature switched off, leaving the result for the user to manage.
    // Captures the defaults on first use so there is always a way back.
    void generateNow();

    // Write the scheme currently generated to a permanent
    // ~/.local/share/color-schemes/kdock-<n>.colors, apply that copy, and hand
    // ownership of the system scheme back to the user (applied() goes false, so
    // nothing here overwrites it afterwards). Returns the id, or empty on
    // failure. Generates first when nothing has been generated yet.
    QString saveCurrentScheme();

signals:
    void changed();

private:
    void onDarkModePing();
    // Ask Plasma for every connected containment's current wallpaper image,
    // keyed by connector name, and continue in applyPalettes().
    void readWallpapers();
    void applyPalettes(const QHash<QString, QString> &imageByScreen);
    // (Re)arm the watch on Plasma's applet config. Must run again after every
    // signal: KConfig saves by writing a temp file and renaming it over the
    // original, so the inode being watched is gone and QFileSystemWatcher drops
    // the watch *silently* — without this the whole thing fires exactly once,
    // which is the very bug this watcher exists to fix.
    void armWallpaperWatch();
    // Push the dock colors of one screen's palette onto every dock there.
    void applyToDocks(const QString &screen, const SchemeColors &scheme);
    // Drop the ColorAuto colours of every dock except the ones listed. The
    // exception set is how a refresh keeps the docks it just coloured while
    // still clearing the ones whose screen it could not resolve.
    void applySystem(const SchemeColors &scheme);
    void restoreDefaults();
    void clearDockColors(const QSet<QString> &except = {});
    void removeGeneratedFiles();
    // Connector name of the monitor the system scheme follows.
    QString systemScreenName() const;
    static QString schemeFilePath(const QString &id);
    static WallpaperColors::Options options();

    Theme *m_theme = nullptr;
    AppearanceControl *m_appearance = nullptr;
    DockManager *m_manager = nullptr;
    VirtualDesktops *m_desktops = nullptr;

    // Coalesces the four triggers, and — more importantly — waits out the
    // asynchronous half of a wallpaper change: kdock only asks Plasma to cycle
    // the plugin, KDE picks the next image itself, so reading immediately gets
    // the *previous* path back.
    QTimer m_debounce;
    // Watches Plasma's applet config, which is where a wallpaper change lands
    // whoever made it — including the D-Bus scripts that never go through
    // kdock at all, which is the common case and what the four in-process
    // triggers could never see.
    QFileSystemWatcher m_wallpaperWatch;
    // Last dark-mode state acted on, so a repeated ping is a no-op (the accent
    // color emits the same signal).
    bool m_lastDark = false;
    bool m_reading = false;
    // Set while the round trip in flight was started by generateNow(): it is
    // what tells applyPalettes() to take ownership without turning the feature
    // on, and not to reset the variant counter.
    bool m_manualRun = false;
    // The scheme last handed to the system, so "save" can write it out again
    // without regenerating (and therefore without re-reading the wallpaper).
    // Not persisted: a fresh process has nothing to save until it generates.
    SchemeColors m_lastScheme;
    bool m_haveScheme = false;
};
