// Firing a KWin global shortcut over D-Bus, which is how the KWin-backed widgets
// (overview, move-to-desktop, move-to-monitor, maxmin) act on the active window
// instead of reimplementing compositor protocols. The plumbing was copied
// verbatim into every one of those classes; new ones use this.

#pragma once

#include <QString>

namespace KWinShortcut {

// KWin is on the session bus. Used to be `sessionIsKde()`, which asked the
// wrong question: this session is LXQt *with* kwin_wayland, so every one of
// these widgets was disabled while KWin sat there answering. See Session.
bool available();

// org.kde.kglobalaccel /component/kwin invokeShortcut <name>. Fire and forget:
// an unknown name is a no-op on KWin's side, so a missing shortcut degrades to
// "the button does nothing" rather than to an error dialog.
void invoke(const QString &name);

} // namespace KWinShortcut
