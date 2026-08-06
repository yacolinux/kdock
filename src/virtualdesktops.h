// KWin's virtual desktops, read off org.kde.KWin /VirtualDesktopManager: how
// many there are, which one is current, and their names. Process-wide singleton
// shared by every dock (it lives in DockManager::Shared).
//
// Positions are exposed **1-based** ("Escritorio 1" == 1) to match the numbers
// the user sees; KWin's own `desktops` property counts from 0. A position of 0
// means "no answer": X11, a wlroots compositor, or the Xvfb harness, where the
// dock must behave exactly as it did before this class existed.

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

class VirtualDesktops : public QObject
{
    Q_OBJECT
    // Exposed to QML as the `virtualDesktops` context property, for the pager
    // widget and the app icons' "send to desktop" menu.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int current READ currentPosition NOTIFY currentChanged)
    Q_PROPERTY(QStringList names READ names NOTIFY countChanged)

public:
    explicit VirtualDesktops(QObject *parent = nullptr);

    // Number of desktops KWin reports, 0 when it is not reachable.
    int count() const { return m_names.size(); }
    // 1-based position of the current desktop, 0 when unknown.
    int currentPosition() const { return m_current; }
    // Desktop names in position order (index 0 is desktop 1).
    QStringList names() const { return m_names; }
    // Name of a 1-based position, or an empty string when it doesn't exist.
    Q_INVOKABLE QString nameOf(int position) const;

    // Switch the session to a 1-based position. No-op for a position that
    // doesn't exist, so a stale pager button cannot throw.
    Q_INVOKABLE void switchTo(int position);

    // Ordered uuids, for the callers that talk to KWin in uuid terms
    // (ActiveWindowControl and DockModel move windows between desktops).
    QStringList desktopIds() const { return m_ids; }
    QString currentDesktopId() const;
    // uuid of a 1-based position, empty when it doesn't exist.
    QString idOf(int position) const;

signals:
    void currentChanged(int position);
    void countChanged();

private slots:
    // Wired to KWin's D-Bus signals, so they must be slots: QDBusConnection
    // only offers the SLOT() form of connect().
    void refreshDesktops();
    void onCurrentChanged(const QString &id);

private:
    // One property off /VirtualDesktopManager, read through
    // org.freedesktop.DBus.Properties.Get rather than
    // QDBusInterface::property(): `desktops` is a struct array that was never
    // registered with qDBusRegisterMetaType, and QDBusInterface would return an
    // invalid QVariant after only a warning on stderr (see the .cpp).
    static QVariant desktopProperty(const char *name);
    void refreshCurrent();

    QStringList m_ids;    // uuids, position order
    QStringList m_names;  // names, position order
    QString m_currentId;
    int m_current = 0;    // 1-based, 0 = unknown
};
