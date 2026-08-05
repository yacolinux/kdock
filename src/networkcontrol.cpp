#include "networkcontrol.h"

#include "nmdbus.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDebug>
#include <QUuid>

#include <tuple>

static QString wifiIconFor(int strength)
{
    const char *lvl = strength >= 80 ? "excellent"
                    : strength >= 55 ? "good"
                    : strength >= 30 ? "ok"
                    : strength >= 5  ? "weak"
                                     : "none";
    return QStringLiteral("network-wireless-signal-%1").arg(QLatin1String(lvl));
}

NetworkControl::NetworkControl(QObject *parent)
    : QObject(parent)
{
    NM::registerTypes();

    m_rescanDebounce.setSingleShot(true);
    m_rescanDebounce.setInterval(500);
    connect(&m_rescanDebounce, &QTimer::timeout, this, &NetworkControl::rescan);

    // Any NM property change (state, active connection, wifi toggle, signal
    // strength) surfaces as a PropertiesChanged on some object under the NM
    // service. Empty path = match any object.
    QDBusConnection::systemBus().connect(NM::Service, QString(), NM::PropsIface,
                                         QStringLiteral("PropertiesChanged"),
                                         this, SLOT(scheduleRescan()));

    rescan();
}

void NetworkControl::scheduleRescan()
{
    m_rescanDebounce.start();
}

void NetworkControl::rescan()
{
    const QVariantMap nm = NM::getAll(NM::Path, NM::Iface);
    if (nm.isEmpty()) {
        if (m_available) {
            m_available = false;
            m_iconName = QStringLiteral("network-offline");
            m_primaryName.clear();
            emit changed();
        }
        return;
    }

    m_available = true;
    m_wifiEnabled = nm.value(QStringLiteral("WirelessEnabled")).toBool();
    const bool networking = nm.value(QStringLiteral("NetworkingEnabled")).toBool();
    const uint state = nm.value(QStringLiteral("State")).toUInt();
    m_primaryType = nm.value(QStringLiteral("PrimaryConnectionType")).toString();

    const QString primaryActive =
        nm.value(QStringLiteral("PrimaryConnection")).value<QDBusObjectPath>().path();
    m_primaryName.clear();
    if (!primaryActive.isEmpty() && primaryActive != QLatin1String("/"))
        m_primaryName = NM::getProp(primaryActive, NM::ActiveIface, QStringLiteral("Id")).toString();

    // Locate the Wi-Fi device once per rescan; it also carries the active AP
    // (for the signal strength) and is the target of scans and new connections.
    m_wifiDevice.clear();
    m_activeApPath.clear();
    m_signal = 0;
    const QList<QDBusObjectPath> devices = NM::objectPaths(nm.value(QStringLiteral("Devices")));
    for (const QDBusObjectPath &d : devices) {
        if (NM::getProp(d.path(), NM::DeviceIface, QStringLiteral("DeviceType")).toUInt()
            != NM::DeviceWifi)
            continue;
        m_wifiDevice = d.path();
        const QString ap = NM::getProp(m_wifiDevice, NM::WirelessIface,
                                       QStringLiteral("ActiveAccessPoint"))
                               .value<QDBusObjectPath>().path();
        if (!ap.isEmpty() && ap != QLatin1String("/")) {
            m_activeApPath = ap;
            m_signal = NM::getProp(ap, NM::ApIface, QStringLiteral("Strength")).toInt();
        }
        break;
    }

    // A finished scan bumps LastScan (ms of CLOCK_BOOTTIME, -1 while scanning).
    if (!m_wifiDevice.isEmpty()) {
        const qlonglong last =
            NM::getProp(m_wifiDevice, NM::WirelessIface, QStringLiteral("LastScan")).toLongLong();
        if (m_scanning && last != m_lastScan) {
            m_scanning = false;
            emit scanningChanged();
        }
        m_lastScan = last;
    }

    // Derive the status icon.
    if (!networking || state <= 20) {
        m_iconName = QStringLiteral("network-offline");
    } else if (m_primaryType.contains(QLatin1String("wireless"))) {
        m_iconName = wifiIconFor(m_signal);
    } else if (!m_primaryType.isEmpty()) {
        m_iconName = QStringLiteral("network-wired-activated");
    } else {
        m_iconName = QStringLiteral("network-idle");
    }

    if (m_apTracking)
        refreshAccessPoints();

    emit changed();
}

