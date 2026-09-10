#include "screenbrightness.h"

#include <QGuiApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDBusMessage>
#include <QDBusReply>
#include <QVariantMap>

namespace {
const auto kService = QStringLiteral("org.kde.ScreenBrightness");
const auto kRootPath = QStringLiteral("/org/kde/ScreenBrightness");
const auto kRootIface = QStringLiteral("org.kde.ScreenBrightness");
const auto kDisplayIface = QStringLiteral("org.kde.ScreenBrightness.Display");
const auto kPropsIface = QStringLiteral("org.freedesktop.DBus.Properties");

QString pathFor(const QString &name)
{
    return kRootPath + QLatin1Char('/') + name;
}

constexpr int kMaxRefreshAttempts = 8;
constexpr int kInitialRefreshDelayMs = 100;
} // namespace

ScreenBrightness::ScreenBrightness(QObject *parent)
    : QObject(parent)
{
    m_refreshTimer.setSingleShot(true);
    connect(&m_refreshTimer, &QTimer::timeout, this, &ScreenBrightness::retryRefresh);
    connect(qGuiApp, &QGuiApplication::screenAdded, this,
            [this](QScreen *) { scheduleRefresh(); });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this,
            [this](QScreen *) { scheduleRefresh(); });
    connectSignals();
    refresh();
}

void ScreenBrightness::connectSignals()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;

    bus.connect(kService, kRootPath, kRootIface, QStringLiteral("DisplayAdded"), this,
                SLOT(scheduleRefresh()));
    bus.connect(kService, kRootPath, kRootIface, QStringLiteral("DisplayRemoved"), this,
                SLOT(scheduleRefresh()));
    // BrightnessChanged is (s, i, s, s) — the two trailing strings say which
    // client changed it and why. A slot may take fewer arguments than the
    // signal, so this one stops at the value.
    bus.connect(kService, kRootPath, kRootIface, QStringLiteral("BrightnessChanged"), this,
                SLOT(onBrightnessChanged(QString, int)));
    bus.connect(kService, kRootPath, kRootIface, QStringLiteral("BrightnessRangeChanged"), this,
                SLOT(onRangeChanged(QString, int)));

    // PowerDevil can be restarted (or start later than we do): pick the displays
    // up when it comes back instead of staying empty for the session.
    bus.connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"),
                QStringList{kService}, QString(), this, SLOT(scheduleRefresh()));

    // The display objects are recreated after resume, and PowerDevil may emit
    // its DisplayAdded signal before their Properties interfaces are ready.
    // Re-enumerate after logind announces that the machine woke up as well.
    QDBusConnection::systemBus().connect(
        QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("PrepareForSleep"),
        this, SLOT(onPrepareForSleep(bool)));
}

int ScreenBrightness::indexOf(const QString &name) const
{
    for (int i = 0; i < m_displays.size(); ++i) {
        if (m_displays.at(i).toMap().value(QStringLiteral("name")).toString() == name)
            return i;
    }
    return -1;
}

QVariantList ScreenBrightness::fixtureDisplays()
{
    const QByteArray raw = qgetenv("KDOCK_TEST_DISPLAYS");
    if (raw.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isArray())
        return {};

    QVariantList out;
    const QJsonArray array = doc.array();
    for (const QJsonValue &v : array) {
        QVariantMap entry = v.toObject().toVariantMap();
        const int max = entry.value(QStringLiteral("max")).toInt();
        const int value = entry.value(QStringLiteral("brightness")).toInt();
        entry[QStringLiteral("value")] = max > 0 ? qreal(value) / max : 0.0;
        out.append(entry);
    }
    return out;
}

