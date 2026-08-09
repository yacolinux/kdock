// Where every card of the Principal tab sits and how big it is.
//
// This is TileLayout with the sections and the groups taken out: ControlManager
// has exactly one grid and at most a dozen cards, so a card is identified by its
// section id alone. What is kept is everything that was expensive to get right —
// the collision rules (free / swap / refused), the "growing relocates instead of
// failing" resize, and the records of cards that are not on screen right now.
//
// All of it lives in C++ on purpose: QML only draws and drags, every drop asks
// this class and then obeys. That is what makes `--dump-sections` a real test.

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

class CmConfig;

struct CmCardRecord
{
    QString id;
    int col = 0;
    int row = 0;
    int w = 2;
    int h = 2;
    QString bg;    // empty = the default card background
    QString label; // empty = the section's own name
    int showTitle = -1; // -1 inherit from config, 0 off, 1 on

    bool covers(int c, int r) const
    {
        return c >= col && c < col + w && r >= row && r < row + h;
    }
    bool overlaps(const CmCardRecord &other) const
    {
        return col < other.col + other.w && other.col < col + w && row < other.row + other.h
               && other.row < row + h;
    }
};

class CmLayout : public QObject
{
    Q_OBJECT
public:
    // What dropping a card on a slot would do. The QML asks before the drop so
    // the ghost tells the truth instead of guessing.
    enum DropKind { Free = 0, Swap = 1, Refused = 2 };
    Q_ENUM(DropKind)

    explicit CmLayout(CmConfig *config, QObject *parent = nullptr);

    // Columns of the matrix. 0 in the config means "fit to width", and the QML
    // reports the count it computed through setAutoColumns().
    int columns() const;
    Q_INVOKABLE void setAutoColumns(int columns);

    // The cards the Principal tab shows, in model order, with col/row/w/h filled.
    // A card the user has never placed lands in the first free slot at the size
    // the section table asks for.
    QList<CmCardRecord> placement() const;
    // Rows the canvas needs (at least 1, so an empty grid still has somewhere to
    // drop into).
    Q_INVOKABLE int rows() const;
    // One of DropKind. Read-only: it never writes anything.
    Q_INVOKABLE int dropKind(const QString &id, int col, int row) const;

    // --- editing; all of these persist and emit changed() -------------------
    // false = the drop was refused (the target is taken by a card of a different
    // size); the caller snaps the card back.
    Q_INVOKABLE bool moveCard(const QString &id, int col, int row);
    // Never fails: if the bigger card would overlap, it is relocated to the
    // first slot where it does fit.
    Q_INVOKABLE bool resizeCard(const QString &id, int w, int h);
    Q_INVOKABLE void setCardProperty(const QString &id, const QString &key, const QVariant &value);
    Q_INVOKABLE void resetCard(const QString &id);
    Q_INVOKABLE void resetAll();

    // --- import / export ----------------------------------------------------
    Q_INVOKABLE bool exportToFile(const QString &path) const;
    Q_INVOKABLE bool importFromFile(const QString &path);

    // Human-readable dump of the grid, for --dump-sections.
    QString dump() const;

signals:
    void changed();

private:
    void load();
    void save();
    // Copy the current on-screen arrangement into storage, so that editing
    // starts from exactly what the user is looking at.
    void materialize();
    // First free (col,row) for a w*h card, scanning row-major.
    static void firstFreeSlot(const QList<CmCardRecord> &placed, int columns, int w, int h,
                              const QString &ignoreId, int *col, int *row);
    static bool fits(const QList<CmCardRecord> &placed, const CmCardRecord &candidate,
                     const QString &ignoreId);

    CmConfig *m_config;
    // Cards the user has arranged. Ids not in CmConfig::principalCards() stay
    // here as well: turning a section back on has to find its old slot, and a
    // card that is not drawn must never take part in a collision (that is what
    // placement() filters on).
    QList<CmCardRecord> m_cards;
    int m_autoColumns = 6;
};
