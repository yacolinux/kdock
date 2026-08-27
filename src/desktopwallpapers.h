// Wallpapers per virtual desktop.
//
// Plasma has no such thing: a desktop *containment* is per screen, not per
// virtual desktop, so every desktop shares one wallpaper. The only way to get a
// different one per desktop is to rewrite the containment's wallpaper at the
// moment the user switches — which is what this class does, hanging off
// VirtualDesktops::currentChanged.
//
// The split is deliberate and asymmetric:
//   - **Desktop 1 belongs to KDE.** Whatever the user configured there
//     (typically a slideshow per monitor) is snapshotted before we touch
//     anything and restored every time they come back to it — and on quit. Its
//     config is never edited from kdock.
//   - **Desktops 2..kMaxDesktops** have a *mode* each, picked in the Wallpapers
//     tab:
//       - **Static** (the original behaviour): an image per monitor, keyed by
//         connector name (eDP-1, DP-5, …), applied as org.kde.image. A monitor
//         with no image configured is left alone.
//       - **Slideshow**: each monitor with a folder configured runs KDE's own
//         org.kde.slideshow plugin rooted at that folder, advancing every
//         `slideshowInterval(desktop)` seconds. Monitors with no folder are
//         left alone.
//     A desktop is in exactly one mode (`slideshowEnabled()`); the other
//     desktop's per-monitor keys are simply ignored.
//
// Everything is off by default (`enabled()` is false until the Wallpapers tab
// turns it on): a test harness with a throwaway XDG_DATA_HOME therefore cannot
// touch the real desktop, even though it does reach the real session bus.

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class VirtualDesktops;

// One monitor's wallpaper configuration, as read off its containment: the
// plugin id plus every key of its ["Wallpaper", <plugin>, "General"] group.
// Keys are enumerated with Plasma's `configKeys`, so this is plugin-agnostic —
// slideshow, plain image or anything else round-trips unchanged.
struct WallpaperSnapshot
{
    QString plugin;
    QHash<QString, QString> keys;
};

class DesktopWallpapers : public QObject
{
    Q_OBJECT

public:
    // At most this many monitors get a row in the Wallpapers tab. A product
    // decision, like DockConfig::kMaxDesktops.
    static constexpr int kMaxScreens = 5;

    // Plasma's org.kde.image FillMode, in the order the tab lists them.
    static constexpr int kFillModeDefault = 2; // scaled and cropped

    // The default slideshow interval, in seconds (5 minutes). KDE's slideshow
    // takes its interval in seconds, so the persisted key keeps seconds too.
    static constexpr int kSlideshowIntervalDefault = 300;

    explicit DesktopWallpapers(VirtualDesktops *desktops, QObject *parent = nullptr);

    // ---- persisted settings (shared kdock.conf, group [Wallpapers]) --------
    static bool enabled();
    static void setEnabled(bool on);
    static int fillMode();
    static void setFillMode(int mode);
    // Image for (1-based desktop, connector name). Empty = not configured.
    static QString imageFor(int desktop, const QString &screen);
    // An empty path removes the key.
    static void setImageFor(int desktop, const QString &screen, const QString &path);

    // ---- slideshow mode (same group [Wallpapers<desktop>]) -----------------
    // Whether `desktop` uses KDE's slideshow instead of static images. When
    // true, the static images above are ignored and each monitor runs
    // org.kde.slideshow rooted at its slideshow folder.
    static bool slideshowEnabled(int desktop);
    static void setSlideshowEnabled(int desktop, bool on);
    // Folder a monitor's slideshow is rooted at. Empty = not configured.
    static QString slideshowFolder(int desktop, const QString &screen);
    // An empty path removes the key.
    static void setSlideshowFolder(int desktop, const QString &screen, const QString &path);
    // Seconds between slideshow steps, shared by every monitor of the desktop.
    static int slideshowInterval(int desktop);
    static void setSlideshowInterval(int desktop, int seconds);

