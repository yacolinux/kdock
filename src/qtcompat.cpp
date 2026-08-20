#include "qtcompat.h"

#include "dockconfig.h"
#include "theme.h"

#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantMap>

namespace {

const char *kEnabledKey = "QtCompat/enabled";
const char *kColorsKey = "QtCompat/colors";
const char *kIconsKey = "QtCompat/icons";
const char *kFontsKey = "QtCompat/fonts";
const char *kFontKey = "QtCompat/font";
const char *kFixedFontKey = "QtCompat/fixedFont";

QSettings shared()
{
    return QSettings(DockConfig::settingsFilePath(), QSettings::IniFormat);
}

// The lxqt.conf keys, in QSettings form. `icon_theme` is deliberately bare:
// QSettings maps the top level onto the file's [General] section, which is
// where the plugin reads it from.
const char *kLxqtIconThemeKey = "icon_theme";
const char *kLxqtFontKey = "Qt/font";
const char *kLxqtFixedFontKey = "Qt/fixedFont";

QSettings lxqtSettings()
{
    return QSettings(QSettings::UserScope, QStringLiteral("lxqt"), QStringLiteral("lxqt"));
}

// "r,g,b[,a]" — the format kdeglobals uses. QSettings hands a value with commas
// back as a QStringList, hence the two branches (same parser as Theme).
QColor parseKdeColor(const QVariant &value, const QColor &fallback)
{
    QStringList parts;
    if (value.userType() == QMetaType::QStringList)
        parts = value.toStringList();
    else if (value.isValid())
        parts = value.toString().split(QLatin1Char(','));
    if (parts.size() < 3)
        return fallback;
    return QColor(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(),
                  parts[2].trimmed().toInt());
}

// The ten keys of lxqt.conf's [Palette] group, in the order the plugin reads
// them, paired with the kdeglobals key each one translates. Kept as one table
// so the tab can show exactly what will be written without repeating the map.
struct Mapping
{
    const char *lxqtKey;
    const char *kdeKey;
    QPalette::ColorRole fallbackRole;
};

const Mapping kMappings[] = {
    {"window_color", "Colors:Window/BackgroundNormal", QPalette::Window},
    {"window_text_color", "Colors:Window/ForegroundNormal", QPalette::WindowText},
    {"base_color", "Colors:View/BackgroundNormal", QPalette::Base},
    {"text_color", "Colors:View/ForegroundNormal", QPalette::Text},
    {"highlight_color", "Colors:Selection/BackgroundNormal", QPalette::Highlight},
    {"highlighted_text_color", "Colors:Selection/ForegroundNormal", QPalette::HighlightedText},
    {"link_color", "Colors:View/ForegroundLink", QPalette::Link},
    {"link_visited_color", "Colors:View/ForegroundVisited", QPalette::LinkVisited},
    {"tooltip_base_color", "Colors:Tooltip/BackgroundNormal", QPalette::ToolTipBase},
    {"tooltip_text_color", "Colors:Tooltip/ForegroundNormal", QPalette::ToolTipText},
};

} // namespace

QtCompat::QtCompat(Theme *theme, QObject *parent)
    : QObject(parent)
    , m_theme(theme)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250);
    connect(&m_debounce, &QTimer::timeout, this, &QtCompat::apply);

    // Theme already watches kdeglobals, so its signal is every trigger this
    // needs: whoever changed the scheme — this tab's picker, the dock widget,
    // the Colores tab, ColorAuto, dark mode — lands here.
    if (m_theme)
        connect(m_theme, &Theme::changed, this, [this] {
            if (enabled())
                m_debounce.start();
        });

    // Nothing is applied at construction on purpose. The palette on disk is
    // already whatever the last run left; re-applying it would be a no-op, and
    // applying it when kdeglobals moved while kdock was down is what the first
    // Theme::changed (or the tab's button) is for.
}

bool QtCompat::enabled()
{
    return shared().value(QLatin1String(kEnabledKey), false).toBool();
}

void QtCompat::setEnabled(bool on)
{
    if (on == enabled())
        return;
    QSettings s = shared();
    s.setValue(QLatin1String(kEnabledKey), on);
    s.sync();
    if (on)
        applyNow();
    emit changed();
}

bool QtCompat::colorsEnabled()
{
    return shared().value(QLatin1String(kColorsKey), true).toBool();
}

void QtCompat::setColorsEnabled(bool on)
{
    QSettings s = shared();
    s.setValue(QLatin1String(kColorsKey), on);
    s.sync();
    applyNow();
    emit changed();
}

bool QtCompat::iconsEnabled()
{
    return shared().value(QLatin1String(kIconsKey), true).toBool();
}

void QtCompat::setIconsEnabled(bool on)
{
    QSettings s = shared();
    s.setValue(QLatin1String(kIconsKey), on);
    s.sync();
    applyNow();
    emit changed();
}

