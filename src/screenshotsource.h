// Thumbnails through org.kde.KWin.ScreenShot2.CaptureWindow.
//
// How it works: the method takes the window's uuid (the same one
// plasma-window-management reports in window_with_uuid), an options map, and the
// *write* end of a pipe. KWin answers the call with the image metadata
// (width/height/stride/format) and writes the raw pixels into the pipe. The pipe
// holds 64 KB by default and a 4K window is ~33 MB, so KWin blocks on the write
// until we drain it: the read therefore has to happen *concurrently* with the
// call. Here that is done with a QSocketNotifier on the read end (no worker
// thread, no extra Qt module, no race with the reply).
//
// Authorization (this is the part that breaks first): KWin resolves the caller's
// /proc/<pid>/exe against the Exec= of a .desktop that declares
//   X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2
// and rejects everyone else with
//   org.kde.KWin.ScreenShot2.Error.NoAuthorized
// So a build-tree binary is *not* authorized until a .desktop pointing at it is
// installed. The authorization is **per executable**: both kdock.desktop.in and
// kdock-previews.desktop.in carry the key, and one of them answering says
// nothing about the other.
//
// Cost control: exactly one capture is in flight at a time (KWin re-renders the
// window offscreen for each one), requests are coalesced per uuid, the image is
// scaled down as soon as it arrives and only the scaled copy is kept.

#pragma once

#include "thumbnailsource.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QVariant>

class QSocketNotifier;
class QDBusPendingCallWatcher;

class ScreenShotSource : public ThumbnailSource
{
    Q_OBJECT
public:
    explicit ScreenShotSource(QObject *parent = nullptr);
    ~ScreenShotSource() override;

    bool available() const override;

    void request(const QString &uuid, const QSize &target) override;
    void cancel(const QString &uuid) override;

private:
    struct Request {
        QString uuid;
        QSize target;
    };

    // Start the next queued capture if none is in flight.
    void pump();
    void startCapture(const Request &req);
    // Called from both the pipe reader and the D-Bus reply; builds the image once
    // both halves are in.
    void tryComplete();
    void abortCurrent(const QString &reason);
    void cleanupCurrent();

    QList<Request> m_queue;

    // In-flight capture state.
    bool m_busy = false;
    Request m_current;
    int m_readFd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QByteArray m_buffer;
    bool m_pipeDone = false;
    bool m_replyDone = false;
    bool m_replyOk = false;
    QVariantMap m_meta;
    QString m_error;

    // The authorization hint is worth exactly one line per run.
    bool m_authWarned = false;
};
