#include "wallpapercontrol.h"

#include "plasmascript.h"
#include "session.h"
#include "wallpaperfolder.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QUrl>

namespace {

const QChar kSep = QChar(0x1f); // unit separator between the script's output fields

// JS snippet resolving the Plasma screen index whose geometry starts at (x, y)
// into `t` (‑1 if none). Only valid indices (0..screenCount-1) are scanned so a
// disconnected containment (screen == -1) can never match.
QString targetIndexJs(int x, int y)
{
    return QStringLiteral(
        "var t=-1;for(var i=0;i<screenCount;i++){var g=screenGeometry(i);"
        "if(g.x===%1&&g.y===%2){t=i;break;}}")
        .arg(x)
        .arg(y);
}

} // namespace

WallpaperControl::WallpaperControl(QObject *parent)
    : QObject(parent)
{
    checkAvailability();
}

void WallpaperControl::checkAvailability()
{
    // Everything below is Plasma's scripting API, so this is the one gate that
    // really is about plasmashell and not about KWin (see Session). Under LXQt
    // the widget is still useful: the alternate engine takes over below.
    m_available = Session::hasPlasmaShell();
}

void WallpaperControl::setAlternateEngine(std::function<bool()> live,
                                          std::function<void(const QString &)> advance)
{
    m_altLive = std::move(live);
    m_altAdvance = std::move(advance);
}

void WallpaperControl::refreshAvailability()
{
    const bool now = available();
    if (now == m_lastAvailable)
        return;
    m_lastAvailable = now;
    emit availableChanged();
}

bool WallpaperControl::alternateLive() const
{
    return m_altLive && m_altAdvance && m_altLive();
}

bool WallpaperControl::available() const
{
    // The widget is worth showing when *someone* can advance a wallpaper: the
    // Plasma path or kdock's own LXQt engine.
    return m_available || alternateLive();
}

void WallpaperControl::nextWallpaper(const QString &screenName)
{
    // Under LXQt kdock draws the wallpapers itself, so "next" is ours to do and
    // there is no containment to talk to.
    if (alternateLive()) {
        m_altAdvance(screenName);
        emit wallpaperAdvanced(screenName);
        return;
    }
    if (!m_available)
        return;

    // Without a screen we can only drive the primary (legacy behaviour).
    if (screenName.isEmpty()) {
        invokeGlobalShortcut();
        return;
    }

    QScreen *target = nullptr;
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens) {
        if (s->name() == screenName) {
            target = s;
            break;
        }
    }
    if (!target) {
        invokeGlobalShortcut();
        return;
    }

    const QRect g = target->geometry();
    advanceForGeometry(g.x(), g.y(), target->name());
}

void WallpaperControl::nextWallpaperAll()
{
    if (alternateLive()) {
        const auto screens = QGuiApplication::screens();
        for (QScreen *s : screens) {
            m_altAdvance(s->name());
            emit wallpaperAdvanced(s->name());
        }
        return;
    }
    if (!m_available)
        return;
    // By geometry, not by name: that is what the containments are matched on
    // anyway, and it skips the name lookup per screen.
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens) {
        const QRect g = s->geometry();
        advanceForGeometry(g.x(), g.y(), s->name());
    }
}

void WallpaperControl::advanceForGeometry(int x, int y, const QString &screenName)
{
    // Step 1: find out which of the two wallpaper plugins the containment runs,
    // because they need opposite treatments (see the header). For a slideshow,
    // this same call already does the first half of the cycle — taking it off
    // the plugin, and deliberately WITHOUT reloadConfig(), which would repaint
    // to whatever stale image the org.kde.image group holds and read as an
    // extra wallpaper flashing by. For a static image it only reports the one
    // on screen.
    const QString probe =
        targetIndexJs(x, y) +
        QStringLiteral(
            "if(t<0){print('NONE');}else{var ds=desktops();var done=false;"
            "for(var j=0;j<ds.length&&!done;j++){var d=ds[j];if(d.screen===t){done=true;"
            "if(d.wallpaperPlugin==='org.kde.slideshow'){"
            "d.wallpaperPlugin='org.kde.image';print('SLIDE');}"
            "else if(d.wallpaperPlugin==='org.kde.image'){"
            "d.currentConfigGroup=['Wallpaper','org.kde.image','General'];"
            "print('IMAGE'+String.fromCharCode(31)+d.readConfig('Image'));}"
            "else{print('OTHER');}}}}");

    auto reply = PlasmaScript::evaluate(probe);
    auto *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, x, y, screenName](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QString> r = *w;
        if (r.isError())
            return;
        const QStringList parts = r.value().split(kSep);
        const QString kind = parts.value(0);

        if (kind == QLatin1String("SLIDE")) {
            // Step 2, slideshow: back to it, and KDE advances the slideshow
            // itself. This has to be a SEPARATE evaluateScript call — both
            // flips inside one script do not repaint at all — which is why it
            // is chained on the reply instead of being one round trip.
            const QString toSlideshow =
                targetIndexJs(x, y) +
                QStringLiteral(
                    "if(t>=0){var ds=desktops();for(var j=0;j<ds.length;j++){var d=ds[j];"
                    "if(d.screen===t){d.wallpaperPlugin='org.kde.slideshow';"
                    "d.reloadConfig();break;}}}");
            PlasmaScript::run(toSlideshow);
            // KDE picks the next image itself, asynchronously, so this only says
            // "a change is under way" — whoever listens has to read the picture
            // back later, not now.
            emit wallpaperAdvanced(screenName);
            return;
        }

        // Step 2, static image: there is no slideshow to advance, so "next" is
        // the next file of the folder the current image lives in. Unlike the
        // slideshow plugin, org.kde.image DOES honour writeConfig("Image") +
        // reloadConfig() and repaints (measured 2026-08-10) — which is the
        // whole reason these two paths are different.
        if (kind != QLatin1String("IMAGE"))
            return; // NONE, OTHER (a third-party plugin), or nothing at all

        const QString currentPath = QUrl(parts.value(1)).toLocalFile();
        if (currentPath.isEmpty())
            return;
        const QString next = WallpaperFolder::next({QFileInfo(currentPath).absolutePath()},
                                                   currentPath);
        if (next.isEmpty() || next == currentPath)
            return;

        const QString writeScript =
            targetIndexJs(x, y) +
            QStringLiteral(
                "if(t>=0){var ds=desktops();for(var j=0;j<ds.length;j++){var d=ds[j];"
                "if(d.screen===t){d.currentConfigGroup=['Wallpaper','org.kde.image','General'];"
                "d.writeConfig('Image','%1');d.reloadConfig();break;}}}")
                .arg(PlasmaScript::escapeJs(QUrl::fromLocalFile(next).toString()));
        PlasmaScript::run(writeScript);
        emit wallpaperAdvanced(screenName);
    });
}


void WallpaperControl::invokeGlobalShortcut()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.kglobalaccel"),
        QStringLiteral("/component/plasmashell"),
        QStringLiteral("org.kde.kglobalaccel.Component"),
        QStringLiteral("invokeShortcut"));
    msg << QStringLiteral("Slideshow Wallpaper Next Image");
    QDBusConnection::sessionBus().asyncCall(msg);
    // Empty name: KDE only ever moves the primary screen here and does not say
    // so, and a listener re-reading every monitor is harmless anyway.
    emit wallpaperAdvanced(QString());
}