QVariantList NetworkControl::connections() const
{
    QVariantList result;
    if (!m_available)
        return result;

    // Map each active connection's underlying settings path -> its active path,
    // so saved connections can be flagged active.
    QMap<QString, QString> activeBySettings;
    {
        const QList<QDBusObjectPath> actives =
            NM::objectPaths(NM::getProp(NM::Path, NM::Iface, QStringLiteral("ActiveConnections")));
        for (const QDBusObjectPath &a : actives) {
            const QString conn = NM::getProp(a.path(), NM::ActiveIface,
                                             QStringLiteral("Connection"))
                                     .value<QDBusObjectPath>().path();
            if (!conn.isEmpty())
                activeBySettings.insert(conn, a.path());
        }
    }

    // List saved connections and read id/type from each.
    QDBusMessage call = QDBusMessage::createMethodCall(
        NM::Service, NM::SettingsPath, NM::SettingsIface, QStringLiteral("ListConnections"));
    QDBusReply<QList<QDBusObjectPath>> reply = QDBusConnection::systemBus().call(call);
    if (!reply.isValid())
        return result;

    for (const QDBusObjectPath &connPath : reply.value()) {
        const NMSettingsMap settings = NM::connectionSettings(connPath.path());
        const QVariantMap conn = settings.value(QStringLiteral("connection"));
        const QString id = conn.value(QStringLiteral("id")).toString();
        const QString type = conn.value(QStringLiteral("type")).toString();
        if (id.isEmpty())
            continue;
        // NM keeps housekeeping connections (loopback, the libvirt bridge, tun
        // devices) alongside the real ones; they are noise in a dock popup.
        static const QStringList hidden = {QStringLiteral("loopback"), QStringLiteral("bridge"),
                                           QStringLiteral("generic"), QStringLiteral("tun"),
                                           QStringLiteral("dummy")};
        if (hidden.contains(type))
            continue;

        QVariantMap v;
        v[QStringLiteral("path")] = connPath.path();
        v[QStringLiteral("id")] = id;
        v[QStringLiteral("type")] = type;
        v[QStringLiteral("wifi")] = type.contains(QLatin1String("wireless"));
        const QString activePath = activeBySettings.value(connPath.path());
        v[QStringLiteral("active")] = !activePath.isEmpty();
        v[QStringLiteral("activePath")] = activePath;
        result.append(v);
    }

    // Active connections first, then alphabetical by id.
    std::sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap ma = a.toMap(), mb = b.toMap();
        if (ma.value(QStringLiteral("active")).toBool() != mb.value(QStringLiteral("active")).toBool())
            return ma.value(QStringLiteral("active")).toBool();
        return ma.value(QStringLiteral("id")).toString().localeAwareCompare(
                   mb.value(QStringLiteral("id")).toString()) < 0;
    });
    return result;
}

QVariantList NetworkControl::accessPoints() const
{
    return m_accessPoints;
}

QString NetworkControl::savedConnectionFor(const QString &ssid) const
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        NM::Service, NM::SettingsPath, NM::SettingsIface, QStringLiteral("ListConnections"));
    QDBusReply<QList<QDBusObjectPath>> reply = QDBusConnection::systemBus().call(call);
    if (!reply.isValid())
        return {};
    for (const QDBusObjectPath &connPath : reply.value()) {
        const NMSettingsMap settings = NM::connectionSettings(connPath.path());
        const QVariantMap wireless = settings.value(QStringLiteral("802-11-wireless"));
        if (wireless.isEmpty())
            continue;
        if (NM::ssidToString(wireless.value(QStringLiteral("ssid")).toByteArray()) == ssid)
            return connPath.path();
    }
    return {};
}

