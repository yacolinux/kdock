// "Modo QT": makes the appearance kdock already drives — color scheme, icon set
// and now the application font — reach the rest of the Qt applications of an
// LXQt session.
//
// The problem it solves: every color-scheme and icon-theme selector in kdock
// ends up writing kdeglobals (plasma-apply-colorscheme / plasma-changeicons),
// and kdock itself reads that file by hand (see Theme). Under Plasma that was
// enough — plasma-integration builds every application's QPalette and icon
// theme from kdeglobals. Under LXQt the platform theme is "lxqt", which never
// looks at kdeglobals, so the desktop's applications stayed on whatever
// lxqt-config-appearance had left.
//
// The bridge is the LXQt platform theme's own settings. Reading its source
// (lxqt-qtplugin/src/lxqtplatformtheme.cpp) it:
//
//   - reads ten keys of the [Palette] group of ~/.config/lxqt/lxqt.conf, plus
//     `icon_theme` at the top level and `font` / `fixedFont` in [Qt];
//   - watches that file and, on a change, calls QApplication::setPalette() +
//     style()->polish(), XdgIconLoader::updateSystemTheme() and
//     QApplication::setFont() for whichever of the three moved. So writing those
//     keys re-themes every running Qt application of the session, live: no
//     environment variable, no relaunch.
//
// Three consequences worth knowing:
//
//   - **kdeglobals is the source of truth for colors and icons, not this
//     class.** Neither has a value of its own here: they translate whatever
//     kdeglobals currently says. That is what makes every *other* selector of
//     kdock (the dock's own pickers, the Colores tab, ColorAuto, dark mode)
//     reach LXQt without a line of extra code — they all already write
//     kdeglobals, and Theme already watches it.
//   - **The font is the exception, and it has to be.** Nothing in kdock sets an
//     application font, so there is nothing to mirror: it is a setting of this
//     class, stored in the shared kdock.conf. Empty means "leave whatever LXQt
//     has alone", which is why it starts empty rather than at some default.
//     kdock gets it for free like any other Qt application — its own QML text
//     uses the application font, and the platform theme in *this* process reacts
//     to the same file.
//   - **An identical write is a no-op.** The plugin only reacts when a value
//     actually differs from the one it read last (its paletteChanged_ flag and
//     the old/new comparisons around it), the same "already set" early return
//     plasma-apply-colorscheme has. Rewriting the same values would re-theme
//     nothing, so this class does not bother writing them.
//
// Off by default, and inert while off: this writes to the user's live session
// and no sandbox (XDG_DATA_HOME) isolates lxqt.conf, so a test harness must not
// be able to re-theme anybody's desktop just by constructing it.

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

class Theme;

class QtCompat : public QObject
{
    Q_OBJECT

public:
    explicit QtCompat(Theme *theme = nullptr, QObject *parent = nullptr);

    // ---- persisted settings (shared kdock.conf, group [QtCompat]) ----------
    // App-wide, never per dock: there is one session appearance.
    //
    // The master switch plus one per part. Turning the master on applies
    // straight away. Turning it off only stops applying: what was already
    // written stays, for the user to manage with lxqt-config-appearance
    // (deliberate — restoring would fight with whatever they did in the
    // meantime).
    static bool enabled();
    void setEnabled(bool on);

    // Colors and icons default to on: with the master switch on, they are what
    // "Modo QT" means. The font defaults to off because it needs a value first
    // (see fontFor()).
    static bool colorsEnabled();
    void setColorsEnabled(bool on);
    static bool iconsEnabled();
    void setIconsEnabled(bool on);
    static bool fontsEnabled();
    void setFontsEnabled(bool on);

    // The two fonts, in QFont::toString() form — the same encoding lxqt.conf
    // uses, so it travels verbatim. Empty = not set.
    enum FontKind { GeneralFont, FixedFont };
    static QString font(FontKind kind);
    void setFont(FontKind kind, const QString &value);
    // The font to *show* in the tab: the stored one, or, when nothing is stored
    // yet, whatever LXQt currently has. Without this the picker would open on
    // the Qt default and the first "OK" would silently change the desktop's font
    // to something the user never chose.
    static QString fontFor(FontKind kind);

    // Translate now and write, ignoring the debounce. No-op while off.
    void applyNow();

    // The ten color values that would be written, as { key, color } maps in the
    // order the tab shows them. Reads kdeglobals; does not write anything, so
    // the tab can show the translation with the feature switched off.
    QVariantList translation() const;
    // The icon theme that would be written (kdeglobals Icons/Theme), for the
    // same reason. Empty when kdeglobals has none.
    QString iconTheme() const;

    // Absolute path of the file the LXQt platform theme watches, for the tab's
    // diagnostics line. Built the same way the plugin builds it.
    static QString lxqtConfPath();
    // Whether this process is running under the LXQt platform theme. When it is
    // not, the write still happens (it is the session's file, not ours) but
    // nothing re-themes, which is worth saying out loud in the tab.
    static bool lxqtPlatformTheme();

signals:
    void changed();

private:
    // One value bound for lxqt.conf. `key` is already in QSettings form, i.e.
    // "Palette/window_color", "Qt/font", or a bare "icon_theme" — QSettings maps
    // the top level onto the file's [General] section, which is exactly where
    // the plugin reads that one from.
    struct Pending
    {
        QString key;
        QString value;
    };
    // Everything the current settings say should be in lxqt.conf. Empty when
    // there is nothing to write (every part off, or kdeglobals unreadable).
    QList<Pending> pendingWrites() const;
    // key -> color, in the order of the [Palette] group. Empty when kdeglobals
    // cannot be read at all.
    QList<QPair<QString, QString>> buildPalette() const;
    void apply();

    Theme *m_theme = nullptr;
    // Coalesces the burst of Theme::changed a scheme change produces (dark mode
    // and ColorAuto write kdeglobals more than once per transition).
    QTimer m_debounce;
};