void ScreenBrightness::refresh()
{
    const QVariantList before = m_displays;
    const bool wasAvailable = m_available;

    m_displays.clear();
    m_available = false;

    // Presence of the variable is what counts, not its contents: "[]" is how a
    // test says "no PowerDevil here". Deciding by the parsed list instead would
    // send that case out to the session bus, where the *real* PowerDevil
    // answers and the test stops being a test.
    if (qEnvironmentVariableIsSet("KDOCK_TEST_DISPLAYS")) {
        m_displays = fixtureDisplays();
        m_available = !m_displays.isEmpty();
        m_refreshTimer.stop();
        m_refreshAttempt = 0;
        if (wasAvailable != m_available || before != m_displays)
            emit changed();
        return;
    }

    QDBusInterface root(kService, kRootPath, kPropsIface, QDBusConnection::sessionBus());
    if (!root.isValid()) {
        scheduleRetry();
        if (wasAvailable != m_available || before != m_displays)
            emit changed();
        return;
    }

    const QDBusReply<QVariant> namesReply =
        root.call(QStringLiteral("Get"), kRootIface, QStringLiteral("DisplaysDBusNames"));
    if (!namesReply.isValid()) {
        scheduleRetry();
        if (wasAvailable != m_available || before != m_displays)
            emit changed();
        return;
    }
    const QStringList names = namesReply.value().toStringList();
    bool complete = !names.isEmpty();

    for (const QString &name : names) {
        QDBusInterface props(kService, pathFor(name), kPropsIface, QDBusConnection::sessionBus());
        // GetAll in one round trip: four separate Get calls per display add up
        // when a hotplug burst re-runs this.
        const QDBusReply<QVariantMap> all = props.call(QStringLiteral("GetAll"), kDisplayIface);
        if (!all.isValid()) {
            complete = false;
            continue;
        }
        const QVariantMap m = all.value();
        const int max = m.value(QStringLiteral("MaxBrightness")).toInt();
        const int value = m.value(QStringLiteral("Brightness")).toInt();

        QVariantMap entry;
        entry[QStringLiteral("name")] = name;
        entry[QStringLiteral("label")] = m.value(QStringLiteral("Label")).toString();
        entry[QStringLiteral("internal")] = m.value(QStringLiteral("IsInternal")).toBool();
        entry[QStringLiteral("brightness")] = value;
        entry[QStringLiteral("max")] = max;
        entry[QStringLiteral("value")] = max > 0 ? qreal(value) / max : 0.0;
        m_displays.append(entry);
    }

    // A valid root object is not enough: during hotplug/resume the root can
    // already list the new names while the per-display objects still reject
    // GetAll. Reporting available=true in that window made the UI commit to
    // an empty, permanent state. Only expose the backend when at least one
    // display was enumerated, and retry incomplete scans below.
    m_available = !m_displays.isEmpty();
    if (complete) {
        m_refreshTimer.stop();
        m_refreshAttempt = 0;
    } else {
        scheduleRetry();
    }

    if (wasAvailable != m_available || before != m_displays)
        emit changed();
}

void ScreenBrightness::scheduleRefresh()
{
    // D-Bus can report several add/remove events for one physical monitor.
    // Coalesce them and restart the bounded retry window for the new topology.
    m_refreshTimer.stop();
    m_refreshAttempt = 0;
    m_refreshTimer.start(kInitialRefreshDelayMs);
}

void ScreenBrightness::retryRefresh()
{
    refresh();
}

void ScreenBrightness::scheduleRetry()
{
    if (m_refreshTimer.isActive() || m_refreshAttempt >= kMaxRefreshAttempts)
        return;

    // 100, 200, 400, ... 12.8s: enough for PowerDevil/DRM to finish a
    // suspend or connector transition without polling forever when the
    // service is genuinely absent (the normal LXQt case).
    const int delay = kInitialRefreshDelayMs << m_refreshAttempt;
    ++m_refreshAttempt;
    m_refreshTimer.start(delay);
}

void ScreenBrightness::onPrepareForSleep(bool sleeping)
{
    if (!sleeping)
        scheduleRefresh();
}

void ScreenBrightness::onBrightnessChanged(const QString &name, int brightness)
{
    const int i = indexOf(name);
    if (i < 0) {
        refresh();
        return;
    }
    QVariantMap entry = m_displays.at(i).toMap();
    const int max = entry.value(QStringLiteral("max")).toInt();
    entry[QStringLiteral("brightness")] = brightness;
    entry[QStringLiteral("value")] = max > 0 ? qreal(brightness) / max : 0.0;
    m_displays[i] = entry;
    emit changed();
}

void ScreenBrightness::onRangeChanged(const QString &name, int maxBrightness)
{
    const int i = indexOf(name);
    if (i < 0) {
        refresh();
        return;
    }
    QVariantMap entry = m_displays.at(i).toMap();
    entry[QStringLiteral("max")] = maxBrightness;
    const int value = entry.value(QStringLiteral("brightness")).toInt();
    entry[QStringLiteral("value")] = maxBrightness > 0 ? qreal(value) / maxBrightness : 0.0;
    m_displays[i] = entry;
    emit changed();
}

void ScreenBrightness::setBrightness(const QString &name, qreal ratio)
{
    const int i = indexOf(name);
    if (i < 0)
        return;
    const QVariantMap entry = m_displays.at(i).toMap();
    const int max = entry.value(QStringLiteral("max")).toInt();
    if (max <= 0)
        return;

    ratio = qBound(MinBrightness, ratio, 1.0);
    const int target = qBound(1, int(qRound(ratio * max)), max);

    QDBusMessage msg = QDBusMessage::createMethodCall(kService, pathFor(name), kDisplayIface,
                                                      QStringLiteral("SetBrightness"));
    msg.setArguments({target, 0u});
    QDBusConnection::sessionBus().asyncCall(msg);

    // Optimistic update: the BrightnessChanged signal confirms it a moment
    // later, and a slider that waits for a round trip feels broken.
    onBrightnessChanged(name, target);
}

void ScreenBrightness::setAll(qreal ratio)
{
    const QVariantList list = m_displays;
    for (const QVariant &v : list)
        setBrightness(v.toMap().value(QStringLiteral("name")).toString(), ratio);
}
