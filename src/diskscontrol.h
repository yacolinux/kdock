// Removable disks / mountable volumes via UDisks2 (system D-Bus), exposed to
// QML as "disks". Lists external filesystems (USB sticks, SD cards, external
// drives), reports their mount state and lets the user mount/unmount/eject and
// open the mount point in the file manager. A single shared instance is used by
// every dock. No extra library linkage: raw org.freedesktop.UDisks2 D-Bus.

#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>

class DisksControl : public QObject
{
    Q_OBJECT
    // Whether UDisks2 answered on the bus at least once.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // Number of listed removable volumes (drives the widget cares about).
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    explicit DisksControl(QObject *parent = nullptr);

    bool available() const { return m_available; }
    int count() const { return m_volumes.size(); }

    // Most-relevant first. Each element is a map with keys:
    //   path (block object path), drive (drive object path), label, device,
    //   mountPoint, mounted (bool), ejectable (bool), size (double bytes).
    Q_INVOKABLE QVariantList volumes() const { return m_volumes; }

    // Actions. path/drivePath are the object paths from a volumes() entry.
    Q_INVOKABLE void mount(const QString &path);
    Q_INVOKABLE void unmount(const QString &path);
    Q_INVOKABLE void eject(const QString &drivePath);
    // Open a mounted volume's mount point in the default file manager.
    Q_INVOKABLE void openMount(const QString &mountPoint);

signals:
    void changed();

private slots:
    void scheduleRescan();

private:
    void rescan();

    bool m_available = false;
    QVariantList m_volumes;
    QTimer m_rescanDebounce;
};
