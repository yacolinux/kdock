// Move the active window to the next monitor via KWin's global shortcut.
// KWin-only (Plasma). "available" reflects only whether we're on KWin, so the
// widget shows whenever the user enables it (like overview / move-to-desktop).

#pragma once

#include <QObject>

class MonitorControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    explicit MonitorControl(QObject *parent = nullptr);

    bool available() const { return m_available; }

    Q_INVOKABLE void moveToNextScreen();

signals:
    void availableChanged();

private:
    bool m_isKde = false;
    bool m_available = false;
};
