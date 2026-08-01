// Maximize (left click) / minimize (right click) the active window, via KWin's
// global shortcuts — the same mechanism as the overview / move-to-desktop /
// move-to-monitor widgets, so it is KWin-only.
//
// "Window Maximize" is a *toggle* on KWin's side: clicking it on an already
// maximized window restores it. There is no state to track here, which is why
// the widget's icon is static (the dock's WindowMonitor reports `activated` and
// `minimized`, but not `maximized`).

#pragma once

#include <QObject>

class MaxMinControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    explicit MaxMinControl(QObject *parent = nullptr);

    bool available() const { return m_available; }

    Q_INVOKABLE void maximize();
    Q_INVOKABLE void minimize();

signals:
    void availableChanged();

private:
    bool m_available = false;
};
