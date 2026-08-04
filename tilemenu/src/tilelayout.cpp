#include "tilelayout.h"

#include "appmenu.h"
#include "tileconfig.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QVariantMap>

namespace {
// Nothing sensible needs a tile taller than this, and the cap keeps a corrupt
// config from asking for a canvas of a million rows.
constexpr int kMaxTileSpan = 8;
// firstFreeSlot() always terminates (rows grow without bound), but a bug in the
// overlap test would otherwise spin forever.
constexpr int kMaxScanRows = 4096;

int indexOfTile(const QList<TileRecord> &tiles, const QString &id)
{
    for (int i = 0; i < tiles.size(); ++i) {
        if (tiles.at(i).id == id)
            return i;
    }
    return -1;
}

// A cell of one band, packed for the occupancy set. Placement is the one hot
// path here: "All Applications" is ~500 tiles, and testing each candidate cell
// against the list of tiles already placed would be cubic.
inline quint64 packCell(int col, int row)
{
    return (quint64(quint32(col)) << 32) | quint32(row);
}

QSet<quint64> occupancyOf(const QList<TileRecord> &tiles, int group, const QString &ignoreId)
{
    QSet<quint64> taken;
    for (const TileRecord &t : tiles) {
        if (t.group != group || t.id == ignoreId)
            continue;
        for (int c = t.col; c < t.col + t.w; ++c) {
            for (int r = t.row; r < t.row + t.h; ++r)
                taken.insert(packCell(c, r));
        }
    }
    return taken;
}

bool cellsFree(const QSet<quint64> &taken, int col, int row, int w, int h)
{
    for (int c = col; c < col + w; ++c) {
        for (int r = row; r < row + h; ++r) {
            if (taken.contains(packCell(c, r)))
                return false;
        }
    }
    return true;
}

QJsonObject tileToJson(const TileRecord &r)
{
    QJsonObject o;
    o[QStringLiteral("id")] = r.id;
    o[QStringLiteral("c")] = r.col;
    o[QStringLiteral("r")] = r.row;
    if (r.w != 1)
        o[QStringLiteral("w")] = r.w;
    if (r.h != 1)
        o[QStringLiteral("h")] = r.h;
    if (!r.bg.isEmpty())
        o[QStringLiteral("bg")] = r.bg;
    if (!r.image.isEmpty())
        o[QStringLiteral("img")] = r.image;
    if (!r.label.isEmpty())
        o[QStringLiteral("lbl")] = r.label;
    if (!r.icon.isEmpty())
        o[QStringLiteral("ic")] = r.icon;
    if (r.showIcon >= 0)
        o[QStringLiteral("si")] = r.showIcon;
    if (r.showLabel >= 0)
        o[QStringLiteral("sl")] = r.showLabel;
    return o;
}

TileRecord tileFromJson(const QJsonObject &o, int group)
{
    TileRecord r;
    r.id = o.value(QStringLiteral("id")).toString();
    r.group = group;
    r.col = o.value(QStringLiteral("c")).toInt();
    r.row = o.value(QStringLiteral("r")).toInt();
    r.w = qBound(1, o.value(QStringLiteral("w")).toInt(1), kMaxTileSpan);
    r.h = qBound(1, o.value(QStringLiteral("h")).toInt(1), kMaxTileSpan);
    r.bg = o.value(QStringLiteral("bg")).toString();
    r.image = o.value(QStringLiteral("img")).toString();
    r.label = o.value(QStringLiteral("lbl")).toString();
    r.icon = o.value(QStringLiteral("ic")).toString();
    r.showIcon = o.contains(QStringLiteral("si")) ? o.value(QStringLiteral("si")).toInt() : -1;
    r.showLabel = o.contains(QStringLiteral("sl")) ? o.value(QStringLiteral("sl")).toInt() : -1;
    return r;
}

// "A", "B", … "Z", "AA", "AB", … — short stable codes for the ASCII dump.
QString dumpCode(int index)
{
    QString out;
    int n = index;
    do {
        out.prepend(QChar('A' + (n % 26)));
        n = n / 26 - 1;
    } while (n >= 0);
    return out;
}
} // namespace

