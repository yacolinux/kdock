// Full audio mixer backend for the settings "Audio" tab: enumerates output
// sinks, input sources and per-application streams via PipeWire's PulseAudio
// compat CLI (pactl), and drives per-device / per-app volume, mute and
// default-device selection. pactl is invoked with LC_ALL=C so the parsed
// field labels ("Name:", "Description:", "Mute:", "Volume:") stay stable.
// Live change notifications come from a long-running `pactl subscribe`.
//
// This is deliberately separate from VolumeControl, which stays a tiny
// default-sink indicator for the dock widget. The single shared preference the
// two agree on is the >100% ceiling (persisted under kdock/kdock audio group).

#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVector>

class AudioControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool maxVolume READ maxVolume WRITE setMaxVolume NOTIFY maxVolumeChanged)

public:
    enum DeviceType { Output, Input, Application };
    Q_ENUM(DeviceType)

    struct Device {
        DeviceType type = Output;
        int index = -1;            // pactl numeric index (used for control)
        QString name;              // pactl node name (used for set-default)
        QString description;       // human-readable label
        QString iconName;          // theme icon name
        qreal volume = 0.0;        // 0..1.5
        bool muted = false;
        bool isDefault = false;    // Output/Input only
    };

    explicit AudioControl(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QVector<Device> outputs() const { return m_outputs; }
    QVector<Device> inputs() const { return m_inputs; }
    QVector<Device> apps() const { return m_apps; }

    bool maxVolume() const { return m_maxVolume; }
    qreal volumeCeiling() const { return m_maxVolume ? 1.5 : 1.0; }

    // The QSettings key both AudioControl and VolumeControl read for the ceiling.
    static bool maxVolumeSetting();

public slots:
    void setMaxVolume(bool on);
    void setVolume(AudioControl::DeviceType type, int index, qreal volume);
    void setMuted(AudioControl::DeviceType type, int index, bool muted);
    void toggleMute(AudioControl::DeviceType type, int index);
    void setDefault(AudioControl::DeviceType type, const QString &name);
    void refresh();

signals:
    void changed();
    void maxVolumeChanged();

private:
    void scheduleRefresh();
    void startSubscriber();
    QString runSync(const QStringList &args) const;
    QVector<Device> parseDevices(DeviceType type, const QString &text) const;
    QString setVolumeVerb(DeviceType type) const;
    QString setMuteVerb(DeviceType type) const;

    QString m_pactl;
    bool m_available = false;
    bool m_maxVolume = false;
    QVector<Device> m_outputs;
    QVector<Device> m_inputs;
    QVector<Device> m_apps;
    QString m_defaultSink;
    QString m_defaultSource;
    QProcess m_subscriber;
    QTimer m_refreshDebounce;
};
