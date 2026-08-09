#include "cmlayout.h"

#include "cmconfig.h"
#include "cmsections.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QVariant>

namespace {
// Nothing sensible needs a card bigger than this, and the cap keeps a corrupt
// config from asking for a canvas of a million rows.
constexpr int kMaxSpan = 8;
// firstFreeSlot() always terminates (rows grow without bound), but a bug in the
// overlap test would otherwise spin forever.
constexpr int kMaxScanRows = 512;

int indexOfCard(const QList<CmCardRecord> &cards, const QString &id)
{
    for (int i = 0; i < cards.size(); ++i) {
        if (cards.at(i).id == id)
            return i;
    }
    return -1;
}

inline quint64 packCell(int col, int row)
{
    return (quint64(quint32(col)) << 32) | quint32(row);
}

QSet<quint64> occupancyOf(const QList<CmCardRecord> &cards, const QString &ignoreId)
{
    QSet<quint64> taken;
    for (const CmCardRecord &c : cards) {
        if (c.id == ignoreId)
            continue;
        for (int x = c.col; x < c.col + c.w; ++x) {
            for (int y = c.row; y < c.row + c.h; ++y)
                taken.insert(packCell(x, y));
        }
    }
    return taken;
}

bool cellsFree(const QSet<quint64> &taken, int col, int row, int w, int h)
{
    for (int x = col; x < col + w; ++x) {
        for (int y = row; y < row + h; ++y) {
            if (taken.contains(packCell(x, y)))
                return false;
        }
    }
    return true;
}

QJsonObject cardToJson(const CmCardRecord &r)
{
    QJsonObject o;
    o[QStringLiteral("id")] = r.id;
    o[QStringLiteral("c")] = r.col;
    o[QStringLiteral("r")] = r.row;
    o[QStringLiteral("w")] = r.w;
    o[QStringLiteral("h")] = r.h;
    if (!r.bg.isEmpty())
        o[QStringLiteral("bg")] = r.bg;
    if (!r.label.isEmpty())
        o[QStringLiteral("lbl")] = r.label;
    if (r.showTitle >= 0)
        o[QStringLiteral("st")] = r.showTitle;
    return o;
}

CmCardRecord cardFromJson(const QJsonObject &o)
{
    CmCardRecord r;
    r.id = o.value(QStringLiteral("id")).toString();
    r.col = o.value(QStringLiteral("c")).toInt();
    r.row = o.value(QStringLiteral("r")).toInt();
    r.w = qBound(1, o.value(QStringLiteral("w")).toInt(2), kMaxSpan);
    r.h = qBound(1, o.value(QStringLiteral("h")).toInt(2), kMaxSpan);
    r.bg = o.value(QStringLiteral("bg")).toString();
    r.label = o.value(QStringLiteral("lbl")).toString();
    r.showTitle = o.contains(QStringLiteral("st")) ? o.value(QStringLiteral("st")).toInt() : -1;
    return r;
}

QString dumpCode(int index)
{
    return QString(QChar('A' + (index % 26)));
}
} // namespace

CmLayout::CmLayout(CmConfig *config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    load();
    // Turning a section on or off changes which cards are drawn, which changes
    // the grid; the records themselves are untouched.
    connect(m_config, &CmConfig::sectionsChanged, this, &CmLayout::changed);
}

int CmLayout::columns() const
{
    const int configured = m_config->columns();
    return configured > 0 ? configured : qMax(1, m_autoColumns);
}

void CmLayout::setAutoColumns(int columns)
{
    columns = qBound(1, columns, 24);
    if (m_autoColumns == columns)
        return;
    m_autoColumns = columns;
    // Only meaningful while the column count is derived from the width; with a
    // fixed count this changes nothing and re-laying out would be noise.
    if (m_config->columns() == 0)
        emit changed();
}

QList<CmCardRecord> CmLayout::placement() const
{
    const QStringList live = m_config->principalCards();
    const int cols = columns();

    QList<CmCardRecord> out;
    QSet<QString> done;

    // 1. Stored records whose section is on Principal right now. Records of
    //    sections that are off are skipped here but kept in m_cards, so turning
    //    one back on recovers its slot.
    for (const CmCardRecord &r : m_cards) {
        if (!live.contains(r.id) || done.contains(r.id))
            continue;
        CmCardRecord copy = r;
        copy.w = qBound(1, qMin(copy.w, cols), kMaxSpan);
        copy.h = qBound(1, copy.h, kMaxSpan);
        copy.col = qBound(0, copy.col, qMax(0, cols - copy.w));
        copy.row = qMax(0, copy.row);
        out.append(copy);
        done.insert(r.id);
    }

    // 2. Cards the user has never placed: first free slot, at the size the
    //    section table asks for. There are at most a dozen of them, so a plain
    //    per-card scan is fine (the tile menu needed a cursor because it places
    //    ~500).
    for (const QString &id : live) {
        if (done.contains(id) || !CmSections::exists(id))
            continue;
        const CmSectionInfo info = CmSections::byId(id);
        CmCardRecord r;
        r.id = id;
        r.w = qBound(1, qMin(info.defaultW, cols), kMaxSpan);
        r.h = qBound(1, info.defaultH, kMaxSpan);
        firstFreeSlot(out, cols, r.w, r.h, QString(), &r.col, &r.row);
        out.append(r);
        done.insert(id);
    }

    return out;
}

