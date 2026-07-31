#include "batterycontrol.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>

namespace {
constexpr const char *kUPowerService = "org.freedesktop.UPower";
constexpr const char *kDisplayDevicePath =
    "/org/freedesktop/UPower/devices/DisplayDevice";
constexpr const char *kDeviceIface = "org.freedesktop.UPower.Device";
constexpr const char *kPropsIface = "org.freedesktop.DBus.Properties";

// UPower DeviceState enum
constexpr uint kStateCharging = 1;
constexpr uint kStateFull = 4;
constexpr uint kStatePendingCharge = 5;
// UPower DeviceType enum
constexpr uint kTypeBattery = 2;
} // namespace

BatteryControl::BatteryControl(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::systemBus();

    // ---- Battery (UPower DisplayDevice) --------------------------------
    m_battIface = new QDBusInterface(QString::fromLatin1(kUPowerService),
                                     QString::fromLatin1(kDisplayDevicePath),
                                     QString::fromLatin1(kDeviceIface), bus, this);
    // UPower emits standard PropertiesChanged on the device object.
    bus.connect(QString::fromLatin1(kUPowerService),
                QString::fromLatin1(kDisplayDevicePath),
                QString::fromLatin1(kPropsIface),
                QStringLiteral("PropertiesChanged"), this, SLOT(refreshBattery()));
    refreshBattery();

    // ---- Power profiles (power-profiles-daemon) ------------------------
    setupProfiles();
}

void BatteryControl::refreshBattery()
{
    if (!m_battIface || !m_battIface->isValid()) {
        if (m_hasBattery) {
            m_hasBattery = false;
            emit changed();
        }
        return;
    }

    const uint type = m_battIface->property("Type").toUInt();
    // DisplayDevice reports Type 0 when there is no battery at all.
    const bool has = (type == kTypeBattery);
    const int pct = qRound(m_battIface->property("Percentage").toDouble());
    const uint state = m_battIface->property("State").toUInt();
    const qlonglong tte = m_battIface->property("TimeToEmpty").toLongLong();
    const qlonglong ttf = m_battIface->property("TimeToFull").toLongLong();
    const QString icon = m_battIface->property("IconName").toString();
    const bool charging =
        (state == kStateCharging || state == kStatePendingCharge || state == kStateFull);

    if (has != m_hasBattery || pct != m_percentage || state != m_state
        || charging != m_charging || tte != m_timeToEmpty || ttf != m_timeToFull
        || icon != m_upowerIcon) {
        m_hasBattery = has;
        m_percentage = pct;
        m_state = static_cast<int>(state);
        m_charging = charging;
        m_timeToEmpty = tte;
        m_timeToFull = ttf;
        m_upowerIcon = icon;
        emit changed();
    }
}

QString BatteryControl::iconName() const
{
    // Prefer UPower's themed symbolic icon when available.
    if (!m_upowerIcon.isEmpty())
        return m_upowerIcon;
    // Fallback: bucket the percentage to the standard battery-XXX icons.
    int bucket = qBound(0, (m_percentage + 5) / 10 * 10, 100);
    QString name = QStringLiteral("battery-%1").arg(bucket, 3, 10, QLatin1Char('0'));
    if (m_charging)
        name += QStringLiteral("-charging");
    return name + QStringLiteral("-symbolic");
}

QString BatteryControl::tooltipText() const
{
    if (!m_hasBattery)
        return m_profilesAvailable ? tr("Power profile: %1").arg(m_activeProfile)
                                   : QString();

    QString s = tr("Battery: %1%").arg(m_percentage);
    if (m_state == static_cast<int>(kStateFull))
        s += QStringLiteral(" (") + tr("fully charged") + QStringLiteral(")");
    else if (m_charging)
        s += QStringLiteral(" (") + tr("charging") + QStringLiteral(")");
    else
        s += QStringLiteral(" (") + tr("on battery") + QStringLiteral(")");

    const qlonglong secs = m_charging ? m_timeToFull : m_timeToEmpty;
    if (secs > 0) {
        const int h = int(secs / 3600);
        const int m = int((secs % 3600) / 60);
        s += QStringLiteral("\n") + tr("%1h %2m remaining").arg(h).arg(m);
    }
    if (m_profilesAvailable)
        s += QStringLiteral("\n") + tr("Profile: %1").arg(m_activeProfile);
    return s;
}

