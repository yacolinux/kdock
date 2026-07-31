// Simplified model for a Relanzador's items (launchers only).
// Unlike DockModel, this does not track running windows.

#pragma once

#include <QAbstractListModel>
#include <QList>

#include "desktopentry.h"

class RelanzadorConfig;

class RelanzadorModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IconNameRole,
        DesktopIdRole,
        IsSubRelanzadorRole,
    };

    explicit RelanzadorModel(RelanzadorConfig *config, DesktopEntryIndex *apps, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void launch(int row);
    Q_INVOKABLE void moveItem(int from, int to);
    Q_INVOKABLE void addItem(const QString &desktopId);
    Q_INVOKABLE void removeItem(int row);

private:
    struct Item {
        QString desktopId;
        DesktopEntry entry;
        bool isSubRelanzador = false;
    };

    void rebuild();
    void saveOrder();

    RelanzadorConfig *m_config;
    DesktopEntryIndex *m_apps;
    QList<Item> m_items;
};