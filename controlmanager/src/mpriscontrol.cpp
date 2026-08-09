#include "mpriscontrol.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>

namespace {
const auto kPrefix = QStringLiteral("org.mpris.MediaPlayer2.");
const auto kPath = QStringLiteral("/org/mpris/MediaPlayer2");
const auto kPlayerIface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const auto kRootIface = QStringLiteral("org.mpris.MediaPlayer2");
const auto kPropsIface = QStringLiteral("org.freedesktop.DBus.Properties");

// a{sv} nested inside a variant arrives as a QDBusArgument. The local copy is
// **const** on purpose: beginMap()/beginArray() have const (read) and non-const
// (write) overloads, and on a non-const object the compiler picks the writing
// ones — the read desynchronizes and libdbus aborts the whole process.
QVariantMap toMap(const QVariant &v)
{
    if (v.canConvert<QVariantMap>() && !v.canConvert<QDBusArgument>())
        return v.toMap();
    const QDBusArgument arg = v.value<QDBusArgument>();
    QVariantMap out;
    if (arg.currentType() != QDBusArgument::MapType)
        return out;
    arg.beginMap();
    while (!arg.atEnd()) {
        QString key;
        QVariant value;
        arg.beginMapEntry();
        arg >> key >> value;
        arg.endMapEntry();
        out.insert(key, value);
    }
    arg.endMap();
    return out;
}

QStringList toStringList(const QVariant &v)
{
    if (!v.canConvert<QDBusArgument>())
        return v.toStringList();
    const QDBusArgument arg = v.value<QDBusArgument>();
    QStringList out;
    if (arg.currentType() != QDBusArgument::ArrayType)
        return out;
    arg.beginArray();
    while (!arg.atEnd()) {
        QString s;
        arg >> s;
        out.append(s);
    }
    arg.endArray();
    return out;
}

// A property value that came wrapped in a variant (GetAll gives QDBusVariant for
// nested types on some players).
QVariant unwrap(const QVariant &v)
{
    if (v.canConvert<QDBusVariant>())
        return v.value<QDBusVariant>().variant();
    return v;
}
} // namespace

MprisControl::MprisControl(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        // No QDBusServiceWatcher: it wants exact names, and players come and go
        // with names nobody can enumerate ahead of time.
        bus.connect(QStringLiteral("org.freedesktop.DBus"),
                    QStringLiteral("/org/freedesktop/DBus"),
                    QStringLiteral("org.freedesktop.DBus"),
                    QStringLiteral("NameOwnerChanged"), this,
                    SLOT(onNameOwnerChanged(QString, QString, QString)));
    }

    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &MprisControl::pollPosition);

    rescanPlayers();
}

void MprisControl::rescanPlayers()
{
    const QString previous = m_current;
    m_players.clear();

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusReply<QStringList> names =
        bus.interface() ? bus.interface()->registeredServiceNames() : QDBusReply<QStringList>();
    const QStringList services = names.isValid() ? names.value() : QStringList();

    for (const QString &name : services) {
        if (!name.startsWith(kPrefix))
            continue;
        QDBusInterface props(name, kPath, kPropsIface, bus);
        const QDBusReply<QVariant> identity =
            props.call(QStringLiteral("Get"), kRootIface, QStringLiteral("Identity"));
        const QDBusReply<QVariant> status =
            props.call(QStringLiteral("Get"), kPlayerIface, QStringLiteral("PlaybackStatus"));

        QVariantMap entry;
        entry[QStringLiteral("id")] = name;
        entry[QStringLiteral("identity")] = identity.isValid()
                                                ? identity.value().toString()
                                                : name.mid(kPrefix.size());
        entry[QStringLiteral("status")] = status.isValid() ? status.value().toString()
                                                           : QStringLiteral("Stopped");
        m_players.append(entry);
    }

    // The user's pick wins while it is still there; otherwise whatever is
    // playing, otherwise the first one.
    QString wanted;
    const auto has = [this](const QString &id) {
        for (const QVariant &v : std::as_const(m_players)) {
            if (v.toMap().value(QStringLiteral("id")).toString() == id)
                return true;
        }
        return false;
    };
    if (!m_preferred.isEmpty() && has(m_preferred))
        wanted = m_preferred;
    if (wanted.isEmpty())
        wanted = firstPlaying();
    if (wanted.isEmpty() && !m_players.isEmpty())
        wanted = m_players.first().toMap().value(QStringLiteral("id")).toString();

    m_current = wanted;
    if (m_current != previous)
        watchCurrent(previous);
    refreshCurrent();
}

QString MprisControl::firstPlaying() const
{
    for (const QVariant &v : m_players) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("status")).toString() == QLatin1String("Playing"))
            return m.value(QStringLiteral("id")).toString();
    }
    return {};
}

void MprisControl::watchCurrent(const QString &previous)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    if (!previous.isEmpty()) {
        bus.disconnect(previous, kPath, kPropsIface, QStringLiteral("PropertiesChanged"), this,
                       SLOT(onPropertiesChanged()));
    }
    if (!m_current.isEmpty()) {
        bus.connect(m_current, kPath, kPropsIface, QStringLiteral("PropertiesChanged"), this,
                    SLOT(onPropertiesChanged()));
    }
}

