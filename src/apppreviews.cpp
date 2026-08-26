#include "apppreviews.h"

#include "screenshotsource.h"
#include "thumbnailcache.h"
#include "thumbnailimageprovider.h"
#include "windowmonitor.h"

#include <QSize>

#include <limits>

AppPreviews::AppPreviews(WindowMonitor *monitor, QObject *parent)
    : QObject(parent)
    , m_source(new ScreenShotSource(this))
    , m_cache(new ThumbnailCache(this))
{
    connect(m_source, &ScreenShotSource::thumbnailReady, this,
            [this](const QString &uuid, const QImage &image) {
                m_stored.insert(uuid);
                m_cache->store(uuid, image);
                evict();
                emit thumbnailReady(uuid, m_cache->revision(uuid));
            });
    // A failure is silent by design: no preview appears and the plain tooltip
    // still says which window it is. ScreenShotSource shouts once for the case
    // that matters (NoAuthorized); anything else here would be one line per
    // hover in the journal.
    connect(m_source, &ScreenShotSource::thumbnailFailed, this,
            [this](const QString &uuid, const QString &) { m_cache->markAttempt(uuid); });

    // A window that closed will never be captured again, and its last thumbnail
    // is the biggest thing we hold per window.
    if (monitor) {
        connect(monitor, &WindowMonitor::windowRemoved, this, [this](AbstractWindow *w) {
            if (w && !w->uuid.isEmpty()) {
                m_source->cancel(w->uuid);
                m_cache->forget(w->uuid);
                m_stored.remove(w->uuid);
            }
        });
    }
}

ThumbnailImageProvider *AppPreviews::createImageProvider() const
{
    return new ThumbnailImageProvider(m_cache);
}

bool AppPreviews::available() const
{
    return m_source->available();
}

void AppPreviews::request(const QString &uuid, int maxWidth)
{
    if (uuid.isEmpty() || maxWidth <= 0)
        return;
    // Reuse a capture that is still warm. Without this a pointer running along
    // the dock (and returning to a recent icon) would each time mean a full
    // offscreen re-render in KWin.
    const qint64 last = m_cache->lastAttempt(uuid);
    if (last > 0 && ThumbnailCache::nowMs() - last < kFreshMs)
        return;

    // The height is left generous: KeepAspectRatio inside ScreenShotSource means
    // the width is what actually binds for any window wider than it is tall, and
    // this way a portrait window is not blown up past the configured size either.
    m_source->request(uuid, QSize(maxWidth, maxWidth));
}

void AppPreviews::cancel(const QString &uuid)
{
    if (!uuid.isEmpty())
        m_source->cancel(uuid);
}

int AppPreviews::revision(const QString &uuid) const
{
    return uuid.isEmpty() ? 0 : m_cache->revision(uuid);
}

QString AppPreviews::thumbId(const QString &uuid) const
{
    // Same normalization the cache does, so the url QML builds and the key the
    // capture was stored under are the same string by construction.
    return ThumbnailCache::normalizeKey(uuid);
}

void AppPreviews::evict()
{
    while (m_stored.size() > kMaxEntries) {
        QString oldest;
        qint64 oldestAt = std::numeric_limits<qint64>::max();
        for (const QString &uuid : std::as_const(m_stored)) {
            const qint64 at = m_cache->lastAttempt(uuid);
            if (at < oldestAt) {
                oldestAt = at;
                oldest = uuid;
            }
        }
        if (oldest.isEmpty())
            return; // cannot happen with a non-empty set; do not spin
        m_cache->forget(oldest);
        m_stored.remove(oldest);
    }
}
