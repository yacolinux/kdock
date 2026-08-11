// Network status & control via NetworkManager (system D-Bus), exposed to QML as
// "network". Reports the primary connection (name/type/wifi signal), lists saved
// connections and nearby access points, and lets the user activate/deactivate
// connections, join a new Wi-Fi network by typing its password, forget one, and
// toggle Wi-Fi. A single shared instance is used by every dock. No extra library
// linkage: raw org.freedesktop.NetworkManager D-Bus (see nmdbus.h).
//
// Joining a new network does not need an NM secret agent: the password travels
// inline in AddAndActivateConnection with psk-flags=0, so NetworkManager stores
// the secret itself (this is what `nmcli device wifi connect` does).
//
// Creating/editing connections in full (static IP, DNS, routes, Ethernet) lives
// in the Settings → Redes tab instead; see networksettings.h.

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

class NetworkControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // Themed icon name reflecting current state (wired/wifi signal/offline).
    Q_PROPERTY(QString iconName READ iconName NOTIFY changed)
    // Id (SSID / connection name) of the primary active connection, if any.
    Q_PROPERTY(QString primaryName READ primaryName NOTIFY changed)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled WRITE setWifiEnabled NOTIFY changed)
    // Whether the machine has a Wi-Fi device at all (no device: the popup drops
    // the whole "nearby networks" half).
    Q_PROPERTY(bool wifiAvailable READ wifiAvailable NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)

public:
    explicit NetworkControl(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QString iconName() const { return m_iconName; }
    QString primaryName() const { return m_primaryName; }
    bool wifiEnabled() const { return m_wifiEnabled; }
    bool wifiAvailable() const { return !m_wifiDevice.isEmpty(); }
    bool scanning() const { return m_scanning; }

    // Saved connections, active first. Each element is a map with keys:
    //   path (settings conn path), id, type, active (bool), activePath, wifi (bool).
    Q_INVOKABLE QVariantList connections() const;

    // Access points seen by the Wi-Fi device, strongest first, deduplicated by
    // SSID. Each element: ssid, strength (0-100), security (label, empty when
    // open), secure (bool), saved (bool), active (bool), path (AP object),
    // connPath (saved connection, when saved), band ("2.4"/"5"/"6").
    // Empty unless access-point tracking is on (see setApTrackingEnabled).
    Q_INVOKABLE QVariantList accessPoints() const;

    // Live IP data of every managed device that has an active connection, the
    // "what is my address" half of the widget. Each element:
    //   iface, typeLabel, mac, stateLabel, connection (active connection id),
    //   wifi (bool), ip4 (first address), mask (ip4 prefix as a netmask),
    //   prefix (int), gateway, dns (comma-joined), ip6, extraIp4 (the further
    //   IPv4 addresses of the device, comma-joined; usually empty).
    // Several D-Bus round trips per device, so it is a call and not a property:
    // only the view that shows it pays for it (same deal as accessPoints()).
    Q_INVOKABLE QVariantList deviceDetails() const;

    // Activate a saved connection (by its settings object path).
    Q_INVOKABLE void activate(const QString &connPath);
    // Deactivate an active connection (by its ActiveConnection object path).
    Q_INVOKABLE void deactivate(const QString &activePath);

    // Ask the Wi-Fi device for a fresh scan. Results arrive through changed().
    Q_INVOKABLE void requestScan();

    // Join a network. An empty password means "open network"; a network that is
    // already saved is simply activated and the password is ignored.
    Q_INVOKABLE void connectToAccessPoint(const QString &ssid, const QString &password,
                                          const QString &apPath);
    // Delete a saved connection.
    Q_INVOKABLE void forgetConnection(const QString &connPath);

    // Enumerating access points costs one D-Bus round trip per AP, so it only
    // happens while the popup that shows them is open.
    Q_INVOKABLE void setApTrackingEnabled(bool on);

    Q_INVOKABLE void setWifiEnabled(bool on);

signals:
    void changed();
    void scanningChanged();
    // Something the user asked for failed (bad password, no authorization…).
    void errorOccurred(const QString &message);

private slots:
    void scheduleRescan();

private:
    void rescan();
    void refreshAccessPoints();
    // Settings path of a saved Wi-Fi connection for this SSID, if any.
    QString savedConnectionFor(const QString &ssid) const;

    bool m_available = false;
    QString m_iconName = QStringLiteral("network-offline");
    QString m_primaryName;
    QString m_primaryType;
    bool m_wifiEnabled = false;
    int m_signal = 0; // 0..100 for the active wifi link
    QTimer m_rescanDebounce;

    QString m_wifiDevice;    // object path of the first Wi-Fi device
    QString m_activeApPath;  // AP the Wi-Fi device is on
    bool m_apTracking = false;
    bool m_scanning = false;
    qlonglong m_lastScan = -1;
    QVariantList m_accessPoints;
};
