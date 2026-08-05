// NetworkManager connection management for the Settings → Redes tab: devices,
// saved connections, and the conversion between NM's D-Bus settings map and the
// plain structs the editor forms bind to. Widget-free and QML-free on purpose,
// so a console probe can round-trip a connection without any GUI.
//
// Everything speaks raw D-Bus through nmdbus.h. Modifying system connections
// needs the polkit action settings.modify.system, which is granted to active
// local sessions (checked with `nmcli general permissions`).

#pragma once

#include "nmdbus.h"

#include <QObject>
#include <QString>
#include <QStringList>

class NetworkSettings : public QObject
{
    Q_OBJECT
public:
    explicit NetworkSettings(QObject *parent = nullptr);

    struct Device {
        QString path;
        QString iface;
        QString driver;
        QString hwAddress;
        uint type = 0;  // NM::DeviceType
        uint state = 0; // NM::DeviceState
        QString activeConnPath;
        bool isWifi() const { return type == NM::DeviceWifi; }
    };

    struct Connection {
        QString path;
        QString uuid;
        QString id;
        QString type;  // "802-11-wireless" / "802-3-ethernet"
        QString iface; // connection.interface-name, may be empty
        bool autoconnect = true;
        bool active = false;
        QString activePath;
        bool isWifi() const { return type == QLatin1String("802-11-wireless"); }
    };

    // One entry of ipv4/ipv6 address-data or route-data.
    struct IpAddress {
        QString address;
        int prefix = 24;
    };
    struct IpRoute {
        QString dest;
        int prefix = 24;
        QString nextHop;
        int metric = -1; // -1: let NM decide
    };

    // The subset of an ipv4/ipv6 setting group the editor exposes.
    struct IpConfig {
        QString method = QStringLiteral("auto");
        QList<IpAddress> addresses;
        QString gateway;
        QStringList dns;
        QStringList searchDomains;
        QList<IpRoute> routes;
        bool ignoreAutoDns = false;
        bool ignoreAutoRoutes = false;
        bool neverDefault = false;
        bool mayFail = true;
    };

    QList<Device> devices() const;
    QList<Connection> connections() const;

    NMSettingsMap settings(const QString &connPath) const;
    // Secrets live outside GetSettings; group is e.g. "802-11-wireless-security".
    NMSettingsMap secrets(const QString &connPath, const QString &group) const;

    // All return an empty string on success, the D-Bus error message otherwise.
    QString addConnection(const NMSettingsMap &settings, QString *newPath = nullptr);
    QString updateConnection(const QString &connPath, const NMSettingsMap &settings);
    QString deleteConnection(const QString &connPath);
    QString activate(const QString &connPath, const QString &devicePath);
    QString deactivate(const QString &activePath);

    // Current addresses/DNS of a device, for the read-only device page.
    QStringList deviceIpSummary(const Device &device) const;

    // --- pure conversions (unit-testable without D-Bus) --------------------
    static IpConfig ipFromSettings(const QVariantMap &group);
    // v6 only changes which legacy keys get stripped; the written keys are the
    // modern address-data/route-data/dns-data in both cases.
    static QVariantMap ipToSettings(const IpConfig &config, bool v6);

    // Human-readable device state and type.
    static QString stateLabel(uint state);
    static QString typeLabel(uint type);

signals:
    // A connection was added/removed/updated, or a device changed state.
    void changed();

private:
    QString m_lastError;
};
