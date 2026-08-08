// Hardening for the few QProcess children that outlive the call that starts
// them (the two `pactl subscribe` subscribers). Both fixes are about what
// happens to the child when the dock is gone:
//
//  - PR_SET_PDEATHSIG: the kernel signals the child as soon as we die, however
//    we die. The dock's SIGTERM path exits with ::_exit(0) on purpose (see
//    main.cpp) and that never runs QProcess's destructor, so without this every
//    logout/kill left two `pactl subscribe` behind, reparented to systemd and
//    running forever. They accumulated across restarts (44 of them, reported
//    2026-08-08).
//  - SIGPIPE back to its default: the dock ignores it process-wide for the
//    clipboard pipes (waylandclipboard.cpp) and signal dispositions survive
//    exec, so every child inherits that. It buys nothing for pactl, which
//    ignores SIGPIPE on its own anyway (measured), but it keeps children from
//    inheriting a disposition that is ours and not theirs.

#pragma once

#include <QProcess>

#include <csignal>

#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif

namespace kdock {

inline void tieToParent(QProcess &process)
{
#ifdef Q_OS_LINUX
    process.setChildProcessModifier([] {
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
        ::signal(SIGPIPE, SIG_DFL);
    });
#else
    Q_UNUSED(process);
#endif
}

} // namespace kdock
