#include "audiocontrol.h"

#include "childprocess.h"

#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

static const auto kMaxVolumeKey = QStringLiteral("audio/maxVolume");

bool AudioControl::maxVolumeSetting()
{
    QSettings s(QStringLiteral("kdock"), QStringLiteral("kdock"));
    return s.value(kMaxVolumeKey, false).toBool();
}

AudioControl::AudioControl(QObject *parent)
    : QObject(parent)
{
    m_maxVolume = maxVolumeSetting();

    m_pactl = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (m_pactl.isEmpty()) {
        qInfo("kdock: pactl not found, audio settings disabled");
        return;
    }

    m_refreshDebounce.setSingleShot(true);
    m_refreshDebounce.setInterval(150);
    connect(&m_refreshDebounce, &QTimer::timeout, this, &AudioControl::refresh);

    refresh();
    startSubscriber();
}

// This used to be a synchronous runSync() with waitForFinished(800), called
// five times in a row by refresh(). That is up to four seconds with the GUI
// thread stopped dead, and it bit for real (2026-08-16): plugging in a monitor
// whose HDMI sink joins the graph makes pactl slow *and* makes `pactl
// subscribe` flood events, so every 150 ms debounce bought another four-second
// freeze and the dock looked hung until the graph settled. Measured with a
// deliberately slow fake pactl: 4007 ms blocked, 2 of 60 timer ticks delivered.
// Nothing here may wait on a child process any more.
void AudioControl::runAsync(const QStringList &args, std::function<void(QString)> onDone)
{
    if (m_pactl.isEmpty()) {
        onDone(QString());
        return;
    }

    auto *p = new QProcess(this);
    // Force the C locale so the field labels we parse ("Name:", "Mute:", ...)
    // are the untranslated English ones regardless of the user's language.
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    p->setProcessEnvironment(env);

    // A wedged pactl now costs one killed child instead of a frozen dock. The
    // flag is what keeps the callback to exactly one call: a watchdog kill
    // raises errorOccurred(Crashed) *and* finished, and a failure to start
    // raises only errorOccurred.
    auto delivered = std::make_shared<bool>(false);
    const auto deliver = [p, onDone, delivered](const QString &out) {
        if (*delivered)
            return;
        *delivered = true;
        onDone(out);
        p->deleteLater();
    };

    auto *watchdog = new QTimer(p);
    watchdog->setSingleShot(true);
    watchdog->setInterval(kQueryTimeoutMs);
    connect(watchdog, &QTimer::timeout, p, [p] { p->kill(); });

    connect(p, &QProcess::finished, this, [p, deliver](int, QProcess::ExitStatus status) {
        deliver(status == QProcess::NormalExit ? QString::fromUtf8(p->readAllStandardOutput())
                                               : QString());
    });
    connect(p, &QProcess::errorOccurred, this, [deliver](QProcess::ProcessError) {
        deliver(QString());
    });

    kdock::tieToParent(*p);
    watchdog->start();
    p->start(m_pactl, args);
}

void AudioControl::refresh()
{
    if (m_pactl.isEmpty())
        return;
    // Coalesce instead of stacking: one batch in flight, one remembered. Without
    // this the event storm of a graph rebuild would start five processes per
    // event for as long as it lasted.
    if (m_refreshInFlight) {
        m_refreshQueued = true;
        return;
    }
    m_refreshInFlight = true;
    m_batch = RefreshBatch{};
    m_batch.pending = 6;

    runAsync({QStringLiteral("get-default-sink")}, [this](const QString &out) {
        m_batch.defaultSink = out.trimmed();
        finishRefresh();
    });
    runAsync({QStringLiteral("get-default-source")}, [this](const QString &out) {
        m_batch.defaultSource = out.trimmed();
        finishRefresh();
    });
    runAsync({QStringLiteral("list"), QStringLiteral("sinks")}, [this](const QString &out) {
        m_batch.sinks = out;
        finishRefresh();
    });
    runAsync({QStringLiteral("list"), QStringLiteral("sources")}, [this](const QString &out) {
        m_batch.sources = out;
        finishRefresh();
    });
    runAsync({QStringLiteral("list"), QStringLiteral("sink-inputs")}, [this](const QString &out) {
        m_batch.sinkInputs = out;
        finishRefresh();
    });
    runAsync({QStringLiteral("list"), QStringLiteral("cards")}, [this](const QString &out) {
        m_batch.cards = out;
        finishRefresh();
    });
}

