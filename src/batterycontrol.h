// Battery state (UPower) and power profile (power-profiles-daemon), both over
// the D-Bus *system* bus. No KDE/Solid linkage: only Qt6::DBus.

#pragma once

#include <QObject>
#include <QStringList>

class QDBusInterface;

class BatteryControl : public QObject
{
    Q_OBJECT
    // Battery
    Q_PROPERTY(bool available READ available NOTIFY changed)   // a real battery exists
    Q_PROPERTY(int percentage READ percentage NOTIFY changed)
    Q_PROPERTY(bool charging READ charging NOTIFY changed)
    Q_PROPERTY(QString iconName READ iconName NOTIFY changed)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY changed)
    // Power profiles
    Q_PROPERTY(bool profilesAvailable READ profilesAvailable NOTIFY changed)
    Q_PROPERTY(QString activeProfile READ activeProfile NOTIFY changed)
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY changed)

public:
    explicit BatteryControl(QObject *parent = nullptr);

    bool available() const { return m_hasBattery; }
    int percentage() const { return m_percentage; }
    bool charging() const { return m_charging; }
    QString iconName() const;
    QString tooltipText() const;

    bool profilesAvailable() const { return m_profilesAvailable; }
    QString activeProfile() const { return m_activeProfile; }
    QStringList profiles() const { return m_profiles; }

    Q_INVOKABLE void setProfile(const QString &profile);

signals:
    void changed();

private slots:
    void refreshBattery();
    void refreshProfiles();

private:
    void setupProfiles();

    // UPower DisplayDevice
    QDBusInterface *m_battIface = nullptr;
    bool m_hasBattery = false;
    int m_percentage = 0;
    int m_state = 0;             // UPower: 1=charging 2=discharging 4=full 5=pending-charge
    bool m_charging = false;
    qlonglong m_timeToEmpty = 0; // seconds
    qlonglong m_timeToFull = 0;  // seconds
    QString m_upowerIcon;        // IconName reported by UPower (themed, symbolic)

    // power-profiles-daemon
    QDBusInterface *m_ppdIface = nullptr;
    QString m_ppdService;
    QString m_ppdPath;
    QString m_ppdInterface;
    bool m_profilesAvailable = false;
    QString m_activeProfile;
    QStringList m_profiles;
};
