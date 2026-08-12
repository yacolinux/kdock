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
#include <QObject>
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

    // True while a generated scheme is on the desktop. Persisted: it describes
    // the system, which outlives this process.
    static bool applied();

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

    // Re-read the wallpapers and re-apply. Debounced, so the four triggers can
    // all call it freely.
    void refresh();
    // Same, without waiting for the debounce (the tab's "Aplicar ahora").
    void refreshNow();

signals:
    void changed();

private:
    void onDarkModePing();
    // Ask Plasma for every connected containment's current wallpaper image,
    // keyed by connector name, and continue in applyPalettes().
    void readWallpapers();
    void applyPalettes(const QHash<QString, QString> &imageByScreen);
    // Push the dock colors of one screen's palette onto every dock there.
    void applyToDocks(const QString &screen, const SchemeColors &scheme);
    void applySystem(const SchemeColors &scheme);
    void restoreDefaults();
    void clearDockColors();
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
    // Last dark-mode state acted on, so a repeated ping is a no-op (the accent
    // color emits the same signal).
    bool m_lastDark = false;
    bool m_reading = false;
};