int CmLayout::rows() const
{
    int bottom = 0;
    const QList<CmCardRecord> cards = placement();
    for (const CmCardRecord &c : cards)
        bottom = qMax(bottom, c.row + c.h);
    return qMax(1, bottom);
}

int CmLayout::dropKind(const QString &id, int col, int row) const
{
    const QList<CmCardRecord> cards = placement();
    const int idx = indexOfCard(cards, id);
    if (idx < 0)
        return Refused;

    const int cols = columns();
    CmCardRecord to = cards.at(idx);
    to.col = qBound(0, col, qMax(0, cols - to.w));
    to.row = qMax(0, row);

    int hits = 0;
    int hit = -1;
    for (int i = 0; i < cards.size(); ++i) {
        if (i == idx)
            continue;
        if (cards.at(i).overlaps(to)) {
            ++hits;
            hit = i;
        }
    }
    if (hits == 0)
        return Free;
    if (hits == 1 && cards.at(hit).w == to.w && cards.at(hit).h == to.h)
        return Swap;
    return Refused;
}

bool CmLayout::fits(const QList<CmCardRecord> &placed, const CmCardRecord &candidate,
                    const QString &ignoreId)
{
    for (const CmCardRecord &other : placed) {
        if (other.id == ignoreId || other.id == candidate.id)
            continue;
        if (other.overlaps(candidate))
            return false;
    }
    return true;
}

void CmLayout::firstFreeSlot(const QList<CmCardRecord> &placed, int columns, int w, int h,
                             const QString &ignoreId, int *col, int *row)
{
    const QSet<quint64> taken = occupancyOf(placed, ignoreId);
    const int span = qMin(w, columns);
    for (int r = 0; r < kMaxScanRows; ++r) {
        for (int c = 0; c + span <= columns; ++c) {
            if (cellsFree(taken, c, r, span, h)) {
                *col = c;
                *row = r;
                return;
            }
        }
    }
    *col = 0;
    *row = 0;
}

void CmLayout::materialize()
{
    // Snapshot what the user is looking at *before* touching the store: from
    // here on editing has to start from exactly the arrangement on screen,
    // auto-placed cards included.
    const QList<CmCardRecord> drawn = placement();

    QSet<QString> visible;
    for (const CmCardRecord &r : drawn)
        visible.insert(r.id);

    QList<CmCardRecord> merged = drawn;
    // Keep the records of the sections that are off Principal right now, at the
    // end, so they never take part in a collision but do come back with their
    // slot when re-enabled.
    for (const CmCardRecord &r : std::as_const(m_cards)) {
        if (!visible.contains(r.id))
            merged.append(r);
    }
    m_cards = merged;
}

bool CmLayout::moveCard(const QString &id, int col, int row)
{
    materialize();
    const int cols = columns();
    const int idx = indexOfCard(m_cards, id);
    if (idx < 0)
        return false;

    const CmCardRecord from = m_cards.at(idx);
    CmCardRecord to = from;
    to.col = qBound(0, col, qMax(0, cols - from.w));
    to.row = qMax(0, row);
    if (to.col == from.col && to.row == from.row)
        return true;

    // Only the cards that are actually on screen can be collided with; the
    // records parked at the end belong to sections nobody can see.
    const QStringList live = m_config->principalCards();
    QList<int> hits;
    for (int i = 0; i < m_cards.size(); ++i) {
        if (i == idx || !live.contains(m_cards.at(i).id))
            continue;
        if (m_cards.at(i).overlaps(to))
            hits.append(i);
    }

    if (hits.isEmpty()) {
        m_cards[idx] = to;
    } else if (hits.size() == 1 && m_cards.at(hits.first()).w == from.w
               && m_cards.at(hits.first()).h == from.h) {
        // Exactly one card of the same size is in the way: trade places. Any
        // other overlap is refused rather than shoved aside — a hand-made
        // arrangement must not rearrange itself because a neighbour moved.
        CmCardRecord &other = m_cards[hits.first()];
        other.col = from.col;
        other.row = from.row;
        m_cards[idx] = to;
    } else {
        return false;
    }

    save();
    emit changed();
    return true;
}

