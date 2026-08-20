// "Modo QT": makes the KDE color scheme kdock already drives reach the rest of
// the Qt applications of an LXQt session.
//
// The problem it solves: every color-scheme selector in kdock ends up writing
// kdeglobals (plasma-apply-colorscheme), and kdock itself reads that file by
// hand (see Theme). Under Plasma that was enough — plasma-integration builds
// every application's QPalette from kdeglobals. Under LXQt the platform theme
// is "lxqt", which never looks at kdeglobals, so the desktop's applications
// stayed on whatever palette lxqt-config-appearance had left.
//
// The bridge is the LXQt platform theme's own palette. Reading its source
// (lxqt-qtplugin/src/lxqtplatformtheme.cpp) it:
//
//   - reads ten keys of the [Palette] group of ~/.config/lxqt/lxqt.conf,
//     builds a QPalette from them and overrides the roles that are valid;
//   - watches that file and, on a change, calls QApplication::setPalette() plus
//     style()->polish(). So writing those ten keys repaints every running Qt
//     application of the session, live: no environment variable, no relaunch.
//
// Two consequences worth knowing:
//
//   - **kdeglobals is the source of truth, not this class.** Nothing here has a
//     scheme of its own: it translates whatever kdeglobals currently says. That
//     is what makes every *other* selector of kdock (the dock's own picker, the
//     Colores tab, ColorAuto, dark mode) reach LXQt without a line of extra
//     code — they all already write kdeglobals, and Theme already watches it.
//   - **An identical write is a no-op.** The plugin only rebuilds the palette
//     when at least one of the ten values differs from the previous one (its
//     paletteChanged_ flag), the same "already set" early return
//     plasma-apply-colorscheme has. Rewriting the same colors would repaint
//     nothing, so this class does not bother writing them.
//
// Off by default, and inert while off: this writes to the user's live session
// and no sandbox (XDG_DATA_HOME) isolates lxqt.conf, so a test harness must not
// be able to change anybody's colors just by constructing it.

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
    // App-wide, never per dock: there is one session palette.
    static bool enabled();
    // Turning it on applies straight away. Turning it off only stops applying:
    // the palette already written stays, for the user to manage with
    // lxqt-config-appearance (deliberate — restoring would fight with whatever
    // they did in the meantime).
    void setEnabled(bool on);

    // Translate now and write, ignoring the debounce. No-op while off.
    void applyNow();

    // The ten values that would be written, as { key, color } maps in the order
    // the tab shows them. Reads kdeglobals; does not write anything, so the tab
    // can show the translation with the feature switched off.
    QVariantList translation() const;

    // Absolute path of the file the LXQt platform theme watches, for the tab's
    // diagnostics line. Built the same way the plugin builds it.
    static QString lxqtConfPath();
    // Whether this process is running under the LXQt platform theme. When it is
    // not, the write still happens (it is the session's file, not ours) but
    // nothing repaints, which is worth saying out loud in the tab.
    static bool lxqtPlatformTheme();

signals:
    void changed();

private:
    // key -> color, in the order of the [Palette] group. Empty when kdeglobals
    // cannot be read at all.
    QList<QPair<QString, QString>> buildPalette() const;
    void apply();

    Theme *m_theme = nullptr;
    // Coalesces the burst of Theme::changed a scheme change produces (dark mode
    // and ColorAuto write kdeglobals more than once per transition).
    QTimer m_debounce;
};
