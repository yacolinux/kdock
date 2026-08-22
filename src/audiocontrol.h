// Full audio mixer backend for the settings "Audio" tab: enumerates output
// sinks, input sources and per-application streams via PipeWire's PulseAudio
// compat CLI (pactl), and drives per-device / per-app volume, mute and
// default-device selection. pactl is invoked with LC_ALL=C so the parsed
// field labels ("Name:", "Description:", "Mute:", "Volume:") stay stable.
// Live change notifications come from a long-running `pactl subscribe`, which
// is restarted with a backoff when the audio server drops it (see
// launchSubscriber).
//
// This is deliberately separate from VolumeControl, which stays a tiny
// default-sink indicator for the dock widget. The single shared preference the
// two agree on is the >100% ceiling (persisted under kdock/kdock audio group).

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>

#include <functional>

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

    // One card profile. The name is the machine-readable pactl id
    // ("output:hdmi-stereo+input:analog-stereo"); the description is the
    // localized label ACP gives it. `available` is whether any of the ports the
    // profile needs are physically connected — an HDMI monitor plugged in shows
    // up here before its sink even exists, because the sink is only created
    // once the profile becomes active.
    struct Profile {
        QString name;
        QString description;
        bool available = false;
    };

    struct Card {
        int index = -1;            // pactl numeric index (used for set-card-profile)
        QString name;              // e.g. alsa_card.pci-0000_00_1f.3
        QString description;
        QVector<Profile> profiles;
        QString activeProfile;
    };

    explicit AudioControl(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QVector<Device> outputs() const { return m_outputs; }
    QVector<Device> inputs() const { return m_inputs; }
    QVector<Device> apps() const { return m_apps; }
    QVector<Card> cards() const { return m_cards; }

    // The same three lists in a shape QML can read (the settings dialog uses the
    // QVector above; kdock-controlmanager draws them from QML). Each element is
    // a map with keys: type, index, name, description, icon, volume, muted,
    // isDefault.
    Q_INVOKABLE QVariantList outputList() const;
    Q_INVOKABLE QVariantList inputList() const;
    Q_INVOKABLE QVariantList appList() const;
    // Same shape as the device lists, but for cards: {index, name, description,
    // profiles: [{name, description, available}], activeProfile}.
    Q_INVOKABLE QVariantList cardList() const;
    // Highest volume a slider may offer (1.0, or 1.5 with the ceiling raised).
    Q_INVOKABLE qreal ceiling() const { return volumeCeiling(); }

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
    void setCardProfile(int cardIndex, const QString &profileName);
    void refresh();

signals:
    void changed();
    void maxVolumeChanged();

private:
    // How long a single pactl query may take before it is killed. This used to
    // be a waitForFinished() on the GUI thread, which is what made a busy audio
    // server freeze the whole dock (see runAsync).
    static constexpr int kQueryTimeoutMs = 800;

    void scheduleRefresh();
    void startSubscriber();
    void launchSubscriber();
    // Runs pactl without ever blocking the caller: onDone gets the stdout, or an
    // empty string if the process failed to start, crashed or hit the watchdog.
    void runAsync(const QStringList &args, std::function<void(QString)> onDone);
    void finishRefresh();
    QVector<Device> parseDevices(DeviceType type, const QString &text) const;
    QVector<Card> parseCards(const QString &text) const;
    QString setVolumeVerb(DeviceType type) const;
    QString setMuteVerb(DeviceType type) const;

    // The six queries of one refresh land in any order, so they are collected
    // here and applied together — the defaults first, because parseDevices()
    // reads them to mark the default device.
    struct RefreshBatch {
        int pending = 0;
        QString defaultSink;
        QString defaultSource;
        QString sinks;
        QString sources;
        QString sinkInputs;
        QString cards;
    };

    QString m_pactl;
    bool m_available = false;
    bool m_maxVolume = false;
    QVector<Device> m_outputs;
    QVector<Device> m_inputs;
    QVector<Device> m_apps;
    QVector<Card> m_cards;
    QString m_defaultSink;
    QString m_defaultSource;
    QProcess m_subscriber;
    QTimer m_refreshDebounce;
    QElapsedTimer m_subscriberUptime;
    int m_subscriberBackoffMs = 1000;
    RefreshBatch m_batch;
    // One batch at a time, and at most one more remembered: while the audio
    // graph is being rebuilt the subscriber fires far faster than pactl answers,
    // and a batch per event would pile up processes without end.
    bool m_refreshInFlight = false;
    bool m_refreshQueued = false;
};
