#include "volumecontrol.h"

#include "audiocontrol.h"

#include <QStandardPaths>

static const auto DEFAULT_SINK = QStringLiteral("@DEFAULT_AUDIO_SINK@");

VolumeControl::VolumeControl(QObject *parent)
    : QObject(parent)
{
    m_wpctl = QStandardPaths::findExecutable(QStringLiteral("wpctl"));
    if (m_wpctl.isEmpty()) {
        qInfo("kdock: wpctl not found, volume widget disabled");
        return;
    }

    m_refreshDebounce.setSingleShot(true);
    m_refreshDebounce.setInterval(150);
    connect(&m_refreshDebounce, &QTimer::timeout, this, &VolumeControl::refresh);

    refresh();
    startSubscriber();

    // Catch changes made between our initial read and the subscription going
    // live (e.g. Plasma restoring the volume/mute at login), which would
    // otherwise leave the cache stale until the next unrelated event.
    QTimer::singleShot(1000, this, [this] { refresh(); });
}

QString VolumeControl::iconName() const
{
    if (m_muted || m_volume <= 0.0)
        return QStringLiteral("audio-volume-muted");
    if (m_volume < 0.34)
        return QStringLiteral("audio-volume-low");
    if (m_volume < 0.67)
        return QStringLiteral("audio-volume-medium");
    return QStringLiteral("audio-volume-high");
}

void VolumeControl::refresh()
{
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p](int exitCode) {
        p->deleteLater();
        if (exitCode != 0) {
            if (m_available) {
                m_available = false;
                emit changed();
            }
            return;
        }
        // Output: "Volume: 0.85" or "Volume: 0.85 [MUTED]"
        const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
        const QStringList parts = out.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() < 2)
            return;
        const qreal vol = parts.at(1).toDouble();
        const bool muted = out.contains(QLatin1String("[MUTED]"));
        if (!m_available || !qFuzzyCompare(vol, m_volume) || muted != m_muted) {
            m_available = true;
            m_volume = vol;
            m_muted = muted;
            emit changed();
        }
    });
    p->start(m_wpctl, {QStringLiteral("get-volume"), DEFAULT_SINK});
}

void VolumeControl::scheduleRefresh()
{
    m_refreshDebounce.start();
}

void VolumeControl::startSubscriber()
{
    const QString pactl = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (pactl.isEmpty()) {
        // No event source: poll
        auto *poll = new QTimer(this);
        poll->setInterval(3000);
        connect(poll, &QTimer::timeout, this, &VolumeControl::refresh);
        poll->start();
        return;
    }

    connect(&m_subscriber, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray out = m_subscriber.readAllStandardOutput();
        if (out.contains("sink") || out.contains("server"))
            scheduleRefresh();
    });
    m_subscriber.start(pactl, {QStringLiteral("subscribe")});
}

void VolumeControl::setVolume(qreal volume)
{
    if (m_wpctl.isEmpty())
        return;
    // Share the >100% ceiling with the Audio settings tab (AudioControl).
    const qreal ceiling = AudioControl::maxVolumeSetting() ? 1.5 : 1.0;
    volume = qBound(0.0, volume, ceiling);
    QStringList args{QStringLiteral("set-volume")};
    // wpctl caps at 100% unless an explicit --limit is passed.
    if (ceiling > 1.0)
        args << QStringLiteral("-l") << QStringLiteral("1.5");
    args << DEFAULT_SINK << QString::number(volume, 'f', 2);
    QProcess::startDetached(m_wpctl, args);
    m_volume = volume; // optimistic; the subscriber confirms shortly after
    emit changed();
}

void VolumeControl::toggleMute()
{
    if (m_wpctl.isEmpty())
        return;
    QProcess::startDetached(m_wpctl,
                            {QStringLiteral("set-mute"), DEFAULT_SINK, QStringLiteral("toggle")});
    m_muted = !m_muted; // optimistic feedback
    emit changed();
    // Reconcile with the real state shortly after, in case the cache was stale
    // or the subscriber misses the confirming event.
    scheduleRefresh();
}
