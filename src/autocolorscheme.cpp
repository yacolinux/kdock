#include "autocolorscheme.h"

#include "appearancecontrol.h"
#include "brightnesscontrol.h"
#include "desktopwallpapers.h"
#include "dockconfig.h"
#include "dockmanager.h"
#include "plasmascript.h"
#include "theme.h"
#include "virtualdesktops.h"

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace {

const QChar kField = QChar(0x1f);
const QChar kRecord = QChar(0x1e);

QSettings shared()
{
    return QSettings(DockConfig::settingsFilePath(), QSettings::IniFormat);
}

// Every key is "ColorAuto/<name>": QSettings maps only the FIRST '/' onto a
// section, so nothing variable (a connector name, a path) may ever go into the
// second half — see CLAUDE.md. Everything variable here travels as a value.
QString key(const char *name)
{
    return QLatin1String("ColorAuto/") + QLatin1String(name);
}

QVariant readKey(const char *name, const QVariant &fallback = QVariant())
{
    return shared().value(key(name), fallback);
}

void writeKey(const char *name, const QVariant &value)
{
    QSettings s = shared();
    s.setValue(key(name), value);
}

// Suspension is not the same thing as being off: it remembers that the user had
// ColorAuto on and dark mode took over, so leaving dark mode can put it back.
bool suspendedByDark()
{
    return readKey("suspendedByDark", false).toBool();
}

void setSuspendedByDark(bool on)
{
    writeKey("suspendedByDark", on);
}

// Which of the two generated schemes was applied last. The next one has to be
// the other one or plasma-apply-colorscheme ignores the request entirely.
int lastSlot()
{
    return readKey("lastSlot", 1).toInt() == 0 ? 0 : 1;
}

void setLastSlot(int slot)
{
    writeKey("lastSlot", slot);
}

void setApplied(bool on)
{
    writeKey("applied", on);
}

void setManual(bool on)
{
    writeKey("manual", on);
}

// Which candidate color of the wallpaper the next generation seeds from. Only
// the manual path moves it; an automatic run always wants the predominant one.
int variant()
{
    return readKey("variant", 0).toInt();
}

void setVariant(int v)
{
    writeKey("variant", v);
}

// Path of the wallpaper the last generation was built from, so a *new* image
// resets the variant counter. Without that, changing the wallpaper after eight
// clicks would keep serving variant 8 — of the new image — which reads as "it
// picked a weird color" rather than as the predominant one.
QString lastImage()
{
    return readKey("lastImage").toString();
}

void setLastImage(const QString &path)
{
    writeKey("lastImage", path);
}

// Plasma's applet config: the one file every wallpaper change lands in, no
// matter who made it.
QString appletsrcPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/plasma-org.kde.plasma.desktop-appletsrc");
}

// KDE's default wallpapers are *packages*: the containment's Image points at a
// directory and the picture lives under contents/images/. Resolve that here or
// every stock wallpaper samples as "unreadable" and the feature looks broken.
QString resolveImagePath(const QString &raw)
{
    if (raw.isEmpty())
        return {};
    QString path = raw;
    if (path.startsWith(QLatin1String("file:")))
        path = QUrl(path).toLocalFile();
    if (path.isEmpty())
        return {};

    QFileInfo fi(path);
    if (fi.isFile())
        return fi.absoluteFilePath();
    if (!fi.isDir())
        return {};

    static const QStringList filters = {
        QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"),
        QStringLiteral("*.webp"), QStringLiteral("*.bmp")};
    // The biggest file in contents/images is the highest resolution variant,
    // which is also the most representative one to sample.
    for (const QString &sub : {QStringLiteral("contents/images"), QString()}) {
        const QString dir = sub.isEmpty() ? path : path + QLatin1Char('/') + sub;
        const auto files = QDir(dir).entryInfoList(filters, QDir::Files, QDir::Size);
        if (!files.isEmpty())
            return files.first().absoluteFilePath();
    }
    return {};
}