void AudioControl::finishRefresh()
{
    if (--m_batch.pending > 0)
        return;

    // Defaults first: parseDevices() compares against them to flag the default
    // device, which is the one ordering constraint the old serial code had.
    m_defaultSink = m_batch.defaultSink;
    m_defaultSource = m_batch.defaultSource;
    m_outputs = parseDevices(Output, m_batch.sinks);
    m_inputs = parseDevices(Input, m_batch.sources);
    m_apps = parseDevices(Application, m_batch.sinkInputs);
    m_cards = parseCards(m_batch.cards);

    m_available = true;
    m_refreshInFlight = false;
    emit changed();

    if (m_refreshQueued) {
        m_refreshQueued = false;
        scheduleRefresh(); // through the debounce, so a storm still coalesces
    }
}

QVector<AudioControl::Device> AudioControl::parseDevices(DeviceType type, const QString &text) const
{
    QVector<Device> list;
    // Block headers appear at column 0; "Sink Input" must be tried before
    // "Sink" so the longer token wins.
    static const QRegularExpression head(
        QStringLiteral("^(Sink Input|Source Output|Sink|Source) #(\\d+)"));
    static const QRegularExpression pctRe(QStringLiteral("(\\d+)%"));

    Device cur;
    QHash<QString, QString> props;
    bool have = false;

    const auto finalize = [&]() {
        if (!have)
            return;
        if (type == Input && cur.name.endsWith(QStringLiteral(".monitor")))
            return; // hide sink monitors from the microphone list
        if (type == Application) {
            cur.description = props.value(QStringLiteral("application.name"));
            if (cur.description.isEmpty())
                cur.description = props.value(QStringLiteral("media.name"));
            if (cur.description.isEmpty())
                cur.description = tr("Application %1").arg(cur.index);
            cur.iconName = props.value(QStringLiteral("application.icon_name"));
            if (cur.iconName.isEmpty())
                cur.iconName = props.value(QStringLiteral("application.name")).toLower();
            if (cur.iconName.isEmpty())
                cur.iconName = QStringLiteral("multimedia-player");
        } else {
            if (cur.description.isEmpty())
                cur.description = cur.name;
            cur.iconName = props.value(QStringLiteral("device.icon_name"));
            if (cur.iconName.isEmpty())
                cur.iconName = (type == Input) ? QStringLiteral("audio-input-microphone")
                                               : QStringLiteral("audio-card");
            cur.isDefault = (type == Output) ? (cur.name == m_defaultSink)
                                             : (cur.name == m_defaultSource);
        }
        list.append(cur);
    };

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const auto hm = head.match(raw);
        if (hm.hasMatch()) {
            finalize();
            cur = Device();
            cur.type = type;
            cur.index = hm.captured(2).toInt();
            props.clear();
            have = true;
            continue;
        }
        if (!have)
            continue;

        // Property lines are double-tab indented: `\t\tkey = "value"`.
        if (raw.startsWith(QLatin1String("\t\t"))) {
            const QString line = raw.trimmed();
            const int eq = line.indexOf(QStringLiteral(" = "));
            if (eq > 0) {
                const QString key = line.left(eq).trimmed();
                QString val = line.mid(eq + 3).trimmed();
                if (val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"')))
                    val = val.mid(1, val.size() - 2);
                props.insert(key, val);
            }
            continue;
        }

        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("Name: ")))
            cur.name = line.mid(6).trimmed();
        else if (line.startsWith(QLatin1String("Description: ")) && cur.description.isEmpty())
            cur.description = line.mid(13).trimmed();
        else if (line.startsWith(QLatin1String("Mute: ")))
            cur.muted = (line.mid(6).trimmed() == QLatin1String("yes"));
        else if (line.startsWith(QLatin1String("Volume: "))) {
            const auto pm = pctRe.match(line);
            if (pm.hasMatch())
                cur.volume = pm.captured(1).toInt() / 100.0;
        }
    }
    finalize();
    return list;
}

