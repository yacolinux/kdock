#include "waylandclipboard.h"

#include <QGuiApplication>
#include <QSocketNotifier>
#include <QTimer>
#include <QDebug>
#include <QtGui/qguiapplication_platform.h>

#include <csignal>
#include <fcntl.h>
#include <unistd.h>

namespace {

// Text mimes in order of preference. The first two are what every toolkit
// offers; the X11 atom names arrive from Xwayland clients.
const QStringList kTextMimes = {
    QStringLiteral("text/plain;charset=utf-8"),
    QStringLiteral("text/plain"),
    QStringLiteral("UTF8_STRING"),
    QStringLiteral("STRING"),
    QStringLiteral("TEXT"),
};

const QStringList kImageMimes = {
    QStringLiteral("image/png"),
    QStringLiteral("image/jpeg"),
    QStringLiteral("image/bmp"),
    QStringLiteral("image/webp"),
};

// Klipper/KWallet mark a password copy with this mime so clipboard managers
// skip it. Storing it in a plain-text history file would be exactly the leak
// the hint exists to prevent.
const QLatin1String kPasswordHint("x-kde-passwordManagerHint");

constexpr int kTransferTimeoutMs = 5000;
constexpr int kMaxTransferBytes = 32 * 1024 * 1024;

wl_display *waylandDisplay()
{
    if (auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>())
        return native->display();
    return nullptr;
}

// The compositor forwards our request to the owning client only once the
// message is on the wire; without this the other side never writes and the read
// below waits for its full timeout.
void flushDisplay()
{
    if (wl_display *d = waylandDisplay())
        wl_display_flush(d);
}

// Drain a pipe without blocking the GUI thread. A blocking read() here would
// freeze the whole dock whenever the source client is slow (or never writes).
// Takes ownership of fd.
template<typename Fn>
void readAllAsync(int fd, Fn &&done)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read);
    auto *buffer = new QByteArray;
    auto *callback = new std::decay_t<Fn>(std::forward<Fn>(done));

    // Declared before `finish` so the latter can stop it — which is the whole
    // point, see below.
    auto *watchdog = new QTimer(notifier);

    // **finish() must run exactly once, and both halves of that are needed.**
    //
    // It tears down state the *other* path still holds a copy of (the two
    // lambdas below capture `buffer`, `callback` and `fd` by value), so a second
    // call is a double free — and the cleanup it does is not enough to prevent
    // one on its own:
    //
    //   - `notifier->deleteLater()` is **deferred**, and the watchdog is only a
    //     child of the notifier, so it dies with it — later. Any nested event
    //     loop running in between still fires the timer. That is not a
    //     hypothetical: kdock's own startup has one (GlobalShortcuts::
    //     registerAction does a blocking QDBusConnection::call), and a clipboard
    //     transfer in flight at that moment took the process down with
    //     "double free or corruption" (2026-08-20).
    //   - `setEnabled(false)` stops future activations but says nothing about an
    //     activation already queued.
    //
    // So: stop the timer, and guard against re-entry anyway. The flag lives on
    // the notifier, which is guaranteed alive for the whole window (deleteLater
    // cannot have run yet when a stale lambda fires).
    const auto finish = [notifier, buffer, callback, fd, watchdog](bool ok) {
        if (notifier->property("kdock.transferDone").toBool())
            return;
        notifier->setProperty("kdock.transferDone", true);
        watchdog->stop();
        notifier->setEnabled(false);
        (*callback)(ok ? *buffer : QByteArray());
        delete buffer;
        delete callback;
        ::close(fd);
        notifier->deleteLater();
    };

    watchdog->setSingleShot(true);
    watchdog->setInterval(kTransferTimeoutMs);
    QObject::connect(watchdog, &QTimer::timeout, notifier, [finish] {
        qWarning("kdock: clipboard transfer timed out");
        finish(false);
    });
    watchdog->start();

    QObject::connect(notifier, &QSocketNotifier::activated, notifier, [=] {
        char chunk[16 * 1024];
        for (;;) {
            const ssize_t n = ::read(fd, chunk, sizeof(chunk));
            if (n > 0) {
                buffer->append(chunk, int(n));
                if (buffer->size() > kMaxTransferBytes) {
                    finish(false);
                    return;
                }
                continue;
            }
            if (n == 0) { // EOF: the writer closed its end
                finish(true);
                return;
            }
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return; // more later
            finish(false);
            return;
        }
    });
    flushDisplay();
}