// Reads every connected containment's wallpaper plugin plus its current Image.
// The cut-down sibling of DesktopWallpapers::captureScriptJs(): that one has to
// round-trip every key of the group, this one only needs the picture.
QString readScriptJs()
{
    return QStringLiteral(
        "var F=String.fromCharCode(31);var R=String.fromCharCode(30);var out='';"
        "var ds=desktops();"
        "for(var j=0;j<ds.length;j++){var d=ds[j];if(d.screen<0)continue;"
        "var g=screenGeometry(d.screen);var key=g.x+','+g.y;"
        "var p=d.wallpaperPlugin;d.currentConfigGroup=['Wallpaper',p,'General'];"
        "out+=(out?R:'')+key+F+p+F+d.readConfig('Image');}"
        "print(out);");
}

} // namespace

const QString AutoColorScheme::kSchemeIdA = QStringLiteral("KdockColorAuto1");
const QString AutoColorScheme::kSchemeIdB = QStringLiteral("KdockColorAuto2");
const QString AutoColorScheme::InternalMonitor = QStringLiteral("internal");

AutoColorScheme::AutoColorScheme(Theme *theme, AppearanceControl *appearance,
                                 DockManager *manager, VirtualDesktops *desktops,
                                 QObject *parent)
    : QObject(parent)
    , m_theme(theme)
    , m_appearance(appearance)
    , m_manager(manager)
    , m_desktops(desktops)
{
    m_debounce.setSingleShot(true);
    // Long enough to outlast the asynchronous half of a wallpaper change: a
    // slideshow advance is KDE picking the next image after kdock cycled the
    // plugin, so a read that lands too early returns the previous path. Same
    // order of magnitude as the ~900 ms the Plasma apply tools take.
    m_debounce.setInterval(1200);
    connect(&m_debounce, &QTimer::timeout, this, &AutoColorScheme::readWallpapers);

    connect(DockConfig::darkModeNotifier(), &DarkModeNotifier::changed, this,
            &AutoColorScheme::onDarkModePing);
    m_lastDark = DockConfig::anyDarkModeActive();

    if (m_desktops) {
        // Requirement 1b: with wallpapers per virtual desktop the image changes
        // without anyone touching the wallpaper controls, so the switch itself
        // is the trigger.
        connect(m_desktops, &VirtualDesktops::currentChanged, this,
                [this](int) { refresh(); });
    }

    // The trigger that actually matters. The in-process ones only fire for
    // wallpaper changes kdock itself made, and the usual way to change a
    // wallpaper is *not* that: a Script Runner script talking qdbus6 straight
    // to plasmashell, KDE's own slideshow timer, or System Settings all leave
    // kdock completely out of the loop. Watching Plasma's config catches every
    // one of them. Deliberately not gated on enabled(): the gate lives in
    // refresh(), and gating here would mean ticking the box does nothing until
    // the *next* wallpaper change.
    connect(&m_wallpaperWatch, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &) {
                armWallpaperWatch();
                refresh();
            });
    connect(&m_wallpaperWatch, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &) {
                // The file can be missing for an instant mid-rename; re-arming
                // from the directory is what picks the new inode back up.
                armWallpaperWatch();
                refresh();
            });
    armWallpaperWatch();

    // A previous run may have died with a generated scheme on the desktop. The
    // flag is persisted precisely so that this is recoverable: put the user's
    // own scheme back rather than leaving them stuck on a temporary one.
    //
    // Not when it was generated by hand, though: that is an explicit choice of
    // the user's ("generate a color and manage it yourself"), and undoing it on
    // every dock restart would be the opposite of what the button promises.
    if (applied() && !enabled() && !manual())
        restoreDefaults();
    else if (enabled() && !DockConfig::anyDarkModeActive())
        refresh();
}

