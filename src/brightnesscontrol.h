// Screen brightness via brightnessctl (no extra library linkage).

#pragma once

#include <QObject>
#include <QTimer>

class BrightnessControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(qreal brightness READ brightness NOTIFY changed)

public:
    // Never let the screen go fully dark; keep at least this fraction of brightness.
    static constexpr qreal MinBrightness = 0.05;

    explicit BrightnessControl(QObject *parent = nullptr);

    bool available() const { return m_available; }
    qreal brightness() const { return m_brightness; }

    Q_INVOKABLE void setBrightness(qreal brightness);
    Q_INVOKABLE void increase() { setBrightness(m_brightness + 0.05); }
    Q_INVOKABLE void decrease() { setBrightness(m_brightness - 0.05); }

signals:
    void changed();

private slots:
    // logind org.freedesktop.login1.Manager.PrepareForSleep(bool)
    void onPrepareForSleep(bool sleeping);

private:
    void refresh();
    void scheduleRefresh();
    void persist();

    QString m_brightnessctl;
    bool m_available = false;
    qreal m_brightness = 1.0;
    // Brightness to re-apply on resume/startup (firmware often resets it).
    qreal m_restoreBrightness = -1.0;
    QTimer m_refreshDebounce;
};
