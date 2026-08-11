#include "cmmodel.h"

#include "cmconfig.h"
#include "cmsections.h"

#include <QVariantMap>

CmModel::CmModel(CmLayout *layout, CmConfig *config, QObject *parent)
    : QAbstractListModel(parent)
    , m_layout(layout)
    , m_config(config)
{
    connect(m_layout, &CmLayout::changed, this, &CmModel::refresh);
    // The column count and the cell size come from the config, and both change
    // where every card lands.
    connect(m_config, &CmConfig::settingsChanged, this, &CmModel::refresh);
    connect(m_config, &CmConfig::sectionsChanged, this, &CmModel::refresh);
    refresh();
}

int CmModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_cards.size();
}

QHash<int, QByteArray> CmModel::roleNames() const
{
    return {
        {IdRole, "cardId"},   {NameRole, "name"},   {IconRole, "icon"},
        {ColRole, "col"},     {RowRole, "row"},     {WidthRole, "span"},
        {HeightRole, "vspan"}, {BackgroundRole, "background"},
        {ForegroundRole, "foreground"}, {ShowTitleRole, "showTitle"},
    };
}

QVariant CmModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_cards.size())
        return {};
    const CmCardRecord &c = m_cards.at(index.row());

    switch (role) {
    case IdRole:
        return c.id;
    case NameRole:
        // A per-card rename wins over the section's own name.
        return !c.label.isEmpty() ? c.label : CmSections::label(c.id);
    case IconRole:
        return CmSections::byId(c.id).icon;
    case ColRole:
        return c.col;
    case RowRole:
        return c.row;
    case WidthRole:
        return c.w;
    case HeightRole:
        return c.h;
    case BackgroundRole:
        return c.bg;
    case ForegroundRole:
        // Empty = no override; CmCard.qml picks the contrast pair itself.
        return c.fg;
    case ShowTitleRole:
        // -1 = follow the global switch.
        return c.showTitle < 0 ? m_config->showCardTitles() : c.showTitle == 1;
    }
    return {};
}

void CmModel::refresh()
{
    const QList<CmCardRecord> next = m_layout->placement();

    // Same cards in the same order (the common case: a move, a resize, a colour
    // change) — update in place so the QML delegates survive and animate.
    bool sameIds = next.size() == m_cards.size();
    if (sameIds) {
        for (int i = 0; i < next.size(); ++i) {
            if (next.at(i).id != m_cards.at(i).id) {
                sameIds = false;
                break;
            }
        }
    }

    if (sameIds) {
        m_cards = next;
        if (!m_cards.isEmpty())
            emit dataChanged(index(0), index(m_cards.size() - 1));
    } else {
        beginResetModel();
        m_cards = next;
        endResetModel();
    }

    m_rows = m_layout->rows();
    emit layoutChanged();
}

QVariantMap CmModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_cards.size())
        return out;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        out.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    return out;
}