bool QtCompat::fontsEnabled()
{
    return shared().value(QLatin1String(kFontsKey), false).toBool();
}

void QtCompat::setFontsEnabled(bool on)
{
    QSettings s = shared();
    s.setValue(QLatin1String(kFontsKey), on);
    s.sync();
    applyNow();
    emit changed();
}

QString QtCompat::font(FontKind kind)
{
    return shared()
        .value(QLatin1String(kind == FixedFont ? kFixedFontKey : kFontKey))
        .toString();
}

void QtCompat::setFont(FontKind kind, const QString &value)
{
    QSettings s = shared();
    s.setValue(QLatin1String(kind == FixedFont ? kFixedFontKey : kFontKey), value);
    s.sync();
    applyNow();
    emit changed();
}

QString QtCompat::fontFor(FontKind kind)
{
    const QString stored = font(kind);
    if (!stored.isEmpty())
        return stored;
    // Nothing chosen yet: show what LXQt has, so the picker opens on the font
    // the user is actually looking at. Opening it on the Qt default would make
    // the first OK change the desktop's font to something never chosen.
    QSettings lxqt = lxqtSettings();
    lxqt.sync();
    return lxqt.value(QLatin1String(kind == FixedFont ? kLxqtFixedFontKey : kLxqtFontKey))
        .toString();
}

QString QtCompat::lxqtConfPath()
{
    // Built exactly the way the plugin builds it (QSettings::UserScope with
    // organization and application both "lxqt"), so this is the file it has its
    // QFileSystemWatcher on and not a look-alike.
    return QSettings(QSettings::UserScope, QStringLiteral("lxqt"), QStringLiteral("lxqt"))
        .fileName();
}

bool QtCompat::lxqtPlatformTheme()
{
    return qEnvironmentVariable("QT_QPA_PLATFORMTHEME").compare(QLatin1String("lxqt"),
                                                                Qt::CaseInsensitive)
           == 0;
}

QList<QPair<QString, QString>> QtCompat::buildPalette() const
{
    QList<QPair<QString, QString>> out;

    const QString path =
        QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kdeglobals"));
    if (path.isEmpty())
        return out;
    QSettings kde(path, QSettings::IniFormat);
    // QSettings shares one cached copy per path and revalidates it by (mtime,
    // size), with the mtime in milliseconds. This process reads kdeglobals from
    // three places (Theme, AppearanceControl, here), and a scheme change can
    // leave the file exactly the same size ("17,18,19" -> "90,91,92"), so a
    // rewrite landing in the same millisecond as the cached read is served
    // stale — the translation would silently come out as the previous scheme.
    // A real plasma-apply-colorscheme run is seconds away from our reads, so
    // this is a guard and not a fix for anything observed; the unit tests do
    // not exercise it (reproducing it needs a same-size write inside the same
    // millisecond, which is what made the first version of the propagation test
    // fail half the time). It costs one stat().
    kde.sync();

    // The live palette is the fallback for a scheme that omits a group — the
    // .colors files of the wild do skip Colors:Tooltip often enough that
    // leaving those two keys out would make tooltips follow the *previous*
    // scheme for ever.
    const QPalette pal = qGuiApp ? qGuiApp->palette() : QPalette();

    for (const Mapping &m : kMappings) {
        const QColor c =
            parseKdeColor(kde.value(QLatin1String(m.kdeKey)), pal.color(m.fallbackRole));
        if (!c.isValid())
            continue;
        // #rrggbb: QColor::fromString() on the reading side takes it, and it is
        // the format lxqt-config-appearance itself writes.
        out.append({QString::fromLatin1(m.lxqtKey), c.name()});
    }
    return out;
}

QVariantList QtCompat::translation() const
{
    QVariantList out;
    const auto pairs = buildPalette();
    out.reserve(pairs.size());
    for (const auto &p : pairs)
        out.append(QVariantMap{{QStringLiteral("key"), p.first},
                               {QStringLiteral("color"), p.second}});
    return out;
}

QString QtCompat::iconTheme() const
{
    const QString path =
        QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kdeglobals"));
    if (path.isEmpty())
        return {};
    QSettings kde(path, QSettings::IniFormat);
    kde.sync(); // same guard as buildPalette()
    // "Icons/Theme", addressed normally — it is not the [General] section, so
    // none of the top-level mapping that bites ColorScheme applies here.
    return kde.value(QStringLiteral("Icons/Theme")).toString();
}

QString QtCompat::kdeColorScheme() const
{
    const QString path =
        QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kdeglobals"));
    if (path.isEmpty())
        return {};
    QSettings kde(path, QSettings::IniFormat);
    kde.sync();
    // NOT "General/ColorScheme": QSettings maps an INI file's [General] section
    // onto the top level, so that path silently reads nothing (the same trap
    // AppearanceControl::readCurrent() documents).
    return kde.value(QStringLiteral("ColorScheme")).toString();
}

