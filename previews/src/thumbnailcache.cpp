#include "thumbnailcache.h"

#include <QElapsedTimer>
#include <QMutexLocker>
#include <QUrl>

qint64 ThumbnailCache::nowMs()
{
    static QElapsedTimer timer;
    if (!timer.isValid())
        timer.start();
    return timer.elapsed();
}

QString ThumbnailCache::normalizeKey(const QString &id)
{
    QString key = QUrl::fromPercentEncoding(id.toUtf8());
    key.remove(QLatin1Char('{'));
    key.remove(QLatin1Char('}'));
    return key;
}

ThumbnailCache::ThumbnailCache(QObject *parent)
    : QObject(parent)
{
}

QImage ThumbnailCache::image(const QString &uuid) const
{
    QMutexLocker lock(&m_mutex);
    return m_entries.value(normalizeKey(uuid)).image;
}

int ThumbnailCache::revision(const QString &uuid) const
{
    QMutexLocker lock(&m_mutex);
    return m_entries.value(normalizeKey(uuid)).revision;
}

qint64 ThumbnailCache::lastAttempt(const QString &uuid) const
{
    QMutexLocker lock(&m_mutex);
    return m_entries.value(normalizeKey(uuid)).lastAttempt;
}

void ThumbnailCache::store(const QString &uuid, const QImage &image)
{
    int revision = 0;
    {
        QMutexLocker lock(&m_mutex);
        Entry &entry = m_entries[normalizeKey(uuid)];
        entry.image = image;
        entry.lastAttempt = nowMs();
        revision = ++entry.revision;
    }
    // The signal carries the uuid as it came in: the models match it against the
    // window's own uuid, not against the cache key.
    emit updated(uuid, revision);
}

void ThumbnailCache::markAttempt(const QString &uuid)
{
    QMutexLocker lock(&m_mutex);
    m_entries[normalizeKey(uuid)].lastAttempt = nowMs();
}

void ThumbnailCache::forget(const QString &uuid)
{
    QMutexLocker lock(&m_mutex);
    m_entries.remove(normalizeKey(uuid));
}
