#include "qtcompat.h"

#include "dockconfig.h"
#include "theme.h"

#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantMap>

namespace {

const char *kEnabledKey = "QtCompat/enabled";

QSettings shared()
{
    return QSettings(DockConfig::settingsFilePath(), QSettings::IniFormat);
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

void QtCompat::applyNow()
{
    m_debounce.stop();
    apply();
}

void QtCompat::apply()
{
    if (!enabled())
        return;
    const auto pairs = buildPalette();
    if (pairs.isEmpty())
        return; // no kdeglobals: nothing honest to translate

    QSettings lxqt(QSettings::UserScope, QStringLiteral("lxqt"), QStringLiteral("lxqt"));
    // Same guard as in buildPalette(). It matters a little more here because
    // lxqt.conf is written by *another program* too (lxqt-config-appearance):
    // a cached copy from before that edit would make the dirty check below say
    // "nothing to do", which is exactly the case the tab's "Aplicar ahora"
    // button exists for.
    lxqt.sync();
    lxqt.beginGroup(QStringLiteral("Palette"));

    // Write only if at least one value actually differs. The platform theme
    // rebuilds its palette only when one of the ten changed (paletteChanged_),
    // so an identical rewrite repaints nothing — and it would still churn the
    // file every time anything touched kdeglobals.
    bool dirty = false;
    for (const auto &p : pairs) {
        if (lxqt.value(p.first).toString().compare(p.second, Qt::CaseInsensitive) != 0) {
            dirty = true;
            break;
        }
    }
    if (!dirty) {
        lxqt.endGroup();
        return;
    }

    for (const auto &p : pairs)
        lxqt.setValue(p.first, p.second);
    lxqt.endGroup();
    lxqt.sync();

    emit changed();
}