void AutoColorScheme::armWallpaperWatch()
{
    const QString path = appletsrcPath();
    // addPath() on an already-watched path is a no-op that logs, so ask first.
    if (!m_wallpaperWatch.files().contains(path) && QFile::exists(path))
        m_wallpaperWatch.addPath(path);
    const QString dir = QFileInfo(path).absolutePath();
    if (!m_wallpaperWatch.directories().contains(dir) && QFile::exists(dir))
        m_wallpaperWatch.addPath(dir);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

bool AutoColorScheme::enabled()
{
    return readKey("enabled", false).toBool();
}

bool AutoColorScheme::colorDocks()
{
    return readKey("colorDocks", true).toBool();
}

void AutoColorScheme::setColorDocks(bool on)
{
    writeKey("colorDocks", on);
}

bool AutoColorScheme::systemScheme()
{
    return readKey("systemScheme", true).toBool();
}

void AutoColorScheme::setSystemScheme(bool on)
{
    writeKey("systemScheme", on);
}

QString AutoColorScheme::systemMonitor()
{
    return readKey("systemMonitor").toString();
}

void AutoColorScheme::setSystemMonitor(const QString &value)
{
    writeKey("systemMonitor", value);
}

int AutoColorScheme::lightness()
{
    return readKey("lightness", int(WallpaperColors::Options::Auto)).toInt();
}

void AutoColorScheme::setLightness(int mode)
{
    writeKey("lightness", mode);
}

int AutoColorScheme::selectionMode()
{
    return readKey("selectionMode", int(WallpaperColors::Options::DefaultGrays)).toInt();
}

void AutoColorScheme::setSelectionMode(int mode)
{
    writeKey("selectionMode", mode);
}

QColor AutoColorScheme::selectionLight()
{
    const QColor c(readKey("selectionLight", QStringLiteral("#3a3a3a")).toString());
    return c.isValid() ? c : QColor(0x3A, 0x3A, 0x3A);
}

void AutoColorScheme::setSelectionLight(const QColor &c)
{
    if (c.isValid())
        writeKey("selectionLight", c.name());
}

QColor AutoColorScheme::selectionDark()
{
    const QColor c(readKey("selectionDark", QStringLiteral("#d6d6d6")).toString());
    return c.isValid() ? c : QColor(0xD6, 0xD6, 0xD6);
}

void AutoColorScheme::setSelectionDark(const QColor &c)
{
    if (c.isValid())
        writeKey("selectionDark", c.name());
}

bool AutoColorScheme::iconsetEnabled(bool dark)
{
    return readKey(dark ? "iconsetDarkEnabled" : "iconsetLightEnabled", false).toBool();
}

void AutoColorScheme::setIconsetEnabled(bool dark, bool on)
{
    writeKey(dark ? "iconsetDarkEnabled" : "iconsetLightEnabled", on);
}

QString AutoColorScheme::iconsetValue(bool dark)
{
    return readKey(dark ? "iconsetDark" : "iconsetLight").toString();
}

void AutoColorScheme::setIconsetValue(bool dark, const QString &id)
{
    writeKey(dark ? "iconsetDark" : "iconsetLight", id);
}

bool AutoColorScheme::defaultsSaved()
{
    return readKey("defaultsSaved", false).toBool();
}

QString AutoColorScheme::defaultColorScheme()
{
    return readKey("defaultColorScheme").toString();
}

QString AutoColorScheme::defaultIconTheme()
{
    return readKey("defaultIconTheme").toString();
}

bool AutoColorScheme::applied()
{
    return readKey("applied", false).toBool();
}

bool AutoColorScheme::manual()
{
    return readKey("manual", false).toBool();
}

bool AutoColorScheme::isOwnSchemeId(const QString &id)
{
    return id == kSchemeIdA || id == kSchemeIdB;
}

QString AutoColorScheme::userColorScheme()
{
    return defaultsSaved() ? defaultColorScheme() : QString();
}

void AutoColorScheme::captureDefaults()
{
    if (!m_appearance)
        return;
    QString colors = m_appearance->currentColorScheme();
    // Never record one of ours as "what the user had" — that is how a restore
    // ends up restoring the temporary scheme forever.
    if (isOwnSchemeId(colors))
        colors = defaultColorScheme();
    // The dock's icon-theme override *verbatim*, empty included: empty means
    // "follow KDE", and storing the resolved KDE id instead would turn a
    // following dock into a pinned one on the way back. It is a single
    // process-wide value (Theme::setIconTheme), so there is no per-dock choice.
    const QString icons = m_theme ? m_theme->iconTheme() : QString();

    QSettings s = shared();
    s.setValue(key("defaultColorScheme"), colors);
    s.setValue(key("defaultIconTheme"), icons);
    s.setValue(key("defaultsSaved"), true);
    s.sync();
    emit changed();
}

// ---------------------------------------------------------------------------
// On / off
// ---------------------------------------------------------------------------

void AutoColorScheme::setEnabled(bool on)
{
    if (on == enabled())
        return;
    if (on && !defaultsSaved())
        captureDefaults();
    writeKey("enabled", on);
    if (on)
        refreshNow();
    else
        restoreDefaults();
    emit changed();
}

void AutoColorScheme::restoreDefaults()
{
    // The dock colors are ours unconditionally — they are a read-time override
    // and nobody else writes them.
    clearDockColors();

    // The *system* scheme is only ours to put back while applied() says so.
    // After saveCurrentScheme() it is the user's own permanent scheme, and
    // restoring over it would throw away what they just asked to keep.
    if (applied() && defaultsSaved()) {
        if (m_appearance) {
            const QString colors = defaultColorScheme();
            if (!colors.isEmpty())
                m_appearance->applyColorScheme(colors);
        }
        // Unlike the color scheme, an empty icon id is meaningful: it clears
        // the override so the dock follows KDE again. So this one is not gated
        // on being non-empty.
        if (m_theme)
            m_theme->setIconTheme(defaultIconTheme());
    }

    removeGeneratedFiles();
    setApplied(false);
    setManual(false);
}

void AutoColorScheme::clearDockColors(const QSet<QString> &except)
{
    if (!m_manager)
        return;
    // Every config the manager has handed out, not a list of dockIds: see
    // DockManager::liveConfigs() for why walking a list is what broke this.
    const auto configs = m_manager->liveConfigs();
    for (DockConfig *cfg : configs) {
        if (cfg && !except.contains(cfg->dockId()))
            cfg->clearAutoColors();
    }
}

void AutoColorScheme::removeGeneratedFiles()
{
    QFile::remove(schemeFilePath(kSchemeIdA));
    QFile::remove(schemeFilePath(kSchemeIdB));
}

void AutoColorScheme::onDarkModePing()
{
    const bool dark = DockConfig::anyDarkModeActive();
    if (dark == m_lastDark)
        return; // the accent color pings this too; it has to be idempotent
    m_lastDark = dark;

    if (dark) {
        if (!enabled())
            return;
        // Dark mode owns the system scheme while it is on. Stand down and
        // remember to come back, which is what "se activa nuevamente si ya
        // estaba activo antes" means.
        setSuspendedByDark(true);
        setEnabled(false);
        return;
    }

    if (!suspendedByDark())
        return;
    setSuspendedByDark(false);
    // DarkModeAppearance puts the previous scheme back with startDetached, so
    // it lands ~900 ms from now; re-applying ours before that would be undone.
    QTimer::singleShot(1500, this, [this] {
        if (!DockConfig::anyDarkModeActive())
            setEnabled(true);
    });
}

// ---------------------------------------------------------------------------
// Reading and applying
// ---------------------------------------------------------------------------

void AutoColorScheme::refresh()
{
    if (!enabled() || DockConfig::anyDarkModeActive())
        return;
    m_debounce.start();
}

void AutoColorScheme::refreshNow()
{
    if (!enabled() || DockConfig::anyDarkModeActive())
        return;
    m_debounce.stop();
    m_manualRun = false;
    readWallpapers();
}

void AutoColorScheme::generateNow()
{
    // No enabled() gate on purpose: this is the widget, the panel card and the
    // tab button, and they work with the feature off. Dark mode does still win
    // — it owns the desktop's appearance while it is on, and fighting it would
    // just leave the two overwriting each other.
    if (DockConfig::anyDarkModeActive())
        return;
    // Without a snapshot there is no way back, and a manual generation is
    // exactly the case where the user has not been through setEnabled(true).
    if (!defaultsSaved())
        captureDefaults();
    setVariant(variant() + 1);
    m_debounce.stop();
    m_manualRun = true;
    readWallpapers();
}

void AutoColorScheme::readWallpapers()
{
    if (m_reading)
        return; // one round trip in flight is enough; the debounce coalesced the rest
    m_reading = true;

    auto *watcher = new QDBusPendingCallWatcher(PlasmaScript::evaluate(readScriptJs()), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                m_reading = false;
                QDBusPendingReply<QString> reply = *w;
                if (reply.isError())
                    return; // no plasmashell: leave everything as it is

                // "x,y" -> connector, the same mapping the wallpaper code
                // already uses; geometryKeys() is shared rather than copied.
                QHash<QString, QString> byGeometry;
                const auto keys = DesktopWallpapers::geometryKeys();
                for (auto it = keys.constBegin(); it != keys.constEnd(); ++it)
                    byGeometry.insert(it.value(), it.key());

                QHash<QString, QString> imageByScreen;
                const QStringList records = reply.value().split(kRecord, Qt::SkipEmptyParts);
                for (const QString &record : records) {
                    const QStringList fields = record.split(kField);
                    if (fields.size() < 3)
                        continue;
                    const QString screen = byGeometry.value(fields.at(0));
                    if (screen.isEmpty())
                        continue;
                    const QString path = resolveImagePath(fields.at(2));
                    if (!path.isEmpty())
                        imageByScreen.insert(screen, path);
                }
                applyPalettes(imageByScreen);
            });
}

