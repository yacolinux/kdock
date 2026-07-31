// Default-sink volume via WirePlumber's CLI (wpctl), the stock PipeWire
// session manager on Plasma 6 — no libpulse/KDE linkage, just QProcess.
// Change notifications come from a long-running `pactl subscribe`; if pactl
// is missing we fall back to polling.

#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>

class VolumeControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(qreal volume READ volume NOTIFY changed)
    Q_PROPERTY(bool muted READ muted NOTIFY changed)
    Q_PROPERTY(QString iconName READ iconName NOTIFY changed)

public:
    explicit VolumeControl(QObject *parent = nullptr);

    bool available() const { return m_available; }
    qreal volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    QString iconName() const;

    Q_INVOKABLE void setVolume(qreal volume);
    Q_INVOKABLE void toggleMute();
    // Re-read the real default-sink state (async). Exposed so the UI can pull a
    // fresh state on demand (e.g. on hover) and never act on a stale cache.
    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    void scheduleRefresh();
    void startSubscriber();

    QString m_wpctl;
    bool m_available = false;
    qreal m_volume = 0.0;
    bool m_muted = false;
    QProcess m_subscriber;
    QTimer m_refreshDebounce;
};