    // Monitors offered by the UI: the connected ones first, then the ones the
    // config has seen before, capped at kMaxScreens.
    static QStringList configuredScreens();
    // All remembered wallpaper folders across the virtual desktops, with the
    // folder of `currentScreen` on `desktop` first. Empty folders are omitted
    // and repeated paths appear only once. The remembered lists are append-only
    // and are refreshed from the per-monitor config on every call.
    static QStringList slideshowFolders(int desktop, const QString &currentScreen);

    // "x,y" of every connected screen, by connector name. Public because it is
    // the bridge between a Plasma containment (which only knows a screen index
    // and its geometry) and the connector names the rest of the config uses —
    // AutoColorScheme needs the same mapping and copying it would be two
    // sources of truth for one fact.
    static QHash<QString, QString> geometryKeys();

    // The stored desktop-1 config, by connector name. Survives restarts and
    // keeps entries for monitors that are currently unplugged.
    static QHash<QString, WallpaperSnapshot> snapshot();
    static void setSnapshot(const QHash<QString, WallpaperSnapshot> &snap);
    // True while a wallpaper of ours (not KDE's) is on screen.
    static bool applied();

    // Called once from main() after the docks exist: re-applies the current
    // desktop's set, or puts desktop 1 back if a previous run died with our
    // wallpaper still up.
    void start();

    // Re-read desktop 1's configuration from Plasma right now. Only meaningful
    // while the user is actually on desktop 1 (the Wallpapers tab gates the
    // button on that); `force` skips the "did we write this ourselves?" guard.
    void capture(bool force = false);
    // Put the given desktop's wallpapers up — slideshow or static, whichever
    // the desktop is in. Monitors without a folder/image are not touched.
    void apply(int desktop);
    // Put the snapshot back on every connected monitor it covers.
    void restore();

public slots:
    // aboutToQuit: same as restore(), but with blocking D-Bus calls because the
    // event loop is already on its way out.
    void quit();

signals:
    // The stored desktop-1 config changed (a capture landed), so the Wallpapers
    // tab can redraw its read-only list.
    void snapshotChanged();
    // A desktop's wallpapers were just put up. ColorAuto listens: this is one
    // of the two ways the picture changes without anyone touching a wallpaper
    // control. NOT called applied(): that name is already the static getter
    // above, and an overload set of a signal and a static method is not
    // something &DesktopWallpapers::applied can be resolved out of.
    void wallpapersApplied(int desktop);

private:
    void onDesktopChanged(int position);
    // capture() and then, once its reply has landed, apply(desktop). The two
    // must be ordered: capturing after the write would store our own image.
    void captureThenApply(int desktop);
    // Parses the reply of the read script into a snapshot keyed by connector.
    QHash<QString, WallpaperSnapshot> parseCapture(const QString &reply) const;
    // Merges into the stored snapshot (never drops an existing monitor) and
    // persists.
    void mergeSnapshot(const QHash<QString, WallpaperSnapshot> &fresh);
    // The scripts a write needs: the one that writes the config and the one
    // that reloads it. They must go out as **separate** evaluateScript calls or
    // Plasma does not repaint (see AGENTS.md).
    QString applyScript(int desktop) const;
    QString restoreScript() const;
    static QString reloadScript(const QStringList &geometryKeys);
    // Slideshow-only: switches the configured monitors to org.kde.image **without
    // reloading**. It exists to force the next apply to register as a *plugin
    // change*: re-applying org.kde.slideshow over an already-slideshow containment
    // keeps the plugin's Image key, so desktop N+1 with the same folder would
    // repeat desktop N's image. Toggling away and back makes Plasma advance the
    // slideshow (the next-wall.sh finding) — see DesktopWallpapers::apply().
    QString toggleToImageScript(int desktop) const;

    static void setApplied(bool on);

    VirtualDesktops *m_desktops = nullptr;
};
