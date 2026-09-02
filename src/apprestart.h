// "Reiniciar" for the whole kdock family, not just the dock process.
//
// The resident accessories (kdock-previews, kdock-tilemenu, kdock-controlmanager,
// kdock-desktop, kdock-clipboard and kdock-systray) are separate processes.
// They are deliberately resident while kdock runs, but they must not survive a
// whole-dock exit or restart: otherwise a later launch reuses their old D-Bus
// services and keeps executing the binary they started from.
//
// So "Dock → Reiniciar" goes through here: quit the accessories, wait for their
// bus names to be released, and only then relaunch this process.
//
// kdock-calendar is deliberately not in the list: it is a one-shot window with
// no D-Bus service and no state, so the next launch already runs the new
// binary. There is nothing to restart, only a window to close.

#pragma once

#include <QStringList>

namespace kdock {

// Stop every resident accessory without changing its persisted switches. This
// is the single shutdown path for both “Salir” and “Reiniciar”: request the
// normal D-Bus quit first, then terminate only the PID that owns one of our
// still-registered services if it got wedged. Safe to call more than once.
void stopAccessories();

// Relaunches this process with its own arguments after stopping every resident
// accessory. Does not return before quitting the application.
//
// `extraArgs` is appended to the inherited command line — that is how the
// Presets tab hands the preset to apply to the *next* process (--apply-preset
// <zip>) instead of writing the .conf files under the feet of the ones this
// process still has open. Any --apply-preset already in the inherited arguments
// is dropped: it belongs to the launch that consumed it, and carrying it along
// would re-apply that preset on every later "Reiniciar", silently throwing away
// whatever the user changed since.
void restartAll(const QStringList &extraArgs = {});

// Name of that flag, so main.cpp and the caller cannot drift apart.
inline const char *kApplyPresetFlag = "--apply-preset";

} // namespace kdock
