// Session/power actions, with one backend per session type.
//
//   - **KDE**: org.kde.LogoutPrompt for logout/reboot/shutdown (which is what
//     puts KDE's confirmation dialog on screen), plain freedesktop D-Bus for
//     lock and suspend.
//   - **LXQt**: the session's own .desktop files —
//     lxqt-{logout,reboot,shutdown,suspend,lockscreen}.desktop, all of them
//     `Exec=lxqt-leave --<action>` — launched through DesktopEntryIndex. Going
//     through the desktop entries rather than hardcoding the command keeps this
//     working on an LXQt that installs its tools elsewhere, and lxqt-leave shows
//     its own confirmation dialog, so the two backends behave alike: a stray
//     click on the dock does not power the machine off.
//
// `available` is what the four places that draw session buttons (the dock's
// Session widget, the app menu footer, the tile menu footer and the control
// panel's System card) gate on. It used to be "the session is KDE", which under
// LXQt hid all four.

#pragma once

#include <QObject>
#include <QString>

class PowerControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit PowerControl(QObject *parent = nullptr);

    bool available() const { return m_backend != None; }

    Q_INVOKABLE void logout();
    Q_INVOKABLE void reboot();
    Q_INVOKABLE void shutdown();
    Q_INVOKABLE void lock();
    Q_INVOKABLE void suspend();

    // The five actions, in the form the QML uses to call them. Exposed for the
    // probes: an arnés must be able to ask "what would this run?" without
    // running it, because running it logs the user out.
    enum Action { Logout, Reboot, Shutdown, Suspend, Lock };
    // What the LXQt backend would launch for `action`: the resolved .desktop
    // path, or the fallback command line. Empty when this is not the LXQt
    // backend or nothing could be resolved.
    QString resolvedCommand(Action action) const;

private:
    enum Backend { None, Kde, Lxqt };

    // Absolute path of the .desktop LXQt ships for `action`, or empty.
    static QString desktopFileFor(Action action);
    // Launch it (or fall back to `lxqt-leave --<action>`).
    static void runLxqt(Action action);

    Backend m_backend = None;
};
