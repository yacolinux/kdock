// Move the active window to the next (left click) or previous (right click)
// monitor via KWin's global shortcuts.
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
    Q_INVOKABLE void moveToPreviousScreen();

signals:
    void availableChanged();

private:
    bool m_available = false;
};
