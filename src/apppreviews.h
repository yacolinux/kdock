// Window thumbnails for the dock's hover previews (the apps block).
//
// One instance per *process*, not per dock: ScreenShotSource already serializes
// its captures (KWin re-renders the window offscreen for each one), and two docks
// pointing at the same window should share the one image instead of asking twice.
// It travels in DockManager::Shared and every DockWindow registers it as the
// `appPreviews` context property plus the `image://thumb` provider.
//
// The whole thing is inert until QML asks: nothing is captured on a timer, and
// with previews turned off in the settings not a single D-Bus call is made.
//
// Authorization is the part that breaks first — see screenshotsource.h. Without
// X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2 in the *installed*
// kdock.desktop every capture fails and no preview ever appears, which from the
// outside is indistinguishable from a bug in the QML. `kdock --dump-captures`
// tells the two apart.

#pragma once

#include <QObject>
#include <QSet>
#include <QString>

class ScreenShotSource;
class ThumbnailCache;
class ThumbnailImageProvider;
class WindowMonitor;

class AppPreviews : public QObject
{
    Q_OBJECT
public:
    // `monitor` may be null (no compositor backend); it is only used to drop a
    // thumbnail when its window goes away.
    explicit AppPreviews(WindowMonitor *monitor = nullptr, QObject *parent = nullptr);

    // A fresh provider for each QML engine: QQmlEngine::addImageProvider takes
    // ownership, so the docks cannot share one instance. They do share the cache
    // behind it.
    ThumbnailImageProvider *createImageProvider() const;

    // Whether captures are possible at all (KWin on the bus). Says nothing about
    // authorization: that only shows up when the first capture is refused.
    Q_INVOKABLE bool available() const;

    // Ask for a capture of `uuid` no wider than `maxWidth`. A capture younger
    // than kFreshMs is reused as is, so a pointer sweeping across the dock (icon
    // to icon, then back) costs KWin nothing extra.
    Q_INVOKABLE void request(const QString &uuid, int maxWidth);
    // Drop anything still queued (the pointer left the icon). A capture already
    // in flight is left alone — see ScreenShotSource::cancel.
    Q_INVOKABLE void cancel(const QString &uuid);

    // 0 while there has never been a successful capture of this window. QML uses
    // it both as "is there anything to show" and as the cache-busting suffix.
    Q_INVOKABLE int revision(const QString &uuid) const;
    // The url-safe spelling of `uuid`. **Never build the image:// url from the
    // raw uuid**: QUrl percent-encodes KWin's braces, the provider id stops
    // matching the cache key, and every preview stays blank while the capture
    // itself reports success (the bug of 2026-07-30, in kdock-previews).
    Q_INVOKABLE QString thumbId(const QString &uuid) const;

signals:
    // A new capture landed. QML re-reads revision() from here.
    void thumbnailReady(const QString &uuid, int revision);

private:
    // A capture younger than this is reused instead of asked for again.
    static constexpr qint64 kFreshMs = 800;
    // Thumbnails kept alive at once. Only one preview is on screen at a time, so
    // this is purely a "going back to the previous icon is instant" buffer; a
    // 260 px capture is ~170 KB, and the dock's memory profile does not need a
    // hash that grows with the session.
    static constexpr int kMaxEntries = 12;

    void evict();

    ScreenShotSource *m_source;
    ThumbnailCache *m_cache;
    // Which uuids we have ever stored, so evict() has something to walk (the
    // cache itself is keyed by its own normalized ids).
    QSet<QString> m_stored;
};