bool CmLayout::resizeCard(const QString &id, int w, int h)
{
    materialize();
    const int cols = columns();
    const int idx = indexOfCard(m_cards, id);
    if (idx < 0)
        return false;

    const CmSectionInfo info = CmSections::byId(id);
    CmCardRecord to = m_cards.at(idx);
    to.w = qBound(qMax(1, info.minW), qMin(w, cols), kMaxSpan);
    to.h = qBound(qMax(1, info.minH), h, kMaxSpan);
    if (to.w == m_cards.at(idx).w && to.h == m_cards.at(idx).h)
        return true;
    to.col = qBound(0, to.col, qMax(0, cols - to.w));

    // Growing almost always collides, so unlike a drop this never fails: the
    // card relocates to the first slot where the new size fits.
    QList<CmCardRecord> onScreen;
    const QStringList live = m_config->principalCards();
    for (const CmCardRecord &r : std::as_const(m_cards)) {
        if (live.contains(r.id))
            onScreen.append(r);
    }
    if (!fits(onScreen, to, id))
        firstFreeSlot(onScreen, cols, to.w, to.h, id, &to.col, &to.row);

    m_cards[idx] = to;
    save();
    emit changed();
    return true;
}

void CmLayout::setCardProperty(const QString &id, const QString &key, const QVariant &value)
{
    materialize();
    const int idx = indexOfCard(m_cards, id);
    if (idx < 0)
        return;

    CmCardRecord &r = m_cards[idx];
    if (key == QLatin1String("bg"))
        r.bg = value.toString();
    else if (key == QLatin1String("label"))
        r.label = value.toString();
    else if (key == QLatin1String("showTitle"))
        r.showTitle = value.isNull() ? -1 : (value.toBool() ? 1 : 0);
    else
        return;

    save();
    emit changed();
}

void CmLayout::resetCard(const QString &id)
{
    materialize();
    const int idx = indexOfCard(m_cards, id);
    if (idx < 0)
        return;

    const CmSectionInfo info = CmSections::byId(id);
    CmCardRecord &r = m_cards[idx];
    r.bg.clear();
    r.label.clear();
    r.showTitle = -1;
    r.w = qMax(1, info.defaultW);
    r.h = qMax(1, info.defaultH);
    save();
    emit changed();
}

void CmLayout::resetAll()
{
    if (m_cards.isEmpty())
        return;
    m_cards.clear();
    save();
    emit changed();
}

// ---------------------------------------------------------------------------
// Persistence: one compact JSON document in controlmanager.conf.

void CmLayout::load()
{
    m_cards.clear();
    const QString json = m_config->layoutJson();
    if (json.isEmpty())
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject())
        return;

    const QJsonArray cards = doc.object().value(QStringLiteral("cards")).toArray();
    for (const QJsonValue &v : cards) {
        const CmCardRecord r = cardFromJson(v.toObject());
        // A record for a section this build does not know about is dropped: it
        // would be a card nothing can draw.
        if (!r.id.isEmpty() && CmSections::exists(r.id))
            m_cards.append(r);
    }
}

void CmLayout::save()
{
    QJsonArray cards;
    for (const CmCardRecord &r : std::as_const(m_cards))
        cards.append(cardToJson(r));
    QJsonObject root;
    root[QStringLiteral("cards")] = cards;
    m_config->setLayoutJson(
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

bool CmLayout::exportToFile(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    // Indented on the way out: an exported layout is meant to be read and
    // hand-edited, unlike the one line that lives in the .conf.
    const QJsonDocument doc = QJsonDocument::fromJson(m_config->layoutJson().toUtf8());
    return file.write(doc.toJson(QJsonDocument::Indented)) > 0;
}

bool CmLayout::importFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;
    m_config->setLayoutJson(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    load();
    emit changed();
    return true;
}

QString CmLayout::dump() const
{
    const QList<CmCardRecord> cards = placement();
    const int cols = columns();
    const int rowCount = rows();

    QString out = QStringLiteral("== Principal ==  %1 cols, %2 row(s), %3 card(s)\n")
                      .arg(cols)
                      .arg(rowCount)
                      .arg(cards.size());

    for (int r = 0; r < rowCount; ++r) {
        QString line = QStringLiteral("  ");
        for (int c = 0; c < cols; ++c) {
            QString cell = QStringLiteral("..");
            for (int i = 0; i < cards.size(); ++i) {
                if (cards.at(i).covers(c, r)) {
                    cell = dumpCode(i);
                    break;
                }
            }
            line += cell.leftJustified(3, QLatin1Char(' '));
        }
        out += line + QLatin1Char('\n');
    }
    for (int i = 0; i < cards.size(); ++i) {
        const CmCardRecord &c = cards.at(i);
        out += QStringLiteral("  %1 = %2  at (%3,%4) %5x%6%7\n")
                   .arg(dumpCode(i), c.id)
                   .arg(c.col)
                   .arg(c.row)
                   .arg(c.w)
                   .arg(c.h)
                   .arg(c.bg.isEmpty() ? QString() : QStringLiteral("  bg=") + c.bg);
    }
    return out;
}
