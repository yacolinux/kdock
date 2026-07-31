#include "audiocontrol.h"

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

QString AudioControl::runSync(const QStringList &args) const
{
    if (m_pactl.isEmpty())
        return {};
    QProcess p;
    // Force the C locale so the field labels we parse ("Name:", "Mute:", ...)
    // are the untranslated English ones regardless of the user's language.
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    p.setProcessEnvironment(env);
    p.start(m_pactl, args);
    if (!p.waitForFinished(800)) {
        p.kill();
        p.waitForFinished(100);
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

void AudioControl::refresh()
{
    if (m_pactl.isEmpty())
        return;

    m_defaultSink = runSync({QStringLiteral("get-default-sink")}).trimmed();
    m_defaultSource = runSync({QStringLiteral("get-default-source")}).trimmed();

    m_outputs = parseDevices(Output, runSync({QStringLiteral("list"), QStringLiteral("sinks")}));
    m_inputs = parseDevices(Input, runSync({QStringLiteral("list"), QStringLiteral("sources")}));
    m_apps = parseDevices(Application, runSync({QStringLiteral("list"), QStringLiteral("sink-inputs")}));

    m_available = true;
    emit changed();
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
    m_subscriber.start(m_pactl, {QStringLiteral("subscribe")});
}