void AutoColorScheme::applyPalettes(const QHash<QString, QString> &imageByScreen)
{
    const bool manualRun = m_manualRun;
    m_manualRun = false;
    // The read is a D-Bus round trip, so the feature may have been switched off
    // (or dark mode turned on) while it was in flight. Applying now would put a
    // scheme up *after* restoreDefaults() already put the user's back, and the
    // user would be left with the thing they just turned off. A manual run is
    // exempt: it never depended on the switch in the first place.
    if (!manualRun && (!enabled() || DockConfig::anyDarkModeActive()))
        return;
    if (imageByScreen.isEmpty())
        return;

    // A new wallpaper starts the variant cycle over. Judged on the monitor that
    // drives the system scheme, because that is the one whose color the user is
    // looking at when they press the button again. Only an automatic run
    // resets: a manual one just asked for the next variant.
    const QString leadImage = imageByScreen.value(systemScreenName());
    if (!manualRun && !leadImage.isEmpty() && leadImage != lastImage())
        setVariant(0);
    if (!leadImage.isEmpty())
        setLastImage(leadImage);

    const WallpaperColors::Options opt = options();
    QHash<QString, SchemeColors> byScreen;
    for (auto it = imageByScreen.constBegin(); it != imageByScreen.constEnd(); ++it) {
        const WallpaperPalette pal = WallpaperColors::sample(it.value());
        if (!pal.valid)
            continue;
        byScreen.insert(it.key(), WallpaperColors::buildScheme(pal, opt));
    }
    if (byScreen.isEmpty())
        return;

    if (colorDocks()) {
        // Colour the docks of every screen we resolved, and clear the rest.
        // Clearing the rest matters: a monitor that got unplugged (or whose
        // wallpaper stopped being readable) would otherwise keep its last
        // generated colour for the life of the process, and that colour
        // outranks the dock's own panelColor — so the dock would be stuck on a
        // colour nothing could change.
        QSet<QString> coloured;
        for (auto it = byScreen.constBegin(); it != byScreen.constEnd(); ++it) {
            applyToDocks(it.key(), it.value());
            const auto ids = m_manager ? m_manager->enabledDocksForScreen(it.key())
                                       : QStringList();
            for (const QString &id : ids)
                coloured.insert(id);
        }
        clearDockColors(coloured);
    } else {
        clearDockColors();
    }

    // The scheme that speaks for the session: the monitor that drives it, or
    // any screen we did resolve rather than doing nothing (a one-monitor
    // session whose connector did not match still wants a scheme).
    const QString screen = systemScreenName();
    const SchemeColors lead =
        byScreen.contains(screen) ? byScreen.value(screen) : byScreen.constBegin().value();
    // Remembered whether or not it goes to the system: "save" writes out what
    // was last generated, and with colorDocks-only that is still this.
    m_lastScheme = lead;
    m_haveScheme = true;

    if (systemScheme())
        applySystem(lead);
    // A manual generation owns the system too — that is the whole point of the
    // button working with the feature off — and says so, so the startup cleanup
    // leaves it alone.
    if (manualRun)
        setManual(true);
    else if (manual())
        setManual(false);

    emit changed();
}

