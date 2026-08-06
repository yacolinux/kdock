#include "desktopwallpapers.h"

#include "dockconfig.h"
#include "plasmascript.h"
#include "virtualdesktops.h"

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QSettings>
#include <QStringList>
#include <QUrl>

namespace {

// Field / record separators of the capture script's reply. Wallpaper paths can
// contain anything printable, so the split has to use characters that cannot.
const QChar kField = QChar(0x1f);
const QChar kRecord = QChar(0x1e);

QSettings shared()
{
    return QSettings(DockConfig::settingsFilePath(), QSettings::IniFormat);
}

// One INI section per desktop ("[Wallpapers2]"), key = connector name. A single
// "/" in a QSettings key maps to the section and nothing else, so this keeps
// connector names out of the escaping rules entirely.
QString imageKey(int desktop, const QString &screen)
{
    return QStringLiteral("Wallpapers%1/%2").arg(desktop).arg(screen);
}

// JS that resolves each containment's screen into the "x,y" key the maps below
// are indexed by. Shared prologue of the three write scripts.
QString containmentLoopJs()
{
    return QStringLiteral(
        "var ds=desktops();"
        "for(var j=0;j<ds.length;j++){var d=ds[j];if(d.screen<0)continue;"
        "var g=screenGeometry(d.screen);var key=g.x+','+g.y;");
}

QString jsString(const QString &s)
{
    return QLatin1Char('\'') + PlasmaScript::escapeJs(s) + QLatin1Char('\'');
}

// Reads every connected containment's wallpaper plugin plus every key of its
// config group, printed as record/field-separated text. The key names come from
// Plasma's own `configKeys`, so the snapshot is plugin-agnostic: whatever
// org.kde.slideshow (or anything else) stores round-trips without this code
// knowing the names.
QString captureScriptJs()
{
    return QStringLiteral(
               "var F=String.fromCharCode(31);var R=String.fromCharCode(30);var out='';")
           + containmentLoopJs()
           + QStringLiteral(
               "var p=d.wallpaperPlugin;d.currentConfigGroup=['Wallpaper',p,'General'];"
               "var ks=d.configKeys;var rec=key+F+p;"
               "for(var n=0;n<ks.length;n++){rec+=F+ks[n]+F+d.readConfig(ks[n]);}"
               "out+=(out?R:'')+rec;}"
               "print(out);");
}

} // namespace

DesktopWallpapers::DesktopWallpapers(VirtualDesktops *desktops, QObject *parent)
    : QObject(parent)
    , m_desktops(desktops)
{
    if (m_desktops) {
        connect(m_desktops, &VirtualDesktops::currentChanged, this,
                &DesktopWallpapers::onDesktopChanged);
    }
}

// ---------------------------------------------------------------------------
// Persisted settings
// ---------------------------------------------------------------------------

bool DesktopWallpapers::enabled()
{
    return shared().value(QStringLiteral("Wallpapers/enabled"), false).toBool();
}

void DesktopWallpapers::setEnabled(bool on)
{
    QSettings s = shared();
    s.setValue(QStringLiteral("Wallpapers/enabled"), on);
}

int DesktopWallpapers::fillMode()
{
    return shared().value(QStringLiteral("Wallpapers/fillMode"), kFillModeDefault).toInt();
}

void DesktopWallpapers::setFillMode(int mode)
{
    QSettings s = shared();
    s.setValue(QStringLiteral("Wallpapers/fillMode"), mode);
}

QString DesktopWallpapers::imageFor(int desktop, const QString &screen)
{
    if (screen.isEmpty())
        return {};
    return shared().value(imageKey(desktop, screen)).toString();
}

void DesktopWallpapers::setImageFor(int desktop, const QString &screen, const QString &path)
{
    if (screen.isEmpty())
        return;
    QSettings s = shared();
    if (path.isEmpty())
        s.remove(imageKey(desktop, screen));
    else
        s.setValue(imageKey(desktop, screen), path);
}

QStringList DesktopWallpapers::configuredScreens()
{
    QStringList names;
    // Connected first: those are the ones the user is looking at right now.
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens) {
        if (!s->name().isEmpty() && !names.contains(s->name()))
            names << s->name();
    }
    for (const QString &name : DockConfig::knownScreens()) {
        if (names.size() >= kMaxScreens)
            break;
        if (!name.isEmpty() && !names.contains(name))
            names << name;
    }
    while (names.size() > kMaxScreens)
        names.removeLast();
    return names;
}

QHash<QString, WallpaperSnapshot> DesktopWallpapers::snapshot()
{
    const QByteArray raw = shared().value(QStringLiteral("Wallpapers/snapshot")).toString().toUtf8();
    QHash<QString, WallpaperSnapshot> result;
    if (raw.isEmpty())
        return result;
    const QJsonObject root = QJsonDocument::fromJson(raw).object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        WallpaperSnapshot snap;
        snap.plugin = entry.value(QStringLiteral("plugin")).toString();
        if (snap.plugin.isEmpty())
            continue;
        const QJsonObject keys = entry.value(QStringLiteral("keys")).toObject();
        for (auto k = keys.constBegin(); k != keys.constEnd(); ++k)
            snap.keys.insert(k.key(), k.value().toString());
        result.insert(it.key(), snap);
    }
    return result;
}

