// The tile placement engine: where every tile sits, how big it is, and what
// happens when one is dropped on another.
//
// All of it lives in C++ on purpose. QML only draws and drags; every drop asks
// this class and then follows whatever it decided. That is what makes the
// feature testable — `kdock-tilemenu --dump-layout` and a 40-line console probe
// exercise auto-placement, collisions, swaps, resizes and groups with no window
// on screen.
//
// ## Sections
// One layout per menu section, keyed exactly as AppMenu::sections() keys them
// ("cat:__favorites__", "cat:Internet", "menu:Web/Apps"…). A section the user
// has never touched has no record at all and is laid out automatically, sorted
// the way AppMenu returns it. The first drag "materializes" the section: every
// tile it currently shows is written down, and from then on it is the user's.
//
// ## Groups are tabs
// A section is a list of groups, and the menu shows one of them at a time behind
// a tab bar. Tiles store (group, col, row) and every group starts its own matrix
// at row 0, so the canvas only ever draws the current group and the drag
// arithmetic is plain (col, row) — no shared coordinate space to keep in step.
// (They used to be stacked bands inside one matrix; the `absRow` that scheme
// needed is gone.)

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class AppMenu;
class TileConfig;

struct TileRecord
{
    QString id;
    int group = 0;
    int col = 0;
    int row = 0;
    int w = 1;
    int h = 1;
    QString bg;    // empty = the default tile background
    QString image; // empty = none
    QString label; // empty = the app's own name
    QString icon;  // empty = the app's own icon
    // Per-tile override of the global "show icons" / "show labels": -1 inherit,
    // 0 off, 1 on.
    int showIcon = -1;
    int showLabel = -1;

    bool covers(int c, int r) const
    {
        return c >= col && c < col + w && r >= row && r < row + h;
    }
    bool overlaps(const TileRecord &other) const
    {
        return col < other.col + other.w && other.col < col + w && row < other.row + other.h
               && other.row < row + h;
    }
};

struct TileGroup
{
    QString title;
};

class TileLayout : public QObject
{
    Q_OBJECT
public:
    // What dropping a tile on a slot would do. The QML asks before the drop so
    // the ghost tells the truth instead of guessing.
    enum DropKind { Free = 0, Swap = 1, Refused = 2 };
    Q_ENUM(DropKind)

    TileLayout(TileConfig *config, AppMenu *menu, QObject *parent = nullptr);

    // Columns of the matrix. 0 in the config means "fit to width", and the QML
    // reports the count it computed through setAutoColumns().
    int columns() const;
    Q_INVOKABLE void setAutoColumns(int columns);

    // The tiles of a section, in model order, with group/col/row filled.
    // Apps with no record are auto-placed in the first free slot; records whose
    // app is gone are kept in storage (a reinstall finds its place again) but
    // are not returned here.
    QList<TileRecord> placement(const QString &section) const;
    // [{ index, title, tiles, rows }] — one entry per tab, in tab order.
    Q_INVOKABLE QVariantList groups(const QString &section) const;
    // Rows the canvas needs for one group (at least 1, so an empty tab still has
    // somewhere to drop into).
    Q_INVOKABLE int rowsOfGroup(const QString &section, int group) const;
    // Whether the user has ever arranged this section (drives the A-Z rail:
    // "jump to K" is meaningless once the tiles are placed by hand).
    Q_INVOKABLE bool isCustomized(const QString &section) const;
    // One of DropKind. Read-only: it never materializes the section.
    Q_INVOKABLE int dropKind(const QString &section, const QString &id, int group, int col,
                             int row) const;

    // --- editing; all of these persist and emit changed(section) -------------
    // false = the drop was refused (the target is taken by a tile of a
    // different size); the caller snaps the tile back.
    Q_INVOKABLE bool moveTile(const QString &section, const QString &id, int group, int col,
                              int row);
    // Send a tile to another tab: it lands on the first free slot there. Dragging
    // a tile onto a tab does this, and so does its context menu — with one group
    // on screen at a time there is no other way.
    Q_INVOKABLE bool moveTileToGroup(const QString &section, const QString &id, int group);
    // Never fails: if the bigger tile would overlap, it is relocated to the
    // first slot in its group where it does fit (appending a row if needed).
    Q_INVOKABLE bool resizeTile(const QString &section, const QString &id, int w, int h);
    Q_INVOKABLE void setTileProperty(const QString &section, const QString &id,
                                     const QString &key, const QVariant &value);
    Q_INVOKABLE void resetTile(const QString &section, const QString &id);
    Q_INVOKABLE int addGroup(const QString &section, const QString &title);
    Q_INVOKABLE void renameGroup(const QString &section, int group, const QString &title);
    Q_INVOKABLE void moveGroup(const QString &section, int from, int to);
    // The tiles fall into the neighbouring group. The last group cannot go.
    Q_INVOKABLE void removeGroup(const QString &section, int group);
    Q_INVOKABLE void resetSection(const QString &section);
    Q_INVOKABLE void resetAll();

    // --- import / export ----------------------------------------------------
    Q_INVOKABLE bool exportToFile(const QString &path) const;
    Q_INVOKABLE bool importFromFile(const QString &path);

    // Human-readable dump of one section (or all of them), for --dump-layout.
    QString dump(const QString &section = QString()) const;

signals:
    void changed(const QString &section);

private:
    struct Section
    {
        QList<TileGroup> groups;
        QList<TileRecord> tiles;
        // Records whose app is not installed right now. They keep their slot for
        // a reinstall, and they are held apart from `tiles` because a tile that
        // is not drawn must never take part in a collision test.
        QList<TileRecord> orphans;
    };

    void load();
    void save();
    // Copy the section's current on-screen arrangement into storage, so that
    // editing it starts from exactly what the user is looking at.
    Section &materialize(const QString &section);
    // Live app ids of a section, in the order AppMenu returns them.
    QStringList liveIds(const QString &section) const;
    // First free (col,row) for a w*h tile in `group`, scanning row-major.
    static void firstFreeSlot(const QList<TileRecord> &placed, int group, int columns, int w,
                              int h, const QString &ignoreId, int *col, int *row);
    static bool fits(const QList<TileRecord> &placed, const TileRecord &candidate,
                     const QString &ignoreId);
    // Rows each group occupies, in order (at least one each).
    static QList<int> groupRows(const QList<TileGroup> &groups, const QList<TileRecord> &tiles);

    TileConfig *m_config;
    AppMenu *m_menu;
    QHash<QString, Section> m_sections;
    // Live app ids per section. Mutable because liveIds() is const and this is
    // a cache; cleared whenever AppMenu says the app set changed.
    mutable QHash<QString, QStringList> m_idsCache;
    int m_autoColumns = 10;
};
