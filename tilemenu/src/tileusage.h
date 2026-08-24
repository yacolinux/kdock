// Per-app launch stats for the tile menu: how many times each app has been
// launched and when, plus an ordered "recently launched" list. Only the tile
// menu writes here, and it is the sole source for the two things the KActivities
// service would have given us on Plasma (which does not run under LXQt): the
// search-result ordering by frequency / recency, and the recent-apps strip that
// shows when the empty search box takes focus.
//
// Stored in ~/.local/share/kdock/tilemenu-usage.conf, separate from
// tilemenu.conf so a huge or corrupt usage log can be wiped without touching the
// user's layout and settings. The stats live in one JSON blob per key because
// .desktop ids carry '.' and sometimes '/', which QSettings would read as group
// separators (same reason TileConfig stores the layout as JSON).

#pragma once

#include <QHash>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

class TileUsage : public QObject
{
    Q_OBJECT
public:
    explicit TileUsage(QObject *parent = nullptr);

    // Bumps the count, stamps "now", and moves the id to the front of the recent
    // list (deduplicated, capped). Persisted immediately: a launch usually also
    // closes the menu, so there is no later moment to flush.
    Q_INVOKABLE void recordLaunch(const QString &id);

    int count(const QString &id) const;
    qint64 lastMs(const QString &id) const;
    QStringList recentIds() const { return m_recent; }

signals:
    void changed();

private:
    static QString filePath();
    void load();
    void save();

    QSettings m_settings;

    struct Stat {
        int count = 0;
        qint64 lastMs = 0;
    };
    QHash<QString, Stat> m_stats;
    QStringList m_recent; // most-recent first

    static constexpr int kRecentCap = 10;
};