QVector<AudioControl::Card> AudioControl::parseCards(const QString &text) const
{
    QVector<Card> list;

    static const QRegularExpression cardHead(QStringLiteral("^Card #(\\d+)"));
    // A profile line is "<name>: <description> (sinks: N, sources: N, priority:
    // N, available: yes|no)". The name itself contains colons but never a space,
    // so the first space is the delimiter between name and description.
    static const QRegularExpression availRe(QStringLiteral("\\(sinks: \\d+, sources: \\d+, priority: \\d+, available: (yes|no)\\)"));

    Card cur;
    bool have = false;
    // Which double-tab section of the card we are in; profiles only parse in
    // "Profiles:", properties only in "Properties:".
    bool inProfiles = false;
    bool inProperties = false;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const auto hm = cardHead.match(raw);
        if (hm.hasMatch()) {
            if (have)
                list.append(cur);
            cur = Card{};
            cur.index = hm.captured(1).toInt();
            inProfiles = inProperties = false;
            have = true;
            continue;
        }
        if (!have)
            continue;

        if (raw == QLatin1String("\tProperties:")) {
            inProperties = true;
            inProfiles = false;
            continue;
        }
        if (raw == QLatin1String("\tProfiles:")) {
            inProfiles = true;
            inProperties = false;
            continue;
        }
        if (raw == QLatin1String("\tPorts:")) {
            // The double-tab lines after this are ports, not profiles; leaving
            // inProfiles on would swallow them into the profile list.
            inProfiles = inProperties = false;
            continue;
        }

        // Single-tab field lines of the card header (Name:, Active Profile:,
        // Driver:, ...). Name: is the pactl id used by set-card-profile, and
        // Active Profile: is the currently selected profile.
        if (raw.startsWith(QLatin1Char('\t')) && !raw.startsWith(QLatin1String("\t\t"))) {
            const QString line = raw.trimmed();
            if (line.startsWith(QLatin1String("Name: ")))
                cur.name = line.mid(6).trimmed();
            else if (line.startsWith(QLatin1String("Active Profile: ")))
                cur.activeProfile = line.mid(16).trimmed();
            continue;
        }

        if (raw.startsWith(QLatin1String("\t\t"))) {
            const QString line = raw.trimmed();
            if (inProfiles) {
                Profile p;
                const int sp = line.indexOf(QLatin1Char(' '));
                if (sp < 0)
                    continue;
                p.name = line.left(sp).trimmed();
                if (p.name.endsWith(QLatin1Char(':')))
                    p.name.chop(1);
                if (p.name.isEmpty())
                    continue;
                const auto am = availRe.match(line);
                if (am.hasMatch()) {
                    p.available = (am.captured(1) == QLatin1String("yes"));
                    p.description = line.mid(sp + 1, am.capturedStart() - sp - 1).trimmed();
                } else {
                    p.description = line.mid(sp + 1).trimmed();
                }
                cur.profiles.append(p);
            } else if (inProperties) {
                const int eq = line.indexOf(QStringLiteral(" = "));
                if (eq > 0 && line.left(eq).trimmed() == QLatin1String("device.description")) {
                    QString val = line.mid(eq + 3).trimmed();
                    if (val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"')))
                        val = val.mid(1, val.size() - 2);
                    cur.description = val;
                }
            }
            continue;
        }
    }
    if (have)
        list.append(cur);
    return list;
}

