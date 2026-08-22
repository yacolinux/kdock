// Wallpapers per monitor and per virtual desktop, under LXQt.
//
// The Plasma counterpart of this is DesktopWallpapers, and the two look nothing
// alike on purpose, because the constraint is the opposite one:
//
//   - Under Plasma, wallpapers belong to plasmashell's containments. kdock can
//     only *rewrite somebody else's configuration*, so DesktopWallpapers is all
//     snapshot, apply and restore: desktop 1 belongs to KDE, is captured before
//     anything is touched and put back on the way out.
//   - Under LXQt the desktop is PCManFM-Qt, which cannot do this at all: its
//     own source carries `// FIXME: support different wallpapers on different
//     screen` (pcmanfm/application.cpp) and forces per-screen wallpapers off
//     under Wayland, and it has no notion of virtual desktops either. So kdock
//     **draws the wallpapers itself**, one layer-shell surface per monitor (see
//     WallpaperWindow), and there is nobody's configuration to preserve:
//     no snapshot, no capture, no restore. Every desktop 1..kMaxDesktops is
//     configurable, symmetrically.
//
// The settings are the same ones the Wallpapers tab already writes
// (DesktopWallpapers' static accessors), so a session that moves between the
// two desktops keeps its wallpapers.
//
// **PCManFM's desktop is switched off while this is running.** Two surfaces on
// the background layer means the later one wins, and that is always kdock (the
// desktop starts with the session). Rather than covering PCManFM's icons and
// leaving them there, invisible but alive, the engine asks it to stand down —
// and puts it back when it stops, on quit, and on a signal. That last part is
// not optional: a logout that left the user with no desktop icons and no
// desktop menu would not repair itself.
//
// Off by default like DesktopWallpapers (the same `[Wallpapers] enabled` key),
// so a probe with a throwaway XDG_DATA_HOME is inert even though it does reach
// the real session bus.

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

class VirtualDesktops;
class WallpaperWindow;

class LxqtWallpapers : public QObject
{
    Q_OBJECT

public:
    explicit LxqtWallpapers(VirtualDesktops *desktops, QObject *parent = nullptr);
    ~LxqtWallpapers() override;

    // The shared master switch, i.e. DesktopWallpapers::enabled(). One key for
    // the feature, whichever engine implements it.
    static bool enabled();
    // This engine is the one drawing right now. NOT the same as enabled(): the
    // key is set under Plasma too, where the *other* engine runs, and anything
    // that would take a wallpaper action over (the "next wallpaper" widget)
    // has to ask this one or it would silently do nothing there.
    bool active() const { return m_active; }

    // Called once from main() after the docks exist. While the feature is off
    // this only undoes a suppression left behind by a previous run.
    void start();

    // Put the current desktop's wallpapers up. Monitors with nothing configured
    // are left alone (their surface hides, so the desktop shows through).
    //
    // `advance` picks a **new** image for every monitor in slideshow mode
    // instead of re-showing the one that desktop had. That is what a desktop
    // switch does: without it each (desktop, monitor) keeps its first pick for
    // the whole session and switching back and forth shows the same two
    // pictures over and over — which is exactly how it behaved until
    // 2026-08-20, and read as "the slideshow is stuck".
    void apply(int desktop, bool advance = false);

    // Step the slideshow of one monitor (empty name = every monitor) right now,
    // which is what the "next wallpaper" widget does under LXQt. Only monitors
    // whose desktop is in slideshow mode move.
    void advance(const QString &screenName = QString());

    // Take everything down and give the desktop back to PCManFM. Idempotent.
    void stop();

    // connector name -> the image each monitor is showing **right now**.
    //
    // This is ColorAuto's wallpaper source under LXQt, in place of the
    // plasmashell round trip it uses under Plasma (see
    // AutoColorScheme::setWallpaperSource). It reads the live surfaces rather
    // than recomputing from the config on purpose: in slideshow mode the choice
    // is random and lives only in m_slideshowHistory, so asking imageFor()
    // again could answer with a different picture than the one on screen.
    //
    // Empty while this engine is not the one drawing. That is a normal state,
    // not an error: with the feature off PCManFM owns the desktop, and its
    // wallpaper is deliberately NOT used as a fallback — it has one image for
    // the whole session, so per-monitor colors would be a fiction (decision
    // 2026-08-22). The ColorAuto tab says so instead.
    QHash<QString, QString> currentImages() const;

    // Which picture a monitor would get on a desktop. Public only so a test can
    // ask without a compositor — the choice is the half of this class that can
    // be checked without one (tests/unit/tst_lxqtwallpapers.cpp).
    QString imageForTesting(int desktop, const QString &screen) { return imageFor(desktop, screen); }
    QString advanceForTesting(int desktop, const QString &screen) { return advanceFor(desktop, screen); }

public slots:
    // aboutToQuit / the quit signal handler: same as stop(), and it must run on
    // both paths or a logout leaves the session without a desktop manager.
    void quit();

signals:
    // Same signal (and same meaning) as DesktopWallpapers::wallpapersApplied,
    // so whoever listens does not care which engine is running.
    void wallpapersApplied(int desktop);

    // The engine started or stopped drawing. WallpaperControl listens so its
    // `available` property (and therefore the wallpaper widgets) follows —
    // this engine starts after the docks exist, so nothing would ever see it
    // otherwise.
    void activeChanged();

private:
    void onDesktopChanged(int position);
    void onScreensChanged();
    // Create/destroy surfaces so there is exactly one per connected monitor.
    void syncWindows();
    // The image a monitor should show on `desktop`: the static one, or the
    // current step of its slideshow, or — when kdock has nothing configured
    // there — PCManFM's own wallpaper.
    //
    // That last fallback is not a nicety. PCManFM's desktop is switched off
    // while this engine runs, so "nothing configured" would otherwise mean a
    // black screen: and a config carried over from Plasma has exactly that on
    // desktop 1, which under Plasma belonged to KDE and was never given a key
    // here. With the fallback, a desktop kdock knows nothing about keeps
    // looking the way it did.
    QString imageFor(int desktop, const QString &screen);
    // Pick a new slideshow image for (desktop, screen) and remember it. Returns
    // empty when that pair is not in slideshow mode or has no folder — the
    // caller then leaves whatever is up alone.
    QString advanceFor(int desktop, const QString &screen);
    // PCManFM-Qt's configured wallpaper (its [Desktop] Wallpaper key), or empty.
    static QString pcmanfmWallpaper();
    // Re-arm the slideshow timer for `desktop` (stopped when that desktop is
    // not in slideshow mode).
    void updateTimer(int desktop);

    // Ask PCManFM-Qt to drop (or take back) the desktop. Turning it back on
    // may have to relaunch it: it runs without --daemon-mode, so deleting its
    // last window can make it exit, taking its bus name with it — measured,
    // 2026-08-20, and the reason the "on" path is more than a D-Bus call.
    static void setPcmanfmDesktop(bool on);

    // Persisted "we took the desktop away from PCManFM". quit() clears it, but
    // quit() does not run on a SIGKILL or a crash, and the user would be left
    // with no desktop icons and no desktop menu until they noticed and fixed it
    // by hand. So the fact is written down, and the next start puts the desktop
    // back if the engine is no longer the one holding it.
    static bool desktopSuppressed();
    static void setDesktopSuppressed(bool on);

    // How many past images a (desktop, monitor) remembers, and therefore will
    // not pick again. Three: the one on screen plus the two before it, which is
    // as far back as the eye notices a repeat.
    static constexpr int kHistory = 3;

    // How long a desktop has to stay current before its wallpaper steps.
    //
    // A desktop switch is a slideshow step, but *navigating* is not: clicking
    // through the thumbnails in KWin's Overview fires one currentChanged per
    // click, and without this each of them would pick a new picture, animate a
    // crossfade under the effect and burn that desktop's history for a desktop
    // the user only passed through. Short enough to feel immediate when the
    // switch is real.
    static constexpr int kAdvanceDelayMs = 700;

    VirtualDesktops *m_desktops = nullptr;
    QHash<QString, WallpaperWindow *> m_windows;
    // The last kHistory images of each (desktop, monitor), oldest first; the
    // last one is what is on screen. Per pair and not per monitor so that two
    // desktops sharing a folder do not fight over one history.
    QHash<QString, QStringList> m_slideshowHistory;
    QTimer m_slideshow;
    // Restarted on every desktop change; only its timeout advances. See
    // kAdvanceDelayMs.
    QTimer m_advanceDebounce;
    int m_currentDesktop = 1;
    bool m_active = false;
    // We are the ones holding PCManFM's desktop down. Separate from m_active
    // because the engine can be running with nothing configured, and in that
    // case it must leave the desktop alone (see apply()).
    bool m_suppressed = false;
};
