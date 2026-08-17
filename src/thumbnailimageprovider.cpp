#include "thumbnailimageprovider.h"

#include "thumbnailcache.h"

#include <QAtomicInt>

QImage ThumbnailImageProvider::requestImage(const QString &id, QSize *size,
                                            const QSize &requestedSize)
{
    // Strip the cache-busting revision; a KWin uuid never contains '@'.
    const QString uuid = id.section(QLatin1Char('@'), 0, 0);

    QImage image = m_cache ? m_cache->image(uuid) : QImage();
    if (image.isNull()) {
        // QML only sets the source once the card has a revision, so a miss here
        // means the id it built does not resolve to the key the capture was stored
        // under. That is invisible from the outside — the card just stays empty
        // while everything reports success — so it gets said out loud once.
        // (It happened: QUrl percent-encodes the braces of KWin's uuid.)
        static QAtomicInt warned;
        if (!uuid.isEmpty() && warned.testAndSetRelaxed(0, 1)) {
            qWarning("kdock: no thumbnail behind image provider id '%s' "
                     "(cache key would be '%s'). The card stays empty.",
                     qPrintable(id), qPrintable(ThumbnailCache::normalizeKey(uuid)));
        }
        QImage empty(1, 1, QImage::Format_ARGB32_Premultiplied);
        empty.fill(Qt::transparent);
        if (size)
            *size = empty.size();
        return empty;
    }

    // The cache already holds a card-sized copy; only honour a *smaller*
    // request, never scale back up (that would just blur it).
    if (requestedSize.isValid() && !requestedSize.isEmpty()
        && (requestedSize.width() < image.width() || requestedSize.height() < image.height())) {
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (size)
        *size = image.size();
    return image;
}