QString AudioControl::setVolumeVerb(DeviceType type) const
{
    switch (type) {
    case Output: return QStringLiteral("set-sink-volume");
    case Input: return QStringLiteral("set-source-volume");
    case Application: return QStringLiteral("set-sink-input-volume");
    }
    return {};
}

QString AudioControl::setMuteVerb(DeviceType type) const
{
    switch (type) {
    case Output: return QStringLiteral("set-sink-mute");
    case Input: return QStringLiteral("set-source-mute");
    case Application: return QStringLiteral("set-sink-input-mute");
    }
    return {};
}

void AudioControl::setVolume(DeviceType type, int index, qreal volume)
{
    if (m_pactl.isEmpty() || index < 0)
        return;
    volume = qBound(0.0, volume, volumeCeiling());
    const int pct = qRound(volume * 100.0);
    QProcess::startDetached(m_pactl,
                            {setVolumeVerb(type), QString::number(index),
                             QString::number(pct) + QStringLiteral("%")});
    // Optimistic local update; the subscriber confirms shortly after.
    QVector<Device> &vec = (type == Output) ? m_outputs : (type == Input) ? m_inputs : m_apps;
    for (Device &d : vec)
        if (d.index == index) { d.volume = volume; break; }
    emit changed();
}

void AudioControl::setMuted(DeviceType type, int index, bool muted)
{
    if (m_pactl.isEmpty() || index < 0)
        return;
    QProcess::startDetached(m_pactl,
                            {setMuteVerb(type), QString::number(index),
                             muted ? QStringLiteral("1") : QStringLiteral("0")});
    QVector<Device> &vec = (type == Output) ? m_outputs : (type == Input) ? m_inputs : m_apps;
    for (Device &d : vec)
        if (d.index == index) { d.muted = muted; break; }
    emit changed();
}

void AudioControl::toggleMute(DeviceType type, int index)
{
    QVector<Device> &vec = (type == Output) ? m_outputs : (type == Input) ? m_inputs : m_apps;
    for (const Device &d : vec)
        if (d.index == index) { setMuted(type, index, !d.muted); return; }
}

void AudioControl::setDefault(DeviceType type, const QString &name)
{
    if (m_pactl.isEmpty() || name.isEmpty() || type == Application)
        return;
    const QString verb = (type == Output) ? QStringLiteral("set-default-sink")
                                           : QStringLiteral("set-default-source");
    QProcess::startDetached(m_pactl, {verb, name});
    if (type == Output)
        m_defaultSink = name;
    else
        m_defaultSource = name;
    QVector<Device> &vec = (type == Output) ? m_outputs : m_inputs;
    for (Device &d : vec)
        d.isDefault = (d.name == name);
    emit changed();
}

void AudioControl::setCardProfile(int cardIndex, const QString &profileName)
{
    if (m_pactl.isEmpty() || cardIndex < 0 || profileName.isEmpty())
        return;
    QProcess::startDetached(m_pactl,
                            {QStringLiteral("set-card-profile"), QString::number(cardIndex),
                             profileName});
    // Switching the profile changes the very set of sinks/sources the card
    // exposes, so what we cached is stale by construction. The refresh through
    // the debounce picks up the new graph; the subscriber confirms shortly
    // after as well.
    scheduleRefresh();
}

