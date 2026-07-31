// Network status & control via NetworkManager (system D-Bus), exposed to QML as
// "network". Reports the primary connection (name/type/wifi signal), lists saved
// connections and lets the user activate/deactivate them and toggle Wi-Fi. A
// single shared instance is used by every dock. No extra library linkage: raw
// org.freedesktop.NetworkManager D-Bus.
//
// Out of scope (needs a NM secret agent): joining a brand-new Wi-Fi network by
// typing a password. Activating an already-saved connection works.

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

public:
    explicit NetworkControl(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QString iconName() const { return m_iconName; }
    QString primaryName() const { return m_primaryName; }
    bool wifiEnabled() const { return m_wifiEnabled; }

    // Saved connections, active first. Each element is a map with keys:
    //   path (settings conn path), id, type, active (bool), activePath, wifi (bool).
    Q_INVOKABLE QVariantList connections() const;

    // Activate a saved connection (by its settings object path).
    Q_INVOKABLE void activate(const QString &connPath);
    // Deactivate an active connection (by its ActiveConnection object path).
    Q_INVOKABLE void deactivate(const QString &activePath);

    Q_INVOKABLE void setWifiEnabled(bool on);

signals:
    void changed();

private slots:
    void scheduleRescan();

private:
    void rescan();

    bool m_available = false;
    QString m_iconName = QStringLiteral("network-offline");
    QString m_primaryName;
    QString m_primaryType;
    bool m_wifiEnabled = false;
    int m_signal = 0; // 0..100 for the active wifi link
    QTimer m_rescanDebounce;
};