TileLayout::TileLayout(TileConfig *config, AppMenu *menu, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_menu(menu)
{
    load();
    // placement() runs on every cell the pointer crosses during a drag, and each
    // run asks AppMenu for the section's apps — which walks every .desktop it
    // knows. Cache that list and drop it only when the menu itself changes.
    if (m_menu) {
        connect(m_menu, &AppMenu::changed, this, [this] { m_idsCache.clear(); });
        connect(m_menu, &AppMenu::favoritesChanged, this, [this] { m_idsCache.clear(); });
    }
}

int TileLayout::columns() const
{
    const int configured = m_config->columns();
    return configured > 0 ? configured : qMax(1, m_autoColumns);
}

void TileLayout::setAutoColumns(int columns)
{
    columns = qBound(1, columns, 40);
    if (m_autoColumns == columns)
        return;
    m_autoColumns = columns;
    // Only meaningful while the column count is derived from the width; with a
    // fixed count this changes nothing and re-laying out would be noise.
    if (m_config->columns() == 0)
        emit changed(QString());
}

QStringList TileLayout::liveIds(const QString &section) const
{
    const auto cached = m_idsCache.constFind(section);
    if (cached != m_idsCache.constEnd())
        return cached.value();

    QStringList ids;
    if (!m_menu)
        return ids;
    const QVariantList apps = m_menu->appsInCategory(section);
    ids.reserve(apps.size());
    for (const QVariant &v : apps) {
        const QString id = v.toMap().value(QStringLiteral("id")).toString();
        if (!id.isEmpty() && !ids.contains(id))
            ids.append(id);
    }
    m_idsCache.insert(section, ids);
    return ids;
}

int TileLayout::dropKind(const QString &section, const QString &id, int group, int col,
                         int row) const
{
    const QList<TileRecord> tiles = placement(section);
    const int idx = indexOfTile(tiles, id);
    if (idx < 0)
        return Refused;

    auto it = m_sections.constFind(section);
    const int bandCount = it != m_sections.constEnd() && !it.value().groups.isEmpty()
                              ? it.value().groups.size() : 1;
    const int cols = columns();

    TileRecord to = tiles.at(idx);
    to.group = qBound(0, group, bandCount - 1);
    to.col = qBound(0, col, qMax(0, cols - to.w));
    to.row = qMax(0, row);

    int hits = 0;
    int hit = -1;
    for (int i = 0; i < tiles.size(); ++i) {
        if (i == idx)
            continue;
        const TileRecord &other = tiles.at(i);
        if (other.group == to.group && other.overlaps(to)) {
            ++hits;
            hit = i;
        }
    }
    if (hits == 0)
        return Free;
    if (hits == 1 && tiles.at(hit).w == to.w && tiles.at(hit).h == to.h)
        return Swap;
    return Refused;
}

bool TileLayout::fits(const QList<TileRecord> &placed, const TileRecord &candidate,
                      const QString &ignoreId)
{
    for (const TileRecord &other : placed) {
        if (other.group != candidate.group || other.id == ignoreId || other.id == candidate.id)
            continue;
        if (other.overlaps(candidate))
            return false;
    }
    return true;
}

void TileLayout::firstFreeSlot(const QList<TileRecord> &placed, int group, int columns, int w,
                               int h, const QString &ignoreId, int *col, int *row)
{
    const QSet<quint64> taken = occupancyOf(placed, group, ignoreId);
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

QList<int> TileLayout::groupRows(const QList<TileGroup> &groups, const QList<TileRecord> &tiles)
{
    QList<int> rows;
    rows.reserve(groups.size());
    for (int g = 0; g < groups.size(); ++g) {
        if (groups.at(g).collapsed) {
            rows.append(0);
            continue;
        }
        int bottom = 0;
        for (const TileRecord &t : tiles) {
            if (t.group == g)
                bottom = qMax(bottom, t.row + t.h);
        }
        // An empty group still needs one row, or there is nowhere to drop into.
        rows.append(qMax(1, bottom));
    }
    return rows;
}

QList<TileRecord> TileLayout::placement(const QString &section) const
{
    const QStringList ids = liveIds(section);
    const QSet<QString> liveSet(ids.cbegin(), ids.cend());
    const int cols = columns();

    auto it = m_sections.constFind(section);
    const Section *sec = it != m_sections.constEnd() ? &it.value() : nullptr;

    QList<TileGroup> groups = sec ? sec->groups : QList<TileGroup>();
    if (groups.isEmpty())
        groups.append(TileGroup());

    QList<TileRecord> out;
    QSet<QString> done;

    // 1. Stored records whose app is still installed. Orphans (uninstalled) are
    //    scanned too: after a reinstall the app finds its old slot again.
    if (sec) {
        for (const QList<TileRecord> *list : {&sec->tiles, &sec->orphans}) {
            for (const TileRecord &r : *list) {
                if (!liveSet.contains(r.id) || done.contains(r.id))
                    continue;
                TileRecord copy = r;
                copy.group = qBound(0, copy.group, groups.size() - 1);
                copy.w = qBound(1, copy.w, cols);
                copy.h = qBound(1, copy.h, kMaxTileSpan);
                copy.col = qBound(0, copy.col, qMax(0, cols - copy.w));
                copy.row = qMax(0, copy.row);
                out.append(copy);
                done.insert(r.id);
            }
        }
    }

    // 2. Everything else — a section nobody has arranged yet, or an app
    //    installed after the fact — lands in the first free slot of the last
    //    group, in the order AppMenu returned it (by name, or the user's own
    //    order for favorites).
    //
    //    The cursor only ever moves forward: every tile placed here is 1x1 and
    //    the scan is row-major, so a slot it passed can never come free again.
    //    That is what keeps a 500-app section linear instead of cubic.
    const int target = groups.size() - 1;
    QSet<quint64> taken = occupancyOf(out, target, QString());
    int cursorCol = 0;
    int cursorRow = 0;
    for (const QString &id : ids) {
        if (done.contains(id))
            continue;
        while (taken.contains(packCell(cursorCol, cursorRow))) {
            if (++cursorCol >= cols) {
                cursorCol = 0;
                ++cursorRow;
            }
        }
        TileRecord r;
        r.id = id;
        r.group = target;
        r.col = cursorCol;
        r.row = cursorRow;
        taken.insert(packCell(cursorCol, cursorRow));
        out.append(r);
        done.insert(id);
    }

    // 3. One coordinate space: every tile also carries its row within the whole
    //    canvas, so the QML positions tiles and group headers off the same axis.
    const QList<int> rows = groupRows(groups, out);
    QList<int> start;
    start.reserve(groups.size());
    int acc = 0;
    for (int g = 0; g < groups.size(); ++g) {
        start.append(acc);
        acc += rows.at(g);
    }
    for (TileRecord &r : out)
        r.absRow = start.at(r.group) + r.row;

    return out;
}

QVariantList TileLayout::bands(const QString &section) const
{
    auto it = m_sections.constFind(section);
    QList<TileGroup> groups = it != m_sections.constEnd() ? it.value().groups : QList<TileGroup>();
    if (groups.isEmpty())
        groups.append(TileGroup());

    const QList<TileRecord> tiles = placement(section);
    const QList<int> rows = groupRows(groups, tiles);

    QVariantList out;
    int acc = 0;
    for (int g = 0; g < groups.size(); ++g) {
        QVariantMap m;
        m[QStringLiteral("index")] = g;
        m[QStringLiteral("title")] = groups.at(g).title;
        m[QStringLiteral("collapsed")] = groups.at(g).collapsed;
        m[QStringLiteral("startRow")] = acc;
        m[QStringLiteral("rows")] = rows.at(g);
        out.append(m);
        acc += rows.at(g);
    }
    return out;
}

int TileLayout::totalRows(const QString &section) const
{
    auto it = m_sections.constFind(section);
    QList<TileGroup> groups = it != m_sections.constEnd() ? it.value().groups : QList<TileGroup>();
    if (groups.isEmpty())
        groups.append(TileGroup());
    int total = 0;
    for (int r : groupRows(groups, placement(section)))
        total += r;
    return total;
}

bool TileLayout::isCustomized(const QString &section) const
{
    return m_sections.contains(section);
}

TileLayout::Section &TileLayout::materialize(const QString &section)
{
    // Snapshot what the user is looking at *before* touching the hash: from here
    // on the section is theirs, so editing has to start from exactly the
    // arrangement on screen, auto-placed tiles included.
    const QList<TileRecord> drawn = placement(section);

    Section &s = m_sections[section];
    if (s.groups.isEmpty())
        s.groups.append(TileGroup());

    QSet<QString> live;
    for (const TileRecord &r : drawn)
        live.insert(r.id);

    // Records of uninstalled apps move aside: they keep their slot for a
    // reinstall, but an invisible tile must never take part in a collision.
    QList<TileRecord> orphans;
    for (const QList<TileRecord> *list : {&s.tiles, &s.orphans}) {
        for (const TileRecord &r : *list) {
            if (!live.contains(r.id))
                orphans.append(r);
        }
    }

    s.tiles = drawn;
    for (TileRecord &r : s.tiles)
        r.absRow = 0; // derived, never stored
    s.orphans = orphans;
    return s;
}

bool TileLayout::moveTile(const QString &section, const QString &id, int group, int col, int row)
{
    Section &s = materialize(section);
    const int cols = columns();
    group = qBound(0, group, s.groups.size() - 1);

    const int idx = indexOfTile(s.tiles, id);
    if (idx < 0)
        return false;

    const TileRecord from = s.tiles.at(idx);
    TileRecord to = from;
    to.group = group;
    to.col = qBound(0, col, qMax(0, cols - from.w));
    to.row = qMax(0, row);
    if (to.group == from.group && to.col == from.col && to.row == from.row)
        return true;

    QList<int> hits;
    for (int i = 0; i < s.tiles.size(); ++i) {
        if (i == idx)
            continue;
        const TileRecord &other = s.tiles.at(i);
        if (other.group == to.group && other.overlaps(to))
            hits.append(i);
    }

    if (hits.isEmpty()) {
        s.tiles[idx] = to;
    } else if (hits.size() == 1 && s.tiles.at(hits.first()).w == from.w
               && s.tiles.at(hits.first()).h == from.h) {
        // Exactly one tile of the same size is in the way: trade places. Any
        // other overlap is refused rather than shoved aside — a hand-made
        // arrangement must not rearrange itself because a neighbour moved.
        TileRecord &other = s.tiles[hits.first()];
        other.group = from.group;
        other.col = from.col;
        other.row = from.row;
        s.tiles[idx] = to;
    } else {
        return false;
    }

    save();
    emit changed(section);
    return true;
}

bool TileLayout::moveTileToGroup(const QString &section, const QString &id, int group)
{
    Section &s = materialize(section);
    group = qBound(0, group, s.groups.size() - 1);
    const int idx = indexOfTile(s.tiles, id);
    if (idx < 0)
        return false;
    if (s.tiles.at(idx).group == group)
        return true;

    TileRecord to = s.tiles.at(idx);
    to.group = group;
    firstFreeSlot(s.tiles, group, columns(), to.w, to.h, id, &to.col, &to.row);
    s.tiles[idx] = to;
    save();
    emit changed(section);
    return true;
}

bool TileLayout::resizeTile(const QString &section, const QString &id, int w, int h)
{
    Section &s = materialize(section);
    const int cols = columns();
    const int idx = indexOfTile(s.tiles, id);
    if (idx < 0)
        return false;

    TileRecord to = s.tiles.at(idx);
    to.w = qBound(1, qMin(w, cols), kMaxTileSpan);
    to.h = qBound(1, h, kMaxTileSpan);
    if (to.w == s.tiles.at(idx).w && to.h == s.tiles.at(idx).h)
        return true;
    to.col = qBound(0, to.col, qMax(0, cols - to.w));

    // Growing almost always collides, so unlike a drop this never fails: the
    // tile relocates to the first slot in its group where the new size fits.
    if (!fits(s.tiles, to, id))
        firstFreeSlot(s.tiles, to.group, cols, to.w, to.h, id, &to.col, &to.row);

    s.tiles[idx] = to;
    save();
    emit changed(section);
    return true;
}

void TileLayout::setTileProperty(const QString &section, const QString &id, const QString &key,
                                 const QVariant &value)
{
    Section &s = materialize(section);
    const int idx = indexOfTile(s.tiles, id);
    if (idx < 0)
        return;

    TileRecord &r = s.tiles[idx];
    if (key == QLatin1String("bg"))
        r.bg = value.toString();
    else if (key == QLatin1String("image"))
        r.image = value.toString();
    else if (key == QLatin1String("label"))
        r.label = value.toString();
    else if (key == QLatin1String("icon"))
        r.icon = value.toString();
    else if (key == QLatin1String("showIcon"))
        r.showIcon = value.isNull() ? -1 : (value.toBool() ? 1 : 0);
    else if (key == QLatin1String("showLabel"))
        r.showLabel = value.isNull() ? -1 : (value.toBool() ? 1 : 0);
    else
        return;

    save();
    emit changed(section);
}

void TileLayout::resetTile(const QString &section, const QString &id)
{
    Section &s = materialize(section);
    const int idx = indexOfTile(s.tiles, id);
    if (idx < 0)
        return;

    TileRecord &r = s.tiles[idx];
    r.bg.clear();
    r.image.clear();
    r.label.clear();
    r.icon.clear();
    r.showIcon = -1;
    r.showLabel = -1;
    r.w = 1;
    r.h = 1;
    save();
    emit changed(section);
}

int TileLayout::addGroup(const QString &section, const QString &title)
{
    Section &s = materialize(section);
    TileGroup g;
    g.title = title;
    s.groups.append(g);
    save();
    emit changed(section);
    return s.groups.size() - 1;
}

void TileLayout::renameGroup(const QString &section, int group, const QString &title)
{
    Section &s = materialize(section);
    if (group < 0 || group >= s.groups.size() || s.groups.at(group).title == title)
        return;
    s.groups[group].title = title;
    save();
    emit changed(section);
}

void TileLayout::setGroupCollapsed(const QString &section, int group, bool collapsed)
{
    Section &s = materialize(section);
    if (group < 0 || group >= s.groups.size() || s.groups.at(group).collapsed == collapsed)
        return;
    s.groups[group].collapsed = collapsed;
    save();
    emit changed(section);
}

void TileLayout::moveGroup(const QString &section, int from, int to)
{
    Section &s = materialize(section);
    if (from == to || from < 0 || to < 0 || from >= s.groups.size() || to >= s.groups.size())
        return;

    s.groups.move(from, to);
    // Remap every tile's band index to match the new order.
    const auto remap = [from, to](int g) {
        if (g == from)
            return to;
        if (from < to && g > from && g <= to)
            return g - 1;
        if (from > to && g >= to && g < from)
            return g + 1;
        return g;
    };
    for (TileRecord &r : s.tiles)
        r.group = remap(r.group);
    for (TileRecord &r : s.orphans)
        r.group = remap(r.group);

    save();
    emit changed(section);
}

void TileLayout::removeGroup(const QString &section, int group)
{
    Section &s = materialize(section);
    if (group < 0 || group >= s.groups.size() || s.groups.size() <= 1)
        return; // the last band has to stay: the tiles need somewhere to live

    // The band's tiles fall into its neighbour (the previous one, or the next
    // when removing the first) and are re-placed at free slots there.
    const int dest = group > 0 ? group - 1 : 0;
    QList<TileRecord> homeless;
    for (int i = s.tiles.size() - 1; i >= 0; --i) {
        if (s.tiles.at(i).group == group)
            homeless.prepend(s.tiles.takeAt(i));
    }

    s.groups.removeAt(group);
    const auto remap = [group](int g) { return g > group ? g - 1 : qMin(g, group); };
    for (TileRecord &r : s.tiles)
        r.group = remap(r.group);
    for (TileRecord &r : s.orphans)
        r.group = remap(r.group);

    const int cols = columns();
    for (TileRecord &r : homeless) {
        r.group = dest;
        firstFreeSlot(s.tiles, dest, cols, r.w, r.h, QString(), &r.col, &r.row);
        s.tiles.append(r);
    }

    save();
    emit changed(section);
}

void TileLayout::resetSection(const QString &section)
{
    if (m_sections.remove(section) == 0)
        return;
    save();
    emit changed(section);
}

void TileLayout::resetAll()
{
    if (m_sections.isEmpty())
        return;
    m_sections.clear();
    save();
    emit changed(QString());
}

// ---------------------------------------------------------------------------
// Persistence: one compact JSON document in tilemenu.conf. A blob rather than a
// key per section because section keys carry '/' and ':', which QSettings reads
// as group separators.

void TileLayout::load()
{
    m_sections.clear();
    const QString json = m_config->layoutJson();
    if (json.isEmpty())
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject())
        return;

    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonArray groups = it.value().toObject().value(QStringLiteral("g")).toArray();
        Section s;
        for (int g = 0; g < groups.size(); ++g) {
            const QJsonObject go = groups.at(g).toObject();
            TileGroup group;
            group.title = go.value(QStringLiteral("t")).toString();
            group.collapsed = go.value(QStringLiteral("c")).toBool();
            s.groups.append(group);
            const QJsonArray tiles = go.value(QStringLiteral("tiles")).toArray();
            for (const QJsonValue &tv : tiles) {
                const TileRecord r = tileFromJson(tv.toObject(), g);
                if (!r.id.isEmpty())
                    s.tiles.append(r);
            }
        }
        if (s.groups.isEmpty())
            s.groups.append(TileGroup());
        m_sections.insert(it.key(), s);
    }
}

