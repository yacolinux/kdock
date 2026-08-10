#include "wallpapercontrol.h"

#include "plasmascript.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QScreen>

namespace {

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
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString desktop = env.value(QStringLiteral("XDG_CURRENT_DESKTOP"));
    const QString session = env.value(QStringLiteral("XDG_SESSION_DESKTOP"));

    if (desktop.contains(QStringLiteral("KDE"), Qt::CaseInsensitive) ||
        session.contains(QStringLiteral("KDE"), Qt::CaseInsensitive) ||
        session.contains(QStringLiteral("plasma"), Qt::CaseInsensitive)) {
        m_available = true;
    }
}

void WallpaperControl::nextWallpaper(const QString &screenName)
{
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
    advanceForGeometry(g.x(), g.y());
}

void WallpaperControl::advanceForGeometry(int x, int y)
{
    // Step 1: take the containment off the slideshow plugin, and deliberately
    // WITHOUT reloadConfig() — reloading here repaints to whatever stale image
    // the org.kde.image group holds, which reads as an extra wallpaper flashing
    // by before the real one.
    const QString toImage =
        targetIndexJs(x, y) +
        QStringLiteral(
            "if(t<0){print('NONE');}else{var ds=desktops();var done=false;"
            "for(var j=0;j<ds.length&&!done;j++){var d=ds[j];if(d.screen===t){done=true;"
            "if(d.wallpaperPlugin!=='org.kde.slideshow'){print('NOSLIDE');}"
            "else{d.wallpaperPlugin='org.kde.image';print('OK');}}}}");

    auto reply = PlasmaScript::evaluate(toImage);
    auto *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, x, y](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QString> r = *w;
        // NONE (no containment there), NOSLIDE (a static wallpaper has no
        // "next" image, by design) or an error: leave the desktop alone.
        if (r.isError() || r.value() != QLatin1String("OK"))
            return;

        // Step 2: back to the slideshow, and KDE advances it itself. This has
        // to be a SEPARATE evaluateScript call — both flips in one script do
        // not repaint at all — which is why it is chained on the reply instead
        // of being one round trip.
        const QString toSlideshow =
            targetIndexJs(x, y) +
            QStringLiteral(
                "if(t>=0){var ds=desktops();for(var j=0;j<ds.length;j++){var d=ds[j];"
                "if(d.screen===t){d.wallpaperPlugin='org.kde.slideshow';"
                "d.reloadConfig();break;}}}");
        PlasmaScript::run(toSlideshow);
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
}
