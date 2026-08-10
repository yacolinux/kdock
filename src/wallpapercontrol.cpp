#include "wallpapercontrol.h"

#include "plasmascript.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QScreen>
#include <QUrl>

#include <algorithm>

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

void WallpaperControl::nextWallpaperAll()
{
    if (!m_available)
        return;
    // By geometry, not by name: that is what the containments are matched on
    // anyway, and it skips the name lookup per screen.
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens) {
        const QRect g = s->geometry();
        advanceForGeometry(g.x(), g.y());
    }
}

void WallpaperControl::advanceForGeometry(int x, int y)
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
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, x, y](QDBusPendingCallWatcher *w) {
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
        const QString next = nextImage({QFileInfo(currentPath).absolutePath()}, currentPath);
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
    });
}

QString WallpaperControl::nextImage(const QStringList &folders, const QString &currentPath)
{
    static const QStringList kFilters = {
        QStringLiteral("*.jpg"),  QStringLiteral("*.jpeg"), QStringLiteral("*.png"),
        QStringLiteral("*.webp"), QStringLiteral("*.bmp"),  QStringLiteral("*.gif"),
        QStringLiteral("*.svg"),  QStringLiteral("*.svgz")};

    QStringList files;
    for (const QString &folder : folders) {
        // Not recursive: "the next image of this folder" is what someone
        // looking at a static wallpaper expects, and a folder of folders would
        // make the order impossible to predict.
        QDirIterator it(folder, kFilters, QDir::Files);
        while (it.hasNext())
            files << it.next();
    }
    files.removeDuplicates();
    if (files.isEmpty())
        return {};

    // Stable order so "next" is deterministic regardless of Plasma's mode.
    std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    const int idx = files.indexOf(currentPath);
    // idx < 0 (current not in the set) → start at the first image.
    return files.at((idx + 1) % files.size());
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