void DesktopWallpapers::setSnapshot(const QHash<QString, WallpaperSnapshot> &snap)
{
    QJsonObject root;
    for (auto it = snap.constBegin(); it != snap.constEnd(); ++it) {
        QJsonObject keys;
        for (auto k = it.value().keys.constBegin(); k != it.value().keys.constEnd(); ++k)
            keys.insert(k.key(), k.value());
        QJsonObject entry;
        entry.insert(QStringLiteral("plugin"), it.value().plugin);
        entry.insert(QStringLiteral("keys"), keys);
        root.insert(it.key(), entry);
    }
    QSettings s = shared();
    s.setValue(QStringLiteral("Wallpapers/snapshot"),
               QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

bool DesktopWallpapers::applied()
{
    return shared().value(QStringLiteral("Wallpapers/applied"), false).toBool();
}

void DesktopWallpapers::setApplied(bool on)
{
    QSettings s = shared();
    s.setValue(QStringLiteral("Wallpapers/applied"), on);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void DesktopWallpapers::start()
{
    if (!enabled() || !m_desktops)
        return;
    const int current = m_desktops->currentPosition();
    if (current <= 0)
        return; // KWin didn't answer: X11, wlroots or the Xvfb harness.
    if (current == 1) {
        // A previous run may have died (or been killed) with our wallpaper up.
        // Desktop 1 always gets KDE's config back, so put it back now.
        if (applied())
            restore();
        return;
    }
    onDesktopChanged(current);
}

void DesktopWallpapers::onDesktopChanged(int position)
{
    if (!enabled() || position <= 0)
        return;
    if (position == 1) {
        restore();
        return;
    }
    // Desktops past the configurable range are left alone rather than blanked:
    // the user never told us what to put there.
    if (position > DockConfig::kMaxDesktops)
        return;
    if (applied())
        apply(position); // our own wallpaper is up, so there is nothing to save
    else
        captureThenApply(position);
}

void DesktopWallpapers::quit()
{
    if (!applied())
        return;
    const QString write = restoreScript();
    if (write.isEmpty())
        return;
    // Blocking on purpose: aboutToQuit has already fired, so an async reply
    // would never be delivered and the calls would die with the process.
    PlasmaScript::evaluateBlocking(write);
    PlasmaScript::evaluateBlocking(reloadScript(geometryKeys().values()));
    setApplied(false);
}

// ---------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------

void DesktopWallpapers::capture(bool force)
{
    if (!force && applied())
        return; // would store our own image as if it were KDE's

    auto *watcher = new QDBusPendingCallWatcher(PlasmaScript::evaluate(captureScriptJs()), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                const QDBusPendingReply<QString> reply = *w;
                if (reply.isError())
                    return;
                mergeSnapshot(parseCapture(reply.value()));
            });
}

void DesktopWallpapers::captureThenApply(int desktop)
{
    auto *watcher = new QDBusPendingCallWatcher(PlasmaScript::evaluate(captureScriptJs()), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, desktop](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                const QDBusPendingReply<QString> reply = *w;
                // Even if the read failed, applying is still the right thing:
                // the worst case is a stale (or empty) snapshot, and refusing
                // would leave the desktop switch doing nothing at all.
                if (!reply.isError())
                    mergeSnapshot(parseCapture(reply.value()));
                apply(desktop);
            });
}

QHash<QString, WallpaperSnapshot> DesktopWallpapers::parseCapture(const QString &reply) const
{
    QHash<QString, WallpaperSnapshot> fresh;
    if (reply.isEmpty())
        return fresh;

    // "x,y" -> connector, so the containments can be named the way the rest of
    // the config names them.
    QHash<QString, QString> byGeometry;
    const auto keys = geometryKeys();
    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it)
        byGeometry.insert(it.value(), it.key());

    // Every static image we could have written ourselves, so a capture that
    // slipped through the `applied` guard still cannot store our own work as if
    // it were the user's.
    QStringList ours;
    const QStringList screens = configuredScreens();
    for (int desktop = 2; desktop <= DockConfig::kMaxDesktops; ++desktop) {
        for (const QString &screen : screens) {
            const QString path = imageFor(desktop, screen);
            if (!path.isEmpty())
                ours << path;
        }
    }

    const QStringList records = reply.split(kRecord, Qt::SkipEmptyParts);
    for (const QString &record : records) {
        const QStringList fields = record.split(kField);
        if (fields.size() < 3)
            continue;
        const QString screen = byGeometry.value(fields.at(0));
        if (screen.isEmpty())
            continue;
        WallpaperSnapshot snap;
        snap.plugin = fields.at(1);
        for (int i = 2; i + 1 < fields.size(); i += 2)
            snap.keys.insert(fields.at(i), fields.at(i + 1));
        const QString image = QUrl(snap.keys.value(QStringLiteral("Image"))).toLocalFile();
        if (snap.plugin == QLatin1String("org.kde.image") && ours.contains(image))
            continue; // this is ours, not KDE's
        fresh.insert(screen, snap);
    }
    return fresh;
}