// ---------------------------------------------------------------------------
// Power profiles
// ---------------------------------------------------------------------------

void BatteryControl::setupProfiles()
{
    QDBusConnection bus = QDBusConnection::systemBus();

    // The daemon carries two names: the modern org.freedesktop.UPower.PowerProfiles
    // and the legacy net.hadess.PowerProfiles. Probe both.
    struct Candidate {
        const char *service;
        const char *path;
        const char *iface;
    };
    static const Candidate kCandidates[] = {
        {"org.freedesktop.UPower.PowerProfiles",
         "/org/freedesktop/UPower/PowerProfiles",
         "org.freedesktop.UPower.PowerProfiles"},
        {"net.hadess.PowerProfiles", "/net/hadess/PowerProfiles",
         "net.hadess.PowerProfiles"},
    };

    for (const Candidate &c : kCandidates) {
        auto *iface = new QDBusInterface(QString::fromLatin1(c.service),
                                         QString::fromLatin1(c.path),
                                         QString::fromLatin1(c.iface), bus, this);
        if (iface->isValid()) {
            m_ppdIface = iface;
            m_ppdService = QString::fromLatin1(c.service);
            m_ppdPath = QString::fromLatin1(c.path);
            m_ppdInterface = QString::fromLatin1(c.iface);
            break;
        }
        iface->deleteLater();
    }

    if (!m_ppdIface)
        return;

    m_profilesAvailable = true;
    bus.connect(m_ppdService, m_ppdPath, QString::fromLatin1(kPropsIface),
                QStringLiteral("PropertiesChanged"), this, SLOT(refreshProfiles()));
    refreshProfiles();
}

void BatteryControl::refreshProfiles()
{
    if (!m_ppdIface || !m_ppdIface->isValid())
        return;

    const QString active = m_ppdIface->property("ActiveProfile").toString();

    // "Profiles" is aa{sv}. QDBusInterface::property() fails to demarshal this
    // complex type (returns a QVariant that is not a QDBusArgument), so fetch it
    // explicitly via org.freedesktop.DBus.Properties.Get, whose reply wraps the
    // array in a QDBusVariant we can unpack.
    QStringList names;
    QDBusMessage getMsg = QDBusMessage::createMethodCall(
        m_ppdService, m_ppdPath, QString::fromLatin1(kPropsIface),
        QStringLiteral("Get"));
    getMsg << m_ppdInterface << QStringLiteral("Profiles");
    const QDBusMessage reply =
        QDBusConnection::systemBus().call(getMsg);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QVariant inner = reply.arguments().constFirst();
        if (inner.canConvert<QDBusVariant>())
            inner = inner.value<QDBusVariant>().variant();
        if (inner.canConvert<QDBusArgument>()) {
            QDBusArgument arg = inner.value<QDBusArgument>();
            arg.beginArray();
            while (!arg.atEnd()) {
                QVariantMap entry;
                arg >> entry;
                const QString p = entry.value(QStringLiteral("Profile")).toString();
                if (!p.isEmpty())
                    names.append(p);
            }
            arg.endArray();
        }
    }

    if (active != m_activeProfile || names != m_profiles) {
        m_activeProfile = active;
        if (!names.isEmpty())
            m_profiles = names;
        emit changed();
    }
}

void BatteryControl::setProfile(const QString &profile)
{
    if (!m_ppdIface || profile.isEmpty() || profile == m_activeProfile)
        return;

    // Write ActiveProfile via org.freedesktop.DBus.Properties.Set.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        m_ppdService, m_ppdPath, QString::fromLatin1(kPropsIface),
        QStringLiteral("Set"));
    msg << m_ppdInterface << QStringLiteral("ActiveProfile")
        << QVariant::fromValue(QDBusVariant(profile));
    QDBusConnection::systemBus().asyncCall(msg);

    // Optimistic local update; PropertiesChanged will confirm/correct.
    m_activeProfile = profile;
    emit changed();
}
