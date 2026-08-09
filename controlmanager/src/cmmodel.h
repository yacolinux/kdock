// The cards of the Principal tab, as a model.
//
// A model and not a plain QVariantList so that a drop is a dataChanged() on one
// row instead of a rebuild of the whole canvas: the moved card animates to its
// new slot with a Behavior on x/y and nothing else on screen flickers.
//
// It joins two sources: CmLayout says *where* each card sits, CmSections says
// what it is called and which icon it wears.

#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>

#include "cmlayout.h"

class CmConfig;

class CmModel : public QAbstractListModel
{
    Q_OBJECT
    // Rows the grid needs; the QML sizes the Flickable from it.
    Q_PROPERTY(int rows READ rows NOTIFY layoutChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        IconRole,
        ColRole,
        RowRole,
        WidthRole,
        HeightRole,
        BackgroundRole,
        ShowTitleRole,
    };

    CmModel(CmLayout *layout, CmConfig *config, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int rows() const { return m_rows; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void layoutChanged();

private:
    CmLayout *m_layout;
    CmConfig *m_config;
    QList<CmCardRecord> m_cards;
    int m_rows = 0;
};