// Feed a pipe without blocking. An image does not fit in the 64 KB pipe buffer,
// so a blocking write() would stall the dock until the other side finished
// reading. Takes ownership of fd.
void writeAllAsync(int fd, const QByteArray &data)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Write);
    auto *offset = new qsizetype(0);
    auto *payload = new QByteArray(data);

    // As with readAllAsync(), the timeout and the socket notifier are two
    // independent event sources. A notifier activation may already be queued
    // when the watchdog fires (or the other way around), so cleanup must be
    // idempotent and must stop the watchdog before releasing its captured
    // state. Without this, clicking an image/text entry could leave a stale
    // write callback behind and abort kdock with a double free.
    auto *watchdog = new QTimer(notifier);
    const auto finish = [notifier, offset, payload, fd, watchdog] {
        if (notifier->property("kdock.transferDone").toBool())
            return;
        notifier->setProperty("kdock.transferDone", true);
        watchdog->stop();
        notifier->setEnabled(false);
        delete offset;
        delete payload;
        ::close(fd);
        notifier->deleteLater();
    };

    watchdog->setSingleShot(true);
    watchdog->setInterval(kTransferTimeoutMs);
    QObject::connect(watchdog, &QTimer::timeout, notifier, finish);
    watchdog->start();

    QObject::connect(notifier, &QSocketNotifier::activated, notifier, [=] {
        while (*offset < payload->size()) {
            const ssize_t n = ::write(fd, payload->constData() + *offset,
                                      size_t(payload->size() - *offset));
            if (n > 0) {
                *offset += n;
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                return; // room later
            finish(); // EPIPE: the reader gave up
            return;
        }
        finish();
    });
}