void AutoColorScheme::applyToDocks(const QString &screen, const SchemeColors &scheme)
{
    if (!m_manager)
        return;
    const QStringList docks = m_manager->enabledDocksForScreen(screen);
    for (const QString &dockId : docks) {
        if (DockConfig *cfg = m_manager->configFor(dockId))
            cfg->setAutoColors(scheme.windowBg, scheme.decoration);
    }
}

void AutoColorScheme::applySystem(const SchemeColors &scheme)
{
    if (!m_appearance)
        return;

    const int slot = lastSlot() == 0 ? 1 : 0;
    const QString id = slot == 0 ? kSchemeIdA : kSchemeIdB;
    const QString path = schemeFilePath(id);
    const QString name = QStringLiteral("kdock ColorAuto %1").arg(slot + 1);
    if (!WallpaperColors::writeSchemeFile(scheme, path, id, name))
        return;

    m_appearance->applyColorScheme(id);
    setLastSlot(slot);
    setApplied(true);

    // Drop the one that is no longer current, so System Settings shows a single
    // generated scheme instead of the two kde-material-you-colors is known for.
    // Delayed: plasma-apply-colorscheme is startDetached and still enumerating
    // the directory for a moment after we return.
    //
    // The re-check inside the timer is not paranoia. These are not cancelled,
    // so a second apply within the three seconds leaves an older timer pointing
    // at the slot that apply just wrote — switching the feature off and on
    // quickly used to delete the file that was *currently applied*.
    const int staleSlot = slot == 0 ? 1 : 0;
    const QString stale = schemeFilePath(staleSlot == 0 ? kSchemeIdA : kSchemeIdB);
    QTimer::singleShot(3000, this, [stale, staleSlot] {
        if (lastSlot() == staleSlot)
            return; // it became the current one in the meantime
        QFile::remove(stale);
    });

    if (m_theme) {
        const bool dark = scheme.dark;
        if (iconsetEnabled(dark)) {
            const QString icons = iconsetValue(dark);
            if (!icons.isEmpty())
                m_theme->setIconTheme(icons);
        } else if (defaultsSaved()) {
            m_theme->setIconTheme(defaultIconTheme());
        }
    }
}

