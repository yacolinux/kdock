// The tiles of the section currently on screen, as a model.
//
// A model and not a plain QVariantList so that a drop is a dataChanged() on one
// row instead of a rebuild of the whole canvas: the moved tile animates to its
// new slot with a Behavior on x/y and nothing else on screen flickers.
//
// It joins two sources: TileLayout says *where* each tile sits, AppMenu says
// what the app is called and which icon it has.
//
// The menu shows one group at a time behind a tab bar, so the model holds the
// tiles of `currentGroup` only.

#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVariantList>

#include "tilelayout.h"

class AppMenu;
class TileConfig;

class TileModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString section READ section WRITE setSection NOTIFY sectionChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY queryChanged)
    // Rows the current group needs; the QML sizes the Flickable from it.
    Q_PROPERTY(int rows READ rows NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY layoutChanged)
    Q_PROPERTY(int currentGroup READ currentGroup WRITE setCurrentGroup NOTIFY currentGroupChanged)
    Q_PROPERTY(bool customized READ customized NOTIFY layoutChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        CommentRole,
        IconRole,
        FavoriteRole,
        GroupRole,
        ColRole,
        RowRole,
        WidthRole,
        HeightRole,
        BackgroundRole,
        ImageRole,
        ShowIconRole,
        ShowLabelRole,
    };

    TileModel(TileLayout *layout, AppMenu *menu, TileConfig *config, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString section() const { return m_section; }
    void setSection(const QString &section);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    bool searching() const { return !m_query.trimmed().isEmpty(); }
    int rows() const { return m_rows; }
    QVariantList groups() const { return m_groups; }
    int currentGroup() const { return m_currentGroup; }
    void setCurrentGroup(int group);
    bool customized() const { return m_customized; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap get(int row) const;
    // Row of the first tile whose name starts with `letter`, or -1. Only used
    // while the section is auto-arranged (see TileLayout::isCustomized).
    Q_INVOKABLE int indexOfLetter(const QString &letter) const;
    // Letters the current section actually has an app for, so the A-Z rail can
    // gray out the rest.
    Q_INVOKABLE QStringList availableLetters() const;

signals:
    void sectionChanged();
    void queryChanged();
    void currentGroupChanged();
    void layoutChanged();

private:
    // Metadata of every app in the current section, keyed by id.
    void reloadApps();
    // Search results are a plain auto grid: there is no section to persist a
    // hand-made position into.
    QList<TileRecord> searchPlacement() const;

    TileLayout *m_layout;
    AppMenu *m_menu;
    TileConfig *m_config;

    QString m_section;
    QString m_query;
    QList<TileRecord> m_tiles;
    QHash<QString, QVariantMap> m_apps;
    QVariantList m_groups;
    int m_currentGroup = 0;
    int m_rows = 0;
    bool m_customized = false;
};
