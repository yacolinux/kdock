#include "volumecontrol.h"

#include "audiocontrol.h"
#include "childprocess.h"

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
    if (m_wpctl.isEmpty())
        return;
    if (m_refreshInFlight) {
        m_refreshQueued = true;
        return;
    }
    m_refreshInFlight = true;
    auto *p = new QProcess(this);
    auto *watchdog = new QTimer(p);
    watchdog->setSingleShot(true);
    watchdog->setInterval(800);
    connect(watchdog, &QTimer::timeout, p, [p] { p->kill(); });
    // Release the slot only after the child exits, including a watchdog kill.
    // FailedToStart is the only error that does not also emit finished().
    const auto finish = [this, p, watchdog](bool success) {
        watchdog->stop();
        m_refreshInFlight = false;
        if (m_refreshQueued) {
            m_refreshQueued = false;
            scheduleRefresh();
        }
        p->deleteLater();
        const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
        const QStringList parts = out.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        bool valid = false;
        const qreal vol = parts.size() >= 2 ? parts.at(1).toDouble(&valid) : 0.0;
        if (!success || !valid || parts.first() != QLatin1String("Volume:") || vol < 0) {
            if (m_available) {
                m_available = false;
                emit changed();
            }
            return;
        }
        // Output: "Volume: 0.85" or "Volume: 0.85 [MUTED]"
        const bool muted = out.contains(QLatin1String("[MUTED]"));
        if (!m_available || !qFuzzyCompare(vol, m_volume) || muted != m_muted) {
            m_available = true;
            m_volume = vol;
            m_muted = muted;
            emit changed();
        }
    };
    connect(p, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus status) {
        finish(code == 0 && status == QProcess::NormalExit);
    });
    connect(p, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(false);
    });
    kdock::tieToParent(*p);
    watchdog->start();
    p->start(m_wpctl, {QStringLiteral("get-volume"), DEFAULT_SINK});
}

void VolumeControl::scheduleRefresh()
{
    m_refreshDebounce.start();
}

void VolumeControl::startSubscriber()
{
    m_pactl = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (m_pactl.isEmpty()) {
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
    // pactl exits when the audio server drops the connection (an audio device
    // being re-plugged is enough). Nothing restarted it, so from then on the
    // widget only ever showed what its optimistic writes had guessed.
    connect(&m_subscriber, &QProcess::finished, this, [this] {
        // A subscriber that ran for a while means the server is healthy: reset
        // the backoff so the next real outage reconnects immediately. A short
        // life means pactl cannot connect at all — back off up to 30 s so a
        // dead server does not turn into a fork loop.
        if (m_subscriberUptime.isValid() && m_subscriberUptime.elapsed() > 10000)
            m_subscriberBackoffMs = 1000;
        else
            m_subscriberBackoffMs = qMin(m_subscriberBackoffMs * 2, 30000);
        QTimer::singleShot(m_subscriberBackoffMs, this, [this] { launchSubscriber(); });
    });
    kdock::tieToParent(m_subscriber);
    launchSubscriber();
}

void VolumeControl::launchSubscriber()
{
    if (m_subscriber.state() != QProcess::NotRunning)
        return;
    m_subscriberUptime.start();
    m_subscriber.start(m_pactl, {QStringLiteral("subscribe")});
    // Whatever changed while we had no subscription is invisible to us, so
    // resync — but only once the subscriber proves it survived, or a server
    // that is down turns every retry into a full pactl query round.
    QTimer::singleShot(500, this, [this] {
        if (m_subscriber.state() == QProcess::Running)
            scheduleRefresh();
    });
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