QString QtCompat::kdeUiSettingsScheme() const
{
    const QString path =
        QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kdeglobals"));
    if (path.isEmpty())
        return {};
    QSettings kde(path, QSettings::IniFormat);
    kde.sync();
    // [UiSettings] is a section like any other, so it *is* addressed with the
    // group prefix — unlike the key above.
    return kde.value(QStringLiteral("UiSettings/ColorScheme")).toString();
}

void QtCompat::syncKdeUiSettings()
{
    const QString wanted = kdeColorScheme();
    if (wanted.isEmpty())
        return; // KDE has no scheme by name; nothing to propagate
    if (wanted == kdeUiSettingsScheme() || wanted == m_lastUiSettingsWrite)
        return;

    // kwriteconfig6 and not QSettings: kdeglobals is a KConfig file, with
    // syntax QSettings does not know ([$Version], the [$e]/[$i] entry markers,
    // localised keys like Name[es]=). Rewriting it with QSettings would round
    // trip all of that through a parser that never heard of it. This is also
    // why AppearanceControl reaches for the same tool.
    static const QStringList dirs = {QStringLiteral("/opt/kde/bin"), QStringLiteral("/usr/bin")};
    QString tool = QStandardPaths::findExecutable(QStringLiteral("kwriteconfig6"));
    if (tool.isEmpty())
        tool = QStandardPaths::findExecutable(QStringLiteral("kwriteconfig6"), dirs);
    if (tool.isEmpty())
        return;

    m_lastUiSettingsWrite = wanted;
    QProcess::startDetached(tool, {QStringLiteral("--file"), QStringLiteral("kdeglobals"),
                                   QStringLiteral("--group"), QStringLiteral("UiSettings"),
                                   QStringLiteral("--key"), QStringLiteral("ColorScheme"), wanted});
}

QList<QtCompat::Pending> QtCompat::pendingWrites() const
{
    QList<Pending> out;

    if (colorsEnabled()) {
        for (const auto &p : buildPalette())
            out.append({QStringLiteral("Palette/") + p.first, p.second});
    }
    if (iconsEnabled()) {
        // Only when KDE actually has one: an empty value would blank the key and
        // drop the desktop to the plugin's "oxygen" default, which is not what
        // "the icon set could not be read" should do.
        const QString icons = iconTheme();
        if (!icons.isEmpty())
            out.append({QString::fromLatin1(kLxqtIconThemeKey), icons});
    }
    if (fontsEnabled()) {
        // Each font is independent, and an empty one means "leave LXQt's alone"
        // rather than "clear it": the user may want to set the general font and
        // keep the monospace one the desktop came with.
        const QString general = font(GeneralFont);
        if (!general.isEmpty())
            out.append({QString::fromLatin1(kLxqtFontKey), general});
        const QString fixed = font(FixedFont);
        if (!fixed.isEmpty())
            out.append({QString::fromLatin1(kLxqtFixedFontKey), fixed});
    }
    return out;
}

void QtCompat::applyNow()
{
    m_debounce.stop();
    apply();
}

void QtCompat::apply()
{
    if (!enabled())
        return;

    // Before anything about LXQt: the half of the job that is about KDE's own
    // applications. They never look at lxqt.conf, so nothing below would ever
    // reach them; what they need is the [UiSettings] key. Guarded inside, and
    // gated by the colors switch because that is the scheme it propagates.
    if (colorsEnabled())
        syncKdeUiSettings();

    const auto pending = pendingWrites();
    if (pending.isEmpty())
        return; // every part off, or kdeglobals unreadable

    QSettings lxqt = lxqtSettings();
    // Same guard as in buildPalette(). It matters a little more here because
    // lxqt.conf is written by *another program* too (lxqt-config-appearance):
    // a cached copy from before that edit would make the dirty check below say
    // "nothing to do", which is exactly the case the tab's "Aplicar ahora"
    // button exists for.
    lxqt.sync();

    // Write only if at least one value actually differs. The platform theme
    // reacts per part and only when that part moved (paletteChanged_ for the
    // colors, an old/new comparison for the icon theme and the fonts), so an
    // identical rewrite re-themes nothing — and it would still churn the file
    // every time anything touched kdeglobals.
    bool dirty = false;
    for (const Pending &p : pending) {
        if (lxqt.value(p.key).toString().compare(p.value, Qt::CaseInsensitive) != 0) {
            dirty = true;
            break;
        }
    }
    if (!dirty)
        return;

    for (const Pending &p : pending)
        lxqt.setValue(p.key, p.value);
    lxqt.sync();

    emit changed();
}
