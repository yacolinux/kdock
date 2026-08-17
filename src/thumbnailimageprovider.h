// image://thumb/<uuid>@<revision> — serves ThumbnailCache to QML.
//
// Same shape as kdock's SystrayImageProvider (src/systrayimageprovider.cpp): the
// suffix after '@' only exists to bust QtQuick's URL-keyed pixmap cache, and a
// missing entry answers with a transparent 1x1 so QML renders nothing instead of
// logging a "cannot open" warning per frame.

#pragma once

#include <QQuickImageProvider>

class ThumbnailCache;

class ThumbnailImageProvider : public QQuickImageProvider
{
public:
    explicit ThumbnailImageProvider(ThumbnailCache *cache)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_cache(cache)
    {
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    ThumbnailCache *m_cache;
};
