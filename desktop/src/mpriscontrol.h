// What is playing, and its transport controls: MPRIS2 over the session bus.
//
// Every player registers a bus name under org.mpris.MediaPlayer2.*; there are
// usually several (a browser, a music player, the Plasma browser integration),
// so this class keeps the list and picks one as "current": whatever the user
// last chose, else the one that is actually Playing, else the first.
//
// Two traps this code is written around, both from CLAUDE.md:
//   - Metadata is a{sv} with an a{sv}/as inside. Demarshalling goes through a
//     **const** QDBusArgument: on a non-const one the compiler picks the
//     *writing* overloads of beginMap()/beginArray(), the read desynchronizes
//     and libdbus aborts the process.
//   - Position is not push-based. It is polled, and only while someone is
//     looking (setMonitoring), because a poll is a round trip per tick.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class MprisControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString playerId READ playerId NOTIFY changed)
    Q_PROPERTY(QString identity READ identity NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QString artUrl READ artUrl NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY changed)
    Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY changed)
    Q_PROPERTY(bool canPause READ canPause NOTIFY changed)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY changed)
    // Microseconds, the unit MPRIS speaks. 0 length means "unknown".
    Q_PROPERTY(qlonglong position READ position NOTIFY positionChanged)
    Q_PROPERTY(qlonglong length READ length NOTIFY changed)

public:
    explicit MprisControl(QObject *parent = nullptr);

    bool available() const { return !m_players.isEmpty(); }
    QString playerId() const { return m_current; }
    QString identity() const { return m_identity; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    QString artUrl() const { return m_artUrl; }
    QString status() const { return m_status; }
    bool playing() const { return m_status == QLatin1String("Playing"); }
    bool canGoNext() const { return m_canGoNext; }
    bool canGoPrevious() const { return m_canGoPrevious; }
    bool canPause() const { return m_canPause; }
    bool canSeek() const { return m_canSeek; }
    qlonglong position() const { return m_position; }
    qlonglong length() const { return m_length; }

    // [{ id, identity, status }] — one entry per player on the bus.
    Q_INVOKABLE QVariantList players() const { return m_players; }
    Q_INVOKABLE void setPlayer(const QString &id);

    Q_INVOKABLE void playPause();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    // 0..1 of the track length. A no-op when the player cannot seek.
    Q_INVOKABLE void seekToRatio(qreal ratio);
    Q_INVOKABLE void raisePlayer();

    // Only while the Play section is on screen: the position poll is a D-Bus
    // round trip per tick.
    Q_INVOKABLE void setMonitoring(bool on);

signals:
    void changed();
    void positionChanged();

private slots:
    void onNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void onPropertiesChanged();
    void pollPosition();

private:
    void rescanPlayers();
    void refreshCurrent();
    // Subscribe to the current player's PropertiesChanged and drop the previous
    // subscription; without the drop a long session ends up with one connection
    // per player it ever saw.
    void watchCurrent(const QString &previous);
    QString firstPlaying() const;
    void call(const QString &method);

    QVariantList m_players;
    QString m_current;
    // What the user picked. Kept apart from m_current so that a player quitting
    // falls back automatically without forgetting the choice.
    QString m_preferred;

    QString m_identity;
    QString m_title;
    QString m_artist;
    QString m_album;
    QString m_artUrl;
    QString m_status = QStringLiteral("Stopped");
    QString m_trackId;
    bool m_canGoNext = false;
    bool m_canGoPrevious = false;
    bool m_canPause = false;
    bool m_canSeek = false;
    qlonglong m_position = 0;
    qlonglong m_length = 0;

    QTimer m_poll;
};