QString AutoColorScheme::saveCurrentScheme()
{
    if (!m_haveScheme) {
        // Nothing generated in this process yet. Kick one off and let the user
        // press save again — generating is a D-Bus round trip, so there is no
        // scheme to write out synchronously here.
        generateNow();
        return {};
    }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + QStringLiteral("/color-schemes");
    QDir().mkpath(dir);

    // First free number, so saving twice never overwrites the earlier keep.
    QString id;
    QString path;
    for (int n = 1; n < 1000; ++n) {
        id = QStringLiteral("kdock-%1").arg(n);
        path = dir + QLatin1Char('/') + id + QStringLiteral(".colors");
        if (!QFile::exists(path))
            break;
        path.clear();
    }
    if (path.isEmpty())
        return {};

    if (!WallpaperColors::writeSchemeFile(m_lastScheme, path, id,
                                          QStringLiteral("kdock %1").arg(id.mid(6))))
        return {};

    if (m_appearance)
        m_appearance->applyColorScheme(id);

    // Hand the system scheme over: from here on it is the user's, so neither
    // switching the feature off nor the startup cleanup may overwrite it. The
    // generated temporaries are no longer what is applied, so they go.
    removeGeneratedFiles();
    setApplied(false);
    setManual(false);
    emit changed();
    return id;
}

