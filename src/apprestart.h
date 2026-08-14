// "Reiniciar" for the whole kdock family, not just the dock process.
//
// The three resident accessories (kdock-previews, kdock-tilemenu,
// kdock-controlmanager) are separate processes that outlive a kdock restart on
// purpose — that is what makes them instant on the next click. The price is
// that restarting the dock alone leaves them running the binary that was on
// disk when THEY started: right after an install, the old one. The fix the user
// just installed is simply not there, and the process looks up to date from the
// outside (2026-08-10: a corner-resize fix was tested against a control panel
// whose /proc/<pid>/exe already read "(deleted)").
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