QString pickMime(const QStringList &offered, const QStringList &preferred)
{
    for (const QString &want : preferred) {
        for (const QString &have : offered) {
            if (have.compare(want, Qt::CaseInsensitive) == 0)
                return have;
        }
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------

DataControlOffer::DataControlOffer(struct ::ext_data_control_offer_v1 *object, QObject *parent)
    : QObject(parent)
    , QtWayland::ext_data_control_offer_v1(object)
{
}

DataControlOffer::~DataControlOffer()
{
    if (isInitialized())
        destroy();
}

void DataControlOffer::ext_data_control_offer_v1_offer(const QString &mimeType)
{
    m_mimeTypes.append(mimeType);
}

// ---------------------------------------------------------------------------

DataControlSource::DataControlSource(struct ::ext_data_control_source_v1 *object,
                                     const QByteArray &data, QObject *parent)
    : QObject(parent)
    , QtWayland::ext_data_control_source_v1(object)
    , m_data(data)
{
}

void DataControlSource::ext_data_control_source_v1_send(const QString &mimeType, int32_t fd)
{
    Q_UNUSED(mimeType); // every mime we offer serves the same bytes
    writeAllAsync(fd, m_data);
}

void DataControlSource::ext_data_control_source_v1_cancelled()
{
    emit cancelled();
}

// ---------------------------------------------------------------------------

DataControlDevice::DataControlDevice(struct ::ext_data_control_device_v1 *object, QObject *parent)
    : QObject(parent)
    , QtWayland::ext_data_control_device_v1(object)
{
}

DataControlDevice::~DataControlDevice()
{
    if (isInitialized())
        destroy();
}

void DataControlDevice::ext_data_control_device_v1_data_offer(struct ::ext_data_control_offer_v1 *id)
{
    // Wrapped now so its offer() events (which arrive before the selection
    // event) land somewhere; ownership passes to us.
    new DataControlOffer(id, this);
}

void DataControlDevice::ext_data_control_device_v1_selection(struct ::ext_data_control_offer_v1 *id)
{
    auto *offer = id ? static_cast<DataControlOffer *>(
                      QtWayland::ext_data_control_offer_v1::fromObject(id))
                     : nullptr;
    emit selectionOffered(offer);

    // The previous selection's offer is dead as of this event.
    if (m_currentOffer && m_currentOffer != offer)
        delete m_currentOffer;
    m_currentOffer = offer;
}

void DataControlDevice::ext_data_control_device_v1_finished()
{
    // The compositor revoked the device (e.g. the seat went away).
    if (isInitialized())
        destroy();
}

// ---------------------------------------------------------------------------

WaylandClipboard::WaylandClipboard(QObject *parent)
    : QWaylandClientExtensionTemplate<WaylandClipboard>(1)
{
    setParent(parent);

    // Serving the clipboard means writing into pipes whose reader can vanish
    // mid-transfer; the default SIGPIPE disposition would kill the dock.
    ::signal(SIGPIPE, SIG_IGN);

    connect(this, &QWaylandClientExtension::activeChanged, this, &WaylandClipboard::ensureDevice);
    initialize();
    ensureDevice();
}

void WaylandClipboard::ensureDevice()
{
    if (m_device || !isActive())
        return;
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    wl_seat *seat = native ? native->seat() : nullptr;
    if (!seat)
        return; // retried on the next activeChanged

    m_device = new DataControlDevice(get_data_device(seat), this);
    connect(m_device, &DataControlDevice::selectionOffered, this, &WaylandClipboard::onSelection);
}

void WaylandClipboard::onSelection(DataControlOffer *offer)
{
    if (!offer)
        return; // selection cleared
    if (m_source)
        return; // our own bytes coming back at us

    const QStringList mimes = offer->mimeTypes();
    if (mimes.contains(kPasswordHint))
        return;

    const QString textMime = pickMime(mimes, kTextMimes);
    const QString imageMime = textMime.isEmpty() ? pickMime(mimes, kImageMimes) : QString();
    if (textMime.isEmpty() && imageMime.isEmpty())
        return;

    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0) {
        qWarning("kdock: clipboard pipe2 failed");
        return;
    }
    offer->receive(textMime.isEmpty() ? imageMime : textMime, fds[1]);
    ::close(fds[1]); // our copy; the writer holds the other one

    const QString mime = textMime.isEmpty() ? imageMime : textMime;
    const bool isText = !textMime.isEmpty();
    readAllAsync(fds[0], [this, isText, mime](const QByteArray &data) {
        if (data.isEmpty())
            return;
        if (isText)
            emit textCopied(QString::fromUtf8(data));
        else
            emit imageCopied(data, mime);
    });
}

void WaylandClipboard::takeSelection(const QByteArray &data, const QStringList &mimeTypes)
{
    if (!m_device)
        return;

    // Replacing our own source: drop the old one first, the protocol allows a
    // source to be used for set_selection only once.
    if (m_source) {
        m_source->destroy();
        delete m_source;
        m_source = nullptr;
    }

    m_source = new DataControlSource(create_data_source(), data, this);
    for (const QString &mime : mimeTypes)
        m_source->offer(mime);
    connect(m_source, &DataControlSource::cancelled, this, [this] {
        if (!m_source)
            return;
        m_source->deleteLater();
        m_source = nullptr;
    });
    m_device->set_selection(m_source->object());
    flushDisplay();
}

void WaylandClipboard::setText(const QString &text)
{
    if (text.isEmpty())
        return;
    takeSelection(text.toUtf8(), kTextMimes);
}

void WaylandClipboard::setImage(const QByteArray &pngData)
{
    if (pngData.isEmpty())
        return;
    takeSelection(pngData, {QStringLiteral("image/png")});
}
