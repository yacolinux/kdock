// Move the active window to the next virtual desktop via KWin's global
// shortcut. KWin-only (Plasma); unavailable on wlroots compositors.

#pragma once

#include <QObject>

class DesktopControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit DesktopControl(QObject *parent = nullptr);

    bool available() const { return m_available; }

    Q_INVOKABLE void moveToNextDesktop();

private:
    void checkAvailability();

    bool m_available = false;
};