namespace {
// One device as a QML-readable map. Kept here rather than in the header so the
// Device struct stays a plain aggregate.
QVariantMap deviceToMap(const AudioControl::Device &d)
{
    QVariantMap m;
    m[QStringLiteral("type")] = int(d.type);
    m[QStringLiteral("index")] = d.index;
    m[QStringLiteral("name")] = d.name;
    m[QStringLiteral("description")] = d.description;
    m[QStringLiteral("icon")] = d.iconName;
    m[QStringLiteral("volume")] = d.volume;
    m[QStringLiteral("muted")] = d.muted;
    m[QStringLiteral("isDefault")] = d.isDefault;
    return m;
}

QVariantList devicesToList(const QVector<AudioControl::Device> &devices)
{
    QVariantList out;
    out.reserve(devices.size());
    for (const AudioControl::Device &d : devices)
        out.append(deviceToMap(d));
    return out;
}

QVariantMap cardToMap(const AudioControl::Card &c)
{
    QVariantMap m;
    m[QStringLiteral("index")] = c.index;
    m[QStringLiteral("name")] = c.name;
    m[QStringLiteral("description")] = c.description;
    m[QStringLiteral("activeProfile")] = c.activeProfile;
    QVariantList profiles;
    for (const AudioControl::Profile &p : c.profiles) {
        QVariantMap pm;
        pm[QStringLiteral("name")] = p.name;
        pm[QStringLiteral("description")] = p.description;
        pm[QStringLiteral("available")] = p.available;
        profiles.append(pm);
    }
    m[QStringLiteral("profiles")] = profiles;
    return m;
}
} // namespace

QVariantList AudioControl::outputList() const
{
    return devicesToList(m_outputs);
}

QVariantList AudioControl::inputList() const
{
    return devicesToList(m_inputs);
}

QVariantList AudioControl::appList() const
{
    return devicesToList(m_apps);
}

QVariantList AudioControl::cardList() const
{
    QVariantList out;
    out.reserve(m_cards.size());
    for (const Card &c : m_cards)
        out.append(cardToMap(c));
    return out;
}

void AudioControl::setMaxVolume(bool on)
{
    if (m_maxVolume == on)
        return;
    m_maxVolume = on;
    QSettings s(QStringLiteral("kdock"), QStringLiteral("kdock"));
    s.setValue(kMaxVolumeKey, on);

    // Turning the ceiling back down: clamp anything currently above 100% so the
    // stored device volumes match the new limit (mirrors Plasma's behavior).
    if (!on) {
        const auto clampVec = [this](DeviceType type, QVector<Device> &vec) {
            for (const Device &d : vec)
                if (d.volume > 1.0)
                    setVolume(type, d.index, 1.0);
        };
        clampVec(Output, m_outputs);
        clampVec(Input, m_inputs);
        clampVec(Application, m_apps);
    }
    emit maxVolumeChanged();
    emit changed();
}

void AudioControl::scheduleRefresh()
{
    m_refreshDebounce.start();
}

void AudioControl::startSubscriber()
{
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    m_subscriber.setProcessEnvironment(env);
    connect(&m_subscriber, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray out = m_subscriber.readAllStandardOutput();
        if (out.contains("sink") || out.contains("source") || out.contains("server"))
            scheduleRefresh();
    });
    // Same reconnect policy as VolumeControl: pactl dies with the audio server,
    // and without this the mixer went stale until the dock was restarted.
    connect(&m_subscriber, &QProcess::finished, this, [this] {
        if (m_subscriberUptime.isValid() && m_subscriberUptime.elapsed() > 10000)
            m_subscriberBackoffMs = 1000;
        else
            m_subscriberBackoffMs = qMin(m_subscriberBackoffMs * 2, 30000);
        QTimer::singleShot(m_subscriberBackoffMs, this, [this] { launchSubscriber(); });
    });
    kdock::tieToParent(m_subscriber);
    launchSubscriber();
}

void AudioControl::launchSubscriber()
{
    if (m_subscriber.state() != QProcess::NotRunning)
        return;
    m_subscriberUptime.start();
    m_subscriber.start(m_pactl, {QStringLiteral("subscribe")});
    // Resync only if the subscriber stayed up: refresh() is half a dozen pactl
    // calls, and a dead server would otherwise pay them on every retry.
    QTimer::singleShot(500, this, [this] {
        if (m_subscriber.state() == QProcess::Running)
            scheduleRefresh();
    });
}
