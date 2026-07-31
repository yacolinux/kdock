// Which virtual desktop is current, so a strip can list only the windows on it.
//
// The plasma-window-management protocol reports per-window desktop ids but not
// which one is current, so this comes from KWin over D-Bus. Verified on KWin
// 6.7.4: org.kde.KWin /VirtualDesktopManager exposes `current` (a bare uuid
// string, no braces — the same form the protocol sends in
// virtual_desktop_entered) and a currentChanged(QString) signal.

#pragma once

#include <QObject>
#include <QString>

class VirtualDesktops : public QObject
{
    Q_OBJECT
public:
    explicit VirtualDesktops(QObject *parent = nullptr);

    // Empty when KWin is not reachable; callers then treat every window as
    // being on the current desktop (no filtering).
    QString current() const { return m_current; }

signals:
    void currentChanged();

private slots:
    // A slot (not a plain method) so the string-based bus.connect() below
    // resolves it.
    void setCurrent(const QString &id);

private:
    QString m_current;
};