void MprisControl::refreshCurrent()
{
    m_identity.clear();
    m_title.clear();
    m_artist.clear();
    m_album.clear();
    m_artUrl.clear();
    m_trackId.clear();
    m_status = QStringLiteral("Stopped");
    m_canGoNext = m_canGoPrevious = m_canPause = m_canSeek = false;
    m_position = 0;
    m_length = 0;

    if (m_current.isEmpty()) {
        emit changed();
        return;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface props(m_current, kPath, kPropsIface, bus);

    const QDBusReply<QVariant> identity =
        props.call(QStringLiteral("Get"), kRootIface, QStringLiteral("Identity"));
    if (identity.isValid())
        m_identity = identity.value().toString();

    // One GetAll instead of eight Gets: this runs on every property change of a
    // playing track.
    const QDBusReply<QVariantMap> all = props.call(QStringLiteral("GetAll"), kPlayerIface);
    if (all.isValid()) {
        const QVariantMap m = all.value();
        m_status = m.value(QStringLiteral("PlaybackStatus")).toString();
        m_canGoNext = m.value(QStringLiteral("CanGoNext")).toBool();
        m_canGoPrevious = m.value(QStringLiteral("CanGoPrevious")).toBool();
        m_canPause = m.value(QStringLiteral("CanPause")).toBool();
        m_canSeek = m.value(QStringLiteral("CanSeek")).toBool();
        m_position = m.value(QStringLiteral("Position")).toLongLong();

        const QVariantMap meta = toMap(unwrap(m.value(QStringLiteral("Metadata"))));
        m_title = meta.value(QStringLiteral("xesam:title")).toString();
        m_album = meta.value(QStringLiteral("xesam:album")).toString();
        m_artUrl = meta.value(QStringLiteral("mpris:artUrl")).toString();
        m_length = meta.value(QStringLiteral("mpris:length")).toLongLong();
        m_trackId = meta.value(QStringLiteral("mpris:trackid")).toString();
        m_artist = toStringList(unwrap(meta.value(QStringLiteral("xesam:artist"))))
                       .join(QStringLiteral(", "));
    }

    // Keep the list's status column honest: it is what picks the fallback
    // player after the current one quits.
    for (int i = 0; i < m_players.size(); ++i) {
        QVariantMap entry = m_players.at(i).toMap();
        if (entry.value(QStringLiteral("id")).toString() == m_current) {
            entry[QStringLiteral("status")] = m_status;
            m_players[i] = entry;
            break;
        }
    }

    emit changed();
    emit positionChanged();
}

void MprisControl::onNameOwnerChanged(const QString &name, const QString &oldOwner,
                                      const QString &newOwner)
{
    Q_UNUSED(oldOwner);
    Q_UNUSED(newOwner);
    if (!name.startsWith(kPrefix))
        return;
    rescanPlayers();
}

void MprisControl::onPropertiesChanged()
{
    // The signal carries the changed keys, but a player that only sends
    // "Metadata" still needs the rest re-read to know if it can seek. One
    // GetAll is cheaper than getting this subtly wrong.
    refreshCurrent();
}

void MprisControl::setPlayer(const QString &id)
{
    if (id == m_current)
        return;
    const QString previous = m_current;
    m_preferred = id;
    m_current = id;
    watchCurrent(previous);
    refreshCurrent();
}

void MprisControl::setMonitoring(bool on)
{
    if (on && !m_poll.isActive())
        m_poll.start();
    else if (!on && m_poll.isActive())
        m_poll.stop();
}

void MprisControl::pollPosition()
{
    if (m_current.isEmpty() || m_status != QLatin1String("Playing"))
        return;
    QDBusInterface props(m_current, kPath, kPropsIface, QDBusConnection::sessionBus());
    const QDBusReply<QVariant> pos =
        props.call(QStringLiteral("Get"), kPlayerIface, QStringLiteral("Position"));
    if (!pos.isValid())
        return;
    const qlonglong value = pos.value().toLongLong();
    if (value == m_position)
        return;
    m_position = value;
    emit positionChanged();
}

void MprisControl::call(const QString &method)
{
    if (m_current.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(m_current, kPath, kPlayerIface, method);
    QDBusConnection::sessionBus().asyncCall(msg);
}

void MprisControl::playPause()
{
    call(QStringLiteral("PlayPause"));
}

void MprisControl::play()
{
    call(QStringLiteral("Play"));
}

void MprisControl::pause()
{
    call(QStringLiteral("Pause"));
}

void MprisControl::stop()
{
    call(QStringLiteral("Stop"));
}

void MprisControl::next()
{
    call(QStringLiteral("Next"));
}

void MprisControl::previous()
{
    call(QStringLiteral("Previous"));
}

void MprisControl::raisePlayer()
{
    if (m_current.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(m_current, kPath, kRootIface,
                                                      QStringLiteral("Raise"));
    QDBusConnection::sessionBus().asyncCall(msg);
}

void MprisControl::seekToRatio(qreal ratio)
{
    if (m_current.isEmpty() || !m_canSeek || m_length <= 0 || m_trackId.isEmpty())
        return;
    const qlonglong target = qBound<qlonglong>(0, qlonglong(ratio * m_length), m_length);
    // SetPosition takes the track's object path, so a seek that arrives after
    // the track changed lands on nothing instead of on the wrong song.
    QDBusMessage msg = QDBusMessage::createMethodCall(m_current, kPath, kPlayerIface,
                                                      QStringLiteral("SetPosition"));
    msg.setArguments({QVariant::fromValue(QDBusObjectPath(m_trackId)), target});
    QDBusConnection::sessionBus().asyncCall(msg);
    m_position = target;
    emit positionChanged();
}