void TileLayout::save()
{
    QJsonObject root;
    for (auto it = m_sections.cbegin(); it != m_sections.cend(); ++it) {
        const Section &s = it.value();
        QJsonArray groups;
        for (int g = 0; g < s.groups.size(); ++g) {
            QJsonObject go;
            if (!s.groups.at(g).title.isEmpty())
                go[QStringLiteral("t")] = s.groups.at(g).title;
            if (s.groups.at(g).collapsed)
                go[QStringLiteral("c")] = true;
            QJsonArray tiles;
            for (const QList<TileRecord> *list : {&s.tiles, &s.orphans}) {
                for (const TileRecord &r : *list) {
                    if (qBound(0, r.group, s.groups.size() - 1) == g)
                        tiles.append(tileToJson(r));
                }
            }
            go[QStringLiteral("tiles")] = tiles;
            groups.append(go);
        }
        QJsonObject so;
        so[QStringLiteral("g")] = groups;
        root[it.key()] = so;
    }
    m_config->setLayoutJson(
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

bool TileLayout::exportToFile(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    // Indented on the way out: an exported layout is meant to be read and
    // hand-edited, unlike the one line that lives in the .conf.
    const QJsonDocument doc = QJsonDocument::fromJson(m_config->layoutJson().toUtf8());
    return file.write(doc.toJson(QJsonDocument::Indented)) > 0;
}

bool TileLayout::importFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;
    m_config->setLayoutJson(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    load();
    emit changed(QString());
    return true;
}

QString TileLayout::dump(const QString &section) const
{
    QStringList keys;
    if (!section.isEmpty()) {
        keys.append(section);
    } else if (m_menu) {
        const QVariantList sections = m_menu->sections();
        for (const QVariant &v : sections)
            keys.append(v.toMap().value(QStringLiteral("key")).toString());
    }

    const int cols = columns();
    QString out;
    for (const QString &key : keys) {
        const QList<TileRecord> tiles = placement(key);
        if (tiles.isEmpty() && !isCustomized(key))
            continue;

        out += QStringLiteral("== %1 ==  %2, %3 cols, %4 tile(s)\n")
                   .arg(key, isCustomized(key) ? QStringLiteral("customized")
                                               : QStringLiteral("auto"))
                   .arg(cols)
                   .arg(tiles.size());

        const QVariantList bandList = bands(key);
        for (const QVariant &bv : bandList) {
            const QVariantMap band = bv.toMap();
            const int g = band.value(QStringLiteral("index")).toInt();
            const int rows = band.value(QStringLiteral("rows")).toInt();
            out += QStringLiteral("  [band %1] \"%2\"%3\n")
                       .arg(g)
                       .arg(band.value(QStringLiteral("title")).toString(),
                            band.value(QStringLiteral("collapsed")).toBool()
                                ? QStringLiteral(" (collapsed)") : QString());

            QStringList legend;
            for (int r = 0; r < rows; ++r) {
                QString line = QStringLiteral("    ");
                for (int c = 0; c < cols; ++c) {
                    QString cell = QStringLiteral("..");
                    for (int i = 0; i < tiles.size(); ++i) {
                        const TileRecord &t = tiles.at(i);
                        if (t.group == g && t.covers(c, r)) {
                            cell = dumpCode(i);
                            break;
                        }
                    }
                    line += cell.leftJustified(3, QLatin1Char(' '));
                }
                out += line + QLatin1Char('\n');
            }
            for (int i = 0; i < tiles.size(); ++i) {
                const TileRecord &t = tiles.at(i);
                if (t.group != g)
                    continue;
                legend.append(QStringLiteral("%1=%2(%3x%4)")
                                  .arg(dumpCode(i), t.id)
                                  .arg(t.w)
                                  .arg(t.h));
            }
            if (!legend.isEmpty())
                out += QStringLiteral("    -- ") + legend.join(QStringLiteral("  ")) + QLatin1Char('\n');
        }
        out += QLatin1Char('\n');
    }
    return out;
}
