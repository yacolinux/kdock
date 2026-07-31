// The scaled thumbnails, keyed by window uuid, plus the revision counter that
// busts QML's image cache (same trick as SystrayModel's iconSerial).
//
// Only the *scaled* copy is kept: the raw capture of a 4K window is ~33 MB and
// there is one per window on screen.
//
// Guarded by a mutex because QQuickImageProvider::requestImage runs on QtQuick's
// image-loading thread whenever the QML Image is `asynchronous`, while everything
// that writes here happens on the main thread.

#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QString>

class ThumbnailCache : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailCache(QObject *parent = nullptr);

    // The clock lastAttempt() is measured against: monotonic ms since the first
    // call, shared by the whole process so the schedulers of several strips
    // compare like with like.
    static qint64 nowMs();

    // One key per window, whoever asks. KWin's uuid comes with braces
    // ({461eb7ac-…}), and **QUrl percent-encodes those**: an id that travelled
    // through an image:// URL arrives as %7B461eb7ac-…%7D, which used to miss the
    // cache and leave every card blank while the capture itself was fine (bug
    // 2026-07-30). Percent-decoding and dropping the braces makes both spellings
    // land on the same entry.
    static QString normalizeKey(const QString &id);

    // Null image when there is nothing for this uuid yet.
    QImage image(const QString &uuid) const;
    // 0 when there has never been a successful capture.
    int revision(const QString &uuid) const;
    // Monotonic ms (QElapsedTimer-based) of the last capture *attempt*, hit or
    // miss. The scheduler uses it so a window that cannot be captured is not
    // retried in a tight loop.
    qint64 lastAttempt(const QString &uuid) const;

    void store(const QString &uuid, const QImage &image);
    void markAttempt(const QString &uuid);
    void forget(const QString &uuid);

signals:
    void updated(const QString &uuid, int revision);

private:
    struct Entry {
        QImage image;
        int revision = 0;
        qint64 lastAttempt = 0;
    };

    mutable QMutex m_mutex;
    QHash<QString, Entry> m_entries;
};
