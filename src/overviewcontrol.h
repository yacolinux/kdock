// KWin Overview effect control via D-Bus.

#pragma once

#include <QObject>

class OverviewControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit OverviewControl(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool active() const { return m_active; }

    Q_INVOKABLE void toggle();

signals:
    void activeChanged();

private:
    void checkAvailability();

    bool m_available = false;
    bool m_active = false;
};
