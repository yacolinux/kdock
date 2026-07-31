// Manages the collection of Relanzadores.
// Exposed to QML as "relanzadores".

#pragma once

#include <QObject>
#include <QSettings>
#include <QQmlListProperty>
#include <QMap>

class QAbstractListModel;
class RelanzadorConfig;
class RelanzadorModel;
class DesktopEntryIndex;

class RelanzadoresManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQmlListProperty<RelanzadorConfig> items READ items NOTIFY itemsChanged)
    Q_PROPERTY(int count READ count NOTIFY itemsChanged)

public:
    explicit RelanzadoresManager(DesktopEntryIndex *apps, QObject *parent = nullptr);

    QQmlListProperty<RelanzadorConfig> items();
    int count() const;

    Q_INVOKABLE RelanzadorConfig* createRelanzador(const QString &title);
    Q_INVOKABLE void removeRelanzador(const QString &id);
    Q_INVOKABLE RelanzadorConfig* get(const QString &id) const;
    Q_INVOKABLE RelanzadorConfig* getByIndex(int index) const;
    Q_INVOKABLE QAbstractListModel* getModel(const QString &id);
    QStringList ids() const;

    // Ids to show on a dock given its per-dock config lists. The primary dock
    // uses `hidden` (default all shown); others use `shown` (default none).
    Q_INVOKABLE QStringList visibleIds(bool primary, const QStringList &hidden,
                                       const QStringList &shown) const;

    DesktopEntryIndex* apps() const { return m_apps; }

signals:
    void itemsChanged();

private:
    void load();
    void saveIds();
    QString generateId();

    // QQmlListProperty callbacks
    static qsizetype itemCount(QQmlListProperty<RelanzadorConfig> *prop);
    static RelanzadorConfig *itemAt(QQmlListProperty<RelanzadorConfig> *prop, qsizetype index);

    QSettings m_settings;
    DesktopEntryIndex *m_apps;
    QList<RelanzadorConfig*> m_items;
    QMap<QString, RelanzadorModel*> m_models;
};