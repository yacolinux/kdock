// What desktop session kdock is running in, and which of its services are
// actually there.
//
// Until 2026-08-20 five classes (PowerControl, DesktopControl, OverviewControl,
// WallpaperControl, KWinShortcut) each carried their own copy of the same
// three-line test on XDG_CURRENT_DESKTOP / XDG_SESSION_DESKTOP, and every one of
// them called the result "is this KDE?". Under the LXQt session that reading is
// wrong twice over:
//
//   - XDG_CURRENT_DESKTOP is "LXQt:kwin_wayland", so none of the tests matched
//     and Overview, move-to-desktop, move-to-monitor and maxmin were all dead —
//     even though **KWin and kglobalaccel are running and answering**, because
//     this session uses KWin as its window manager.
//   - The one thing that genuinely needs Plasma (WallpaperControl, which drives
//     org.kde.PlasmaShell's scripting API) was gated on the same test as the
//     rest, so it could not tell "no Plasma here" from "no KWin here".
//
// Hence the split below: `kind()` is about the *session* (who owns the
// appearance, the session dialogs, the desktop's wallpaper), while `hasKWin()`
// and `hasPlasmaShell()` ask the bus who is actually present. A feature should
// gate on whichever of the two it really needs; they are not interchangeable.

#pragma once

namespace Session {

enum Kind {
    Kde,   // Plasma / KDE
    Lxqt,  // LXQt, with any window manager
    Other, // anything else: bare wlroots, GNOME, X11 WMs…
};

// Decided once per process from the environment: nobody switches session type
// under a running dock.
//
// Test seam: KDOCK_TEST_SESSION=kde|lxqt|other overrides the detection. What
// counts is that the variable is *set* — an unrecognised value means Other, so
// a test can ask for "a session kdock knows nothing about" too.
Kind kind();

inline bool isKde() { return kind() == Kde; }
inline bool isLxqt() { return kind() == Lxqt; }

// org.kde.KWin is on the session bus. This is the right question for anything
// that goes through KWin — global shortcuts, virtual desktops, screenshots —
// and it is true in the LXQt session of this machine, where isKde() is false.
//
// Looked up once, on first use: a compositor does not come and go under a
// running dock (and if it did, kdock would be gone with it).
bool hasKWin();

// org.kde.plasmashell is on the session bus, i.e. there is a Plasma desktop
// owning the wallpapers and the containments. The only correct gate for
// anything that calls evaluateScript.
bool hasPlasmaShell();

} // namespace Session