void NetworkControl::refreshAccessPoints()
{
    m_accessPoints.clear();
    if (m_wifiDevice.isEmpty() || !m_wifiEnabled)
        return;

    // One ListConnections pass for the whole AP list instead of one per SSID.
    QMap<QString, QString> savedBySsid;
    {
        QDBusMessage call = QDBusMessage::createMethodCall(
            NM::Service, NM::SettingsPath, NM::SettingsIface, QStringLiteral("ListConnections"));
        QDBusReply<QList<QDBusObjectPath>> reply = QDBusConnection::systemBus().call(call);
        if (reply.isValid()) {
            for (const QDBusObjectPath &connPath : reply.value()) {
                const NMSettingsMap settings = NM::connectionSettings(connPath.path());
                const QVariantMap wireless = settings.value(QStringLiteral("802-11-wireless"));
                if (wireless.isEmpty())
                    continue;
                const QString ssid =
                    NM::ssidToString(wireless.value(QStringLiteral("ssid")).toByteArray());
                if (!ssid.isEmpty() && !savedBySsid.contains(ssid))
                    savedBySsid.insert(ssid, connPath.path());
            }
        }
    }

    const QList<QDBusObjectPath> aps =
        NM::objectPaths(NM::getProp(m_wifiDevice, NM::WirelessIface,
                                    QStringLiteral("AccessPoints")));

    QMap<QString, QVariantMap> bySsid; // strongest wins
    for (const QDBusObjectPath &apPath : aps) {
        const QVariantMap ap = NM::getAll(apPath.path(), NM::ApIface);
        if (ap.isEmpty())
            continue;
        const QString ssid = NM::ssidToString(ap.value(QStringLiteral("Ssid")).toByteArray());
        if (ssid.isEmpty())
            continue; // hidden network

        const uint flags = ap.value(QStringLiteral("Flags")).toUInt();
        const uint wpa = ap.value(QStringLiteral("WpaFlags")).toUInt();
        const uint rsn = ap.value(QStringLiteral("RsnFlags")).toUInt();
        const int strength = ap.value(QStringLiteral("Strength")).toInt();
        const uint freq = ap.value(QStringLiteral("Frequency")).toUInt();

        const QVariantMap existing = bySsid.value(ssid);
        if (!existing.isEmpty() && existing.value(QStringLiteral("strength")).toInt() >= strength)
            continue;

        const QString security = NM::apSecurityLabel(flags, wpa, rsn);
        QVariantMap v;
        v[QStringLiteral("ssid")] = ssid;
        v[QStringLiteral("strength")] = strength;
        v[QStringLiteral("security")] = security;
        v[QStringLiteral("secure")] = !security.isEmpty();
        v[QStringLiteral("path")] = apPath.path();
        v[QStringLiteral("active")] = apPath.path() == m_activeApPath;
        v[QStringLiteral("connPath")] = savedBySsid.value(ssid);
        v[QStringLiteral("saved")] = savedBySsid.contains(ssid);
        v[QStringLiteral("band")] = freq >= 5900 ? QStringLiteral("6")
                                   : freq >= 4900 ? QStringLiteral("5")
                                                  : QStringLiteral("2.4");
        bySsid.insert(ssid, v);
    }

    m_accessPoints.reserve(bySsid.size());
    for (const QVariantMap &v : std::as_const(bySsid))
        m_accessPoints.append(v);

    // Strength is bucketed (and saved networks float up) on purpose: sorting by
    // the raw value reorders the list on every rescan, and the popup refreshes
    // twice a second — rows would jump out from under the pointer mid-click.
    std::sort(m_accessPoints.begin(), m_accessPoints.end(),
              [](const QVariant &a, const QVariant &b) {
                  const QVariantMap ma = a.toMap(), mb = b.toMap();
                  const auto key = [](const QVariantMap &m) {
                      return std::make_tuple(!m.value(QStringLiteral("active")).toBool(),
                                             !m.value(QStringLiteral("saved")).toBool(),
                                             -m.value(QStringLiteral("strength")).toInt() / 20,
                                             m.value(QStringLiteral("ssid")).toString());
                  };
                  return key(ma) < key(mb);
              });
}

void NetworkControl::setApTrackingEnabled(bool on)
{
    if (m_apTracking == on)
        return;
    m_apTracking = on;
    if (on) {
        refreshAccessPoints();
        emit changed();
    } else {
        m_accessPoints.clear();
    }
}

void NetworkControl::requestScan()
{
    if (m_wifiDevice.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(NM::Service, m_wifiDevice,
                                                       NM::WirelessIface,
                                                       QStringLiteral("RequestScan"));
    call.setArguments({QVariant::fromValue(QVariantMap())});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (reply.isError()) {
            // NM refuses a scan that is already running; that is not worth a
            // message to the user.
            m_scanning = false;
            emit scanningChanged();
            return;
        }
    });
    if (!m_scanning) {
        m_scanning = true;
        emit scanningChanged();
    }
}

