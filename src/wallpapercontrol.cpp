#include "wallpapercontrol.h"

#include "plasmascript.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QDirIterator>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QScreen>
#include <QUrl>

#include <algorithm>

namespace {

const QChar kSep = QChar(0x1f); // unit separator between script output fields

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
    // Step 1: read the slideshow's current image + folders for the containment
    // on the target screen.
    const QString readScript =
        targetIndexJs(x, y) +
        QStringLiteral(
            "if(t<0){print('NONE');}else{var ds=desktops();var done=false;"
            "for(var j=0;j<ds.length&&!done;j++){var d=ds[j];if(d.screen===t){done=true;"
            "if(d.wallpaperPlugin!=='org.kde.slideshow'){print('NOSLIDE');}else{"
            "d.currentConfigGroup=['Wallpaper','org.kde.slideshow','General'];"
            "print('OK'+String.fromCharCode(31)+d.readConfig('Image')"
            "+String.fromCharCode(31)+d.readConfig('SlidePaths'));}}}}");

    auto reply = PlasmaScript::evaluate(readScript);
    auto *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, x, y](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QString> r = *w;
        if (r.isError())
            return;
        const QString out = r.value();
        const QStringList parts = out.split(kSep);
        if (parts.isEmpty() || parts.first() != QLatin1String("OK") || parts.size() < 3)
            return; // NONE / NOSLIDE / unexpected

        const QString currentPath = QUrl(parts.at(1)).toLocalFile();
        const QStringList folders =
            parts.at(2).split(QLatin1Char(','), Qt::SkipEmptyParts);

        const QString next = nextImage(folders, currentPath);
        if (next.isEmpty())
            return;

        // Step 2: write the chosen image back to the same containment.
        const QString nextUrl = QUrl::fromLocalFile(next).toString();
        const QString writeScript =
            targetIndexJs(x, y) +
            QStringLiteral(
                "if(t>=0){var ds=desktops();for(var j=0;j<ds.length;j++){var d=ds[j];"
                "if(d.screen===t){d.currentConfigGroup=['Wallpaper','org.kde.slideshow','General'];"
                "d.writeConfig('Image','%1');d.reloadConfig();break;}}}")
                .arg(PlasmaScript::escapeJs(nextUrl));
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
        QDirIterator it(folder, kFilters, QDir::Files, QDirIterator::Subdirectories);
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

    int idx = files.indexOf(currentPath);
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
