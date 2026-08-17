// Where the pixels of a window come from.
//
// The seam exists so the *live* backend can be added later without touching the
// model, the cache or the QML: KWin also offers zkde_screencast_unstable_v1
// (stream_window by the same uuid) which, with libpipewire, gives real video
// instead of periodic stills. The still-image backend (screenshotsource.cpp)
// needs no extra dependency, so it comes first.

#pragma once

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

class ThumbnailSource : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~ThumbnailSource() override = default;

    virtual bool available() const = 0;

    // Ask for a fresh thumbnail of the window `uuid`, scaled to fit `target`.
    // Requests for a uuid already queued are coalesced; every request is
    // answered by exactly one of the two signals below.
    virtual void request(const QString &uuid, const QSize &target) = 0;
    // Drop a pending request (window closed, card scrolled out of view).
    virtual void cancel(const QString &uuid) = 0;

signals:
    void thumbnailReady(const QString &uuid, const QImage &image);
    void thumbnailFailed(const QString &uuid, const QString &reason);
};
