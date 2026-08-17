#include "screenshotsource.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QSocketNotifier>
#include <QVariant>

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace {
const auto kService = QStringLiteral("org.kde.KWin");
const auto kPath = QStringLiteral("/org/kde/KWin/ScreenShot2");
const auto kIface = QStringLiteral("org.kde.KWin.ScreenShot2");

// One read() per 64 KB pipe buffer would mean ~500 event-loop turns for a 4K
// window; a bigger pipe cuts that by an order of magnitude. Best-effort: the
// kernel caps it at /proc/sys/fs/pipe-max-size for unprivileged processes.
constexpr int kPipeSize = 1 << 20;

// Guard against a bogus reply turning into a huge allocation.
constexpr qint64 kMaxImageBytes = 256 * 1024 * 1024;
} // namespace

ScreenShotSource::ScreenShotSource(QObject *parent)
    : ThumbnailSource(parent)
{
}

ScreenShotSource::~ScreenShotSource()
{
    cleanupCurrent();
}

bool ScreenShotSource::available() const
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(kService);
}

void ScreenShotSource::request(const QString &uuid, const QSize &target)
{
    if (uuid.isEmpty())
        return;

    // Coalesce: a card asking again before its turn only updates the target.
    for (Request &queued : m_queue) {
        if (queued.uuid == uuid) {
            queued.target = target;
            return;
        }
    }
    // Already being captured — the result is about to arrive anyway.
    if (m_busy && m_current.uuid == uuid)
        return;

    m_queue.append({uuid, target});
    pump();
}

void ScreenShotSource::cancel(const QString &uuid)
{
    for (int i = m_queue.size() - 1; i >= 0; --i) {
        if (m_queue.at(i).uuid == uuid)
            m_queue.removeAt(i);
    }
    // A capture already in flight is left alone: KWin is going to write into the
    // pipe either way, and dropping the read end mid-write only earns a SIGPIPE
    // on its side.
}

void ScreenShotSource::pump()
{
    if (m_busy || m_queue.isEmpty())
        return;
    startCapture(m_queue.takeFirst());
}

void ScreenShotSource::startCapture(const Request &req)
{
    m_busy = true;
    m_current = req;
    m_buffer.clear();
    m_meta.clear();
    m_error.clear();
    m_pipeDone = false;
    m_replyDone = false;
    m_replyOk = false;

    int fds[2];
    if (::pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
        abortCurrent(QStringLiteral("pipe2 failed"));
        return;
    }
    ::fcntl(fds[0], F_SETPIPE_SZ, kPipeSize);
    m_readFd = fds[0];

    // QDBusUnixFileDescriptor duplicates the descriptor, so our copy of the
    // write end goes away right here: otherwise the reader would never see EOF.
    QDBusUnixFileDescriptor writeEnd(fds[1]);
    ::close(fds[1]);

    m_notifier = new QSocketNotifier(m_readFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] {
        char chunk[64 * 1024];
        for (;;) {
            const ssize_t n = ::read(m_readFd, chunk, sizeof(chunk));
            if (n > 0) {
                if (m_buffer.size() + n > kMaxImageBytes) {
                    m_pipeDone = true;
                    m_error = QStringLiteral("image larger than the sanity limit");
                    m_notifier->setEnabled(false);
                    tryComplete();
                    return;
                }
                m_buffer.append(chunk, int(n));
                continue;
            }
            if (n == 0) { // EOF: KWin closed its end, the image is complete
                m_pipeDone = true;
                m_notifier->setEnabled(false);
                tryComplete();
                return;
            }
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return; // wait for the next activation
            m_pipeDone = true;
            m_error = QStringLiteral("read failed");
            m_notifier->setEnabled(false);
            tryComplete();
            return;
        }
    });

    QVariantMap options;
    // Decorations in: a card without the title bar reads as a cropped window.
    options.insert(QStringLiteral("include-decoration"), true);
    options.insert(QStringLiteral("include-shadow"), false);
    options.insert(QStringLiteral("include-cursor"), false);
    // Logical pixels instead of the output's native resolution: on a scaled
    // monitor that is already half the bytes, and the card is smaller anyway.
    options.insert(QStringLiteral("native-resolution"), false);

    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kIface,
                                                      QStringLiteral("CaptureWindow"));
    call.setArguments({m_current.uuid, options, QVariant::fromValue(writeEnd)});

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *call) {
                const QDBusPendingReply<QVariantMap> reply = *call;
                m_replyDone = true;
                if (reply.isError()) {
                    m_replyOk = false;
                    const QDBusError err = reply.error();
                    m_error = err.name() + QLatin1String(": ") + err.message();
                    if (!m_authWarned && err.name().endsWith(QLatin1String("NoAuthorized"))) {
                        m_authWarned = true;
                        // Named after the running binary because the privilege is
                        // granted per executable: kdock-previews being authorized
                        // says nothing about kdock, and vice versa.
                        qWarning("%s: KWin refused the screenshot (NoAuthorized). The "
                                 "installed .desktop of this binary must carry "
                                 "X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2 "
                                 "and an Exec= with its absolute path. Thumbnails are "
                                 "silently unavailable until then.",
                                 qPrintable(QCoreApplication::applicationName()));
                    }
                } else {
                    m_replyOk = true;
                    m_meta = reply.value();
                }
                call->deleteLater();
                tryComplete();
            });
}

void ScreenShotSource::tryComplete()
{
    // The reply carries the geometry of what is in the pipe, and the pipe tells
    // us when it is all there: both halves are needed.
    if (!m_pipeDone || !m_replyDone)
        return;

    const QString uuid = m_current.uuid;
    const QSize target = m_current.target;
    const bool ok = m_replyOk;
    const QVariantMap meta = m_meta;
    const QString error = m_error;
    QByteArray bytes = m_buffer;

    cleanupCurrent();

    if (!ok) {
        emit thumbnailFailed(uuid, error);
        pump();
        return;
    }

    const int width = meta.value(QStringLiteral("width")).toInt();
    const int height = meta.value(QStringLiteral("height")).toInt();
    const int stride = meta.value(QStringLiteral("stride")).toInt();
    const auto format = QImage::Format(meta.value(QStringLiteral("format")).toUInt());

    if (width <= 0 || height <= 0 || stride <= 0 || format == QImage::Format_Invalid
        || format >= QImage::NImageFormats) {
        emit thumbnailFailed(uuid, QStringLiteral("unusable metadata"));
        pump();
        return;
    }
    if (bytes.size() < qint64(stride) * height) {
        emit thumbnailFailed(uuid, QStringLiteral("short read from the capture pipe"));
        pump();
        return;
    }

    // The QImage does not own `bytes`, so it must be scaled (which allocates)
    // before the buffer goes out of scope.
    const QImage raw(reinterpret_cast<const uchar *>(bytes.constData()), width, height, stride,
                     format);
    QImage scaled = target.isValid() && !target.isEmpty()
        ? raw.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        : raw.copy();
    if (scaled.format() != QImage::Format_ARGB32_Premultiplied)
        scaled = scaled.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    emit thumbnailReady(uuid, scaled);
    pump();
}

void ScreenShotSource::abortCurrent(const QString &reason)
{
    const QString uuid = m_current.uuid;
    cleanupCurrent();
    emit thumbnailFailed(uuid, reason);
    pump();
}

void ScreenShotSource::cleanupCurrent()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_readFd >= 0) {
        ::close(m_readFd);
        m_readFd = -1;
    }
    m_buffer.clear();
    m_buffer.squeeze();
    m_meta.clear();
    m_busy = false;
    m_current = {};
}
