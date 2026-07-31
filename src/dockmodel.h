// The dock's item model: pinned launchers merged with running windows,
// grouped per application (matched via desktop entries).

#pragma once

#include <QAbstractListModel>
#include <QList>

#include "desktopentry.h"

class DockConfig;
class WindowMonitor;
class AbstractWindow;

class DockModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IconNameRole,
        PinnedRole,
        WindowCountRole,
        ActiveRole,
        MinimizedRole,
        IsSeparatorRole,
        TitleRole,
    };

    DockModel(DockConfig *config, DesktopEntryIndex *apps, WindowMonitor *monitor,
              QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void activate(int row);     // left click: launch / focus / cycle / minimize
    Q_INVOKABLE void launch(int row);       // always start a new instance
    Q_INVOKABLE void closeAll(int row);
    Q_INVOKABLE void togglePinned(int row);
    Q_INVOKABLE QVariantList windowList(int row) const;
    Q_INVOKABLE void activateWindow(int row, int windowIndex);
    Q_INVOKABLE void moveItem(int from, int to); // drag-and-drop reordering
    Q_INVOKABLE void syncWindows();               // deferred re-sync after startup
    // Every name the apps block draws (separators have none), for Dock.qml's
    // widest-label measurement. It asks the model instead of the Repeater
    // because the Repeater's id lives inside another component scope and is not
    // reachable from the QML root (bug 2026-07-30).
    Q_INVOKABLE QStringList labelStrings() const;

private:
    struct Item
    {
        QString key; // grouping key (lowercase desktop id or app_id)
        DesktopEntry entry;
        QString fallbackAppId;
        bool pinned = false;
        QList<AbstractWindow *> windows;
        bool isSeparator = false;
        int separatorIndex = 0; // 1 or 2

        QString displayName() const;
        QString iconName() const;
    };

    void rebuild();
    void placeWindow(AbstractWindow *window);
    void removeWindow(AbstractWindow *window);
    void windowChanged(AbstractWindow *window);
    QString keyForAppId(const QString &appId, DesktopEntry *outEntry) const;
    int rowOfKey(const QString &key) const;
    int rowOfWindow(AbstractWindow *window) const;
    QString keyForWindow(AbstractWindow *w) const;

    DockConfig *m_config;
    DesktopEntryIndex *m_apps;
    WindowMonitor *m_monitor;
    QList<Item> m_items;
    bool m_updatingPinned = false;
};