QVariantMap AutoColorScheme::previewEntry() const
{
    QColor bg, fg, sel;
    if (m_haveScheme) {
        bg = m_lastScheme.windowBg;
        fg = m_lastScheme.windowFg;
        sel = m_lastScheme.selectionBg;
    } else {
        // Nothing generated in this process: read back the one that is applied,
        // so the tab has something to show the moment it opens.
        const QString path = schemeFilePath(lastSlot() == 0 ? kSchemeIdA : kSchemeIdB);
        if (!QFile::exists(path))
            return {};
        QSettings ini(path, QSettings::IniFormat);
        const auto colorAt = [&ini](const char *key) {
            // "r,g,b" comes back as a QStringList (QSettings splits on commas).
            const QStringList v = ini.value(QLatin1String(key)).toStringList();
            return v.size() == 3 ? QColor(v.at(0).toInt(), v.at(1).toInt(), v.at(2).toInt())
                                 : QColor();
        };
        bg = colorAt("Colors:Window/BackgroundNormal");
        fg = colorAt("Colors:Window/ForegroundNormal");
        sel = colorAt("Colors:Selection/BackgroundNormal");
    }
    if (!bg.isValid())
        return {};

    return QVariantMap{
        {QStringLiteral("id"),
         QStringLiteral("colorauto:%1%2%3").arg(bg.name(), fg.name(), sel.name())},
        {QStringLiteral("bg"), bg.name()},
        {QStringLiteral("fg"), fg.name()},
        {QStringLiteral("sel"), sel.name()}};
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

WallpaperColors::Options AutoColorScheme::options()
{
    WallpaperColors::Options opt;
    opt.lightness = lightness();
    opt.selectionMode = selectionMode();
    opt.selectionLight = selectionLight();
    opt.selectionDark = selectionDark();
    opt.variant = variant();
    return opt;
}

QString AutoColorScheme::schemeFilePath(const QString &id)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/color-schemes/") + id + QStringLiteral(".colors");
}

QString AutoColorScheme::systemScreenName() const
{
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return {};

    // The built-in panel, by connector prefix. PowerDevil's IsInternal would be
    // the authoritative answer but it does not report a connector at all (see
    // below), so the name is what there is.
    const auto internalScreen = [&screens]() -> QString {
        for (QScreen *s : screens) {
            const QString n = s->name();
            if (n.startsWith(QLatin1String("eDP"), Qt::CaseInsensitive)
                || n.startsWith(QLatin1String("LVDS"), Qt::CaseInsensitive)
                || n.startsWith(QLatin1String("DSI"), Qt::CaseInsensitive))
                return n;
        }
        QScreen *p = QGuiApplication::primaryScreen();
        return p ? p->name() : screens.first()->name();
    };

    const QString configured = systemMonitor();
    if (configured == InternalMonitor)
        return internalScreen();
    if (!configured.isEmpty()) {
        for (QScreen *s : screens) {
            if (s->name() == configured)
                return s->name();
        }
        return internalScreen(); // the chosen monitor is unplugged
    }

    // Empty: follow the brightness widget's own target. That preference is
    // stored as a PowerDevil *label* ("Samsung Electric Company S22F350"), not
    // as a connector — PowerDevil's display objects have no connector at all,
    // verified on this session. The bridge is the screen's own make and model,
    // which is exactly what the label is built from; when it does not match,
    // the tab's combo is the way out.
    const QString target =
        m_manager && m_manager->brightness() ? m_manager->brightness()->wheelTarget() : QString();
    if (target.isEmpty() || target == BrightnessControl::InternalTarget)
        return internalScreen();
    for (QScreen *s : screens) {
        const QString label = s->manufacturer() + QLatin1Char(' ') + s->model();
        if (label.compare(target, Qt::CaseInsensitive) == 0)
            return s->name();
    }
    return internalScreen();
}
