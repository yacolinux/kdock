// Session/power actions via KDE D-Bus (logout/reboot/shutdown use the KDE
// confirmation prompt; lock/suspend are immediate). KWin/Plasma only.

#pragma once

#include <QObject>

class PowerControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit PowerControl(QObject *parent = nullptr);

    bool available() const { return m_available; }

    Q_INVOKABLE void logout();   // org.kde.LogoutPrompt.promptLogout
    Q_INVOKABLE void reboot();   // org.kde.LogoutPrompt.promptReboot
    Q_INVOKABLE void shutdown(); // org.kde.LogoutPrompt.promptShutDown
    Q_INVOKABLE void lock();     // org.freedesktop.ScreenSaver.Lock
    Q_INVOKABLE void suspend();  // org.freedesktop.PowerManagement.Suspend

private:
    bool m_available = false;
};
