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

    // A previous run may have died with a generated scheme on the desktop. The
    // flag is persisted precisely so that this is recoverable: put the user's
    // own scheme back rather than leaving them stuck on a temporary one.
    if (applied() && !enabled())
        restoreDefaults();
    else if (enabled() && !DockConfig::anyDarkModeActive())
        refresh();
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
    clearDockColors();
    if (m_appearance && defaultsSaved()) {
        const QString colors = defaultColorScheme();
        if (!colors.isEmpty())
            m_appearance->applyColorScheme(colors);
    }
    // Unlike the color scheme, an empty icon id is meaningful: it clears the
    // override so the dock follows KDE again. So this one is not gated on
    // being non-empty.
    if (m_theme && defaultsSaved())
        m_theme->setIconTheme(defaultIconTheme());
    removeGeneratedFiles();
    setApplied(false);
}

void AutoColorScheme::clearDockColors()
{
    if (!m_manager)
        return;
    const QStringList docks = DockConfig::knownDocks();
    for (const QString &dockId : docks) {
        if (DockConfig *cfg = m_manager->configFor(dockId))
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
    if (imageByScreen.isEmpty())
        return;

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
        for (auto it = byScreen.constBegin(); it != byScreen.constEnd(); ++it)
            applyToDocks(it.key(), it.value());
    } else {
        clearDockColors();
    }

    if (systemScheme()) {
        const QString screen = systemScreenName();
        // Fall back to any screen we did resolve rather than doing nothing: a
        // one-monitor session whose connector did not match still wants a scheme.
        const SchemeColors scheme =
            byScreen.contains(screen) ? byScreen.value(screen) : byScreen.constBegin().value();
        applySystem(scheme);
    }
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
    const QString stale = schemeFilePath(slot == 0 ? kSchemeIdB : kSchemeIdA);
    QTimer::singleShot(3000, this, [stale] { QFile::remove(stale); });

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