void NetworkControl::activate(const QString &connPath)
{
    if (connPath.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(NM::Service, NM::Path, NM::Iface,
                                                       QStringLiteral("ActivateConnection"));
    call.setArguments({QVariant::fromValue(QDBusObjectPath(connPath)),
                       QVariant::fromValue(QDBusObjectPath(QStringLiteral("/"))),
                       QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")))});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<QDBusObjectPath> reply = *w;
        w->deleteLater();
        if (reply.isError())
            emit errorOccurred(reply.error().message());
        scheduleRescan();
    });
}

void NetworkControl::deactivate(const QString &activePath)
{
    if (activePath.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(NM::Service, NM::Path, NM::Iface,
                                                       QStringLiteral("DeactivateConnection"));
    call.setArguments({QVariant::fromValue(QDBusObjectPath(activePath))});
    QDBusConnection::systemBus().call(call, QDBus::NoBlock);
    scheduleRescan();
}

void NetworkControl::connectToAccessPoint(const QString &ssid, const QString &password,
                                          const QString &apPath)
{
    if (ssid.isEmpty() || m_wifiDevice.isEmpty())
        return;

    // Already saved: activating it reuses the stored secret, whoever owns it.
    const QString saved = savedConnectionFor(ssid);
    if (!saved.isEmpty()) {
        activate(saved);
        return;
    }

    QVariantMap connection;
    connection[QStringLiteral("id")] = ssid;
    connection[QStringLiteral("type")] = QStringLiteral("802-11-wireless");
    connection[QStringLiteral("uuid")] =
        QUuid::createUuid().toString(QUuid::WithoutBraces);

    QVariantMap wireless;
    wireless[QStringLiteral("ssid")] = NM::ssidToBytes(ssid);
    wireless[QStringLiteral("mode")] = QStringLiteral("infrastructure");

    NMSettingsMap settings;
    settings[QStringLiteral("connection")] = connection;
    settings[QStringLiteral("802-11-wireless")] = wireless;
    settings[QStringLiteral("ipv4")] = {{QStringLiteral("method"), QStringLiteral("auto")}};
    settings[QStringLiteral("ipv6")] = {{QStringLiteral("method"), QStringLiteral("auto")}};

    if (!password.isEmpty()) {
        // Read the AP's own flags so WPA3-only networks get key-mgmt=sae and
        // WEP ones get a wep-key instead of a psk.
        QString keyMgmt = QStringLiteral("wpa-psk");
        if (!apPath.isEmpty()) {
            const QVariantMap ap = NM::getAll(apPath, NM::ApIface);
            const QString detected = NM::apKeyMgmt(ap.value(QStringLiteral("Flags")).toUInt(),
                                                   ap.value(QStringLiteral("WpaFlags")).toUInt(),
                                                   ap.value(QStringLiteral("RsnFlags")).toUInt());
            if (!detected.isEmpty())
                keyMgmt = detected;
        }
        QVariantMap security;
        security[QStringLiteral("key-mgmt")] = keyMgmt;
        if (keyMgmt == QLatin1String("none")) { // WEP
            security[QStringLiteral("wep-key0")] = password;
            security[QStringLiteral("wep-key-type")] = 2u; // passphrase
            security[QStringLiteral("auth-alg")] = QStringLiteral("open");
        } else {
            security[QStringLiteral("psk")] = password;
            // 0 = NM stores the secret itself, so no secret agent is involved.
            security[QStringLiteral("psk-flags")] = 0u;
        }
        settings[QStringLiteral("802-11-wireless-security")] = security;
        wireless[QStringLiteral("security")] = QStringLiteral("802-11-wireless-security");
        settings[QStringLiteral("802-11-wireless")] = wireless;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(
        NM::Service, NM::Path, NM::Iface, QStringLiteral("AddAndActivateConnection"));
    call.setArguments({QVariant::fromValue(settings),
                       QVariant::fromValue(QDBusObjectPath(m_wifiDevice)),
                       QVariant::fromValue(QDBusObjectPath(apPath.isEmpty()
                                                               ? QStringLiteral("/")
                                                               : apPath))});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<QDBusObjectPath, QDBusObjectPath> reply = *w;
        w->deleteLater();
        if (reply.isError())
            emit errorOccurred(reply.error().message());
        scheduleRescan();
    });
}

void NetworkControl::forgetConnection(const QString &connPath)
{
    if (connPath.isEmpty())
        return;
    const QString error = NM::callMethod(connPath, NM::ConnIface, QStringLiteral("Delete"), {});
    if (!error.isEmpty())
        emit errorOccurred(error);
    scheduleRescan();
}

void NetworkControl::setWifiEnabled(bool on)
{
    QDBusMessage call = QDBusMessage::createMethodCall(NM::Service, NM::Path, NM::PropsIface,
                                                       QStringLiteral("Set"));
    call.setArguments({NM::Iface, QStringLiteral("WirelessEnabled"),
                       QVariant::fromValue(QDBusVariant(on))});
    QDBusConnection::systemBus().call(call, QDBus::NoBlock);
    scheduleRescan();
}