void DesktopWallpapers::mergeSnapshot(const QHash<QString, WallpaperSnapshot> &fresh)
{
    if (fresh.isEmpty())
        return;
    // Merge rather than replace: a monitor that is unplugged today still has a
    // desktop-1 configuration we must be able to put back when it comes home.
    QHash<QString, WallpaperSnapshot> stored = snapshot();
    for (auto it = fresh.constBegin(); it != fresh.constEnd(); ++it)
        stored.insert(it.key(), it.value());
    setSnapshot(stored);
    emit snapshotChanged();
}

// ---------------------------------------------------------------------------
// Apply / restore
// ---------------------------------------------------------------------------

QHash<QString, QString> DesktopWallpapers::geometryKeys()
{
    QHash<QString, QString> keys;
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens) {
        const QRect g = s->geometry();
        keys.insert(s->name(),
                    QStringLiteral("%1,%2").arg(g.x()).arg(g.y()));
    }
    return keys;
}

QString DesktopWallpapers::applyScript(int desktop) const
{
    const auto keys = geometryKeys();
    QStringList entries;
    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
        const QString path = imageFor(desktop, it.key());
        if (path.isEmpty())
            continue; // monitor not configured for this desktop: leave it alone
        entries << jsString(it.value()) + QLatin1Char(':')
                       + jsString(QUrl::fromLocalFile(path).toString());
    }
    if (entries.isEmpty())
        return {};

    return QStringLiteral("var m={%1};var f=%2;")
               .arg(entries.join(QLatin1Char(',')))
               .arg(fillMode())
           + containmentLoopJs()
           + QStringLiteral(
               "if(!m.hasOwnProperty(key))continue;"
               "d.wallpaperPlugin='org.kde.image';"
               "d.currentConfigGroup=['Wallpaper','org.kde.image','General'];"
               "d.writeConfig('Image',m[key]);d.writeConfig('FillMode',f);}");
}

QString DesktopWallpapers::restoreScript() const
{
    const auto stored = snapshot();
    const auto keys = geometryKeys();
    QStringList entries;
    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
        const auto snap = stored.constFind(it.key());
        if (snap == stored.constEnd())
            continue; // never captured this monitor
        QStringList pairs;
        for (auto k = snap->keys.constBegin(); k != snap->keys.constEnd(); ++k)
            pairs << jsString(k.key()) + QLatin1Char(':') + jsString(k.value());
        entries << jsString(it.value()) + QStringLiteral(":{p:") + jsString(snap->plugin)
                       + QStringLiteral(",k:{") + pairs.join(QLatin1Char(',')) + QStringLiteral("}}");
    }
    if (entries.isEmpty())
        return {};

    return QStringLiteral("var m={%1};").arg(entries.join(QLatin1Char(',')))
           + containmentLoopJs()
           + QStringLiteral(
               "if(!m.hasOwnProperty(key))continue;var e=m[key];"
               "d.wallpaperPlugin=e.p;"
               "d.currentConfigGroup=['Wallpaper',e.p,'General'];"
               "for(var n in e.k){d.writeConfig(n,e.k[n]);}}");
}

QString DesktopWallpapers::reloadScript(const QStringList &geometryKeys)
{
    if (geometryKeys.isEmpty())
        return {};
    QStringList entries;
    for (const QString &key : geometryKeys)
        entries << jsString(key) + QStringLiteral(":1");
    return QStringLiteral("var m={%1};").arg(entries.join(QLatin1Char(',')))
           + containmentLoopJs()
           + QStringLiteral("if(!m.hasOwnProperty(key))continue;d.reloadConfig();}");
}

void DesktopWallpapers::apply(int desktop)
{
    const QString write = applyScript(desktop);
    if (write.isEmpty())
        return;
    // Two **separate** evaluateScript calls. Writing the config and reloading it
    // in the same call does not repaint on this Plasma (see AGENTS.md, the
    // next-wall.sh finding); as two calls it does.
    PlasmaScript::run(write);
    PlasmaScript::run(reloadScript(geometryKeys().values()));
    setApplied(true);
}

void DesktopWallpapers::restore()
{
    const QString write = restoreScript();
    if (write.isEmpty()) {
        setApplied(false);
        return;
    }
    PlasmaScript::run(write);
    PlasmaScript::run(reloadScript(geometryKeys().values()));
    // The snapshot itself is deliberately *not* cleared: it is the last known
    // good desktop-1 configuration, and keeping it is what guarantees we can
    // always put desktop 1 back.
    setApplied(false);
}
