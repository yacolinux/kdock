// Firing a KWin global shortcut over D-Bus, which is how the KDE-only widgets
// (overview, move-to-desktop, move-to-monitor, maxmin) act on the active window
// instead of reimplementing compositor protocols. The plumbing was copied
// verbatim into every one of those classes; new ones use this.

#pragma once

#include <QString>

namespace KWinShortcut {

// XDG_CURRENT_DESKTOP / XDG_SESSION_DESKTOP say KDE. Static for the process:
// nobody switches session type under a running dock.
bool sessionIsKde();

// org.kde.kglobalaccel /component/kwin invokeShortcut <name>. Fire and forget:
// an unknown name is a no-op on KWin's side, so a missing shortcut degrades to
// "the button does nothing" rather than to an error dialog.
void invoke(const QString &name);

} // namespace KWinShortcut
