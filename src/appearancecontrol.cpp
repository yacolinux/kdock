#include "appearancecontrol.h"

#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace {

QString kdeglobalsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/kdeglobals");
}

// Every "<data dir>/color-schemes" that exists, in lookup order.
QStringList colorSchemeDirs()
{
    QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                                 QStringLiteral("color-schemes"),
                                                 QStandardPaths::LocateDirectory);
    dirs.removeDuplicates();
    return dirs;
}

// "r,g,b" (the format kdeglobals and the .colors files use) as "#rrggbb".
// QSettings hands values with commas back as a QStringList.
QString kdeColorToHex(const QVariant &value, const QString &fallback)
{
    QStringList parts;
    if (value.userType() == QMetaType::QStringList)
        parts = value.toStringList();
    else if (value.isValid())
        parts = value.toString().split(QLatin1Char(','));
    if (parts.size() < 3)
        return fallback;
    return QColor(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(),
                  parts[2].trimmed().toInt())
        .name();
}

} // namespace

AppearanceControl::AppearanceControl(Theme *theme, QObject *parent)
    : QObject(parent)
{
    readCurrent();
    // Theme already watches kdeglobals, so its signal is the cheapest way to
    // notice that the icon theme or the color scheme changed - including when
    // something other than this widget changed them.
    if (theme)
        connect(theme, &Theme::changed, this, [this] {
            const QString icons = m_currentIconTheme;
            const QString colors = m_currentColorScheme;
            readCurrent();
            if (icons != m_currentIconTheme || colors != m_currentColorScheme)
                emit changed();
        });
}

void AppearanceControl::readCurrent()
{
    QSettings kde(kdeglobalsPath(), QSettings::IniFormat);
    m_currentIconTheme = kde.value(QStringLiteral("Icons/Theme")).toString();
    // Absent on setups whose scheme was applied through a look-and-feel
    // package; then no row is marked as the current one.
    m_currentColorScheme = kde.value(QStringLiteral("General/ColorScheme")).toString();
}

QVariantList AppearanceControl::iconThemes()
{
    QVariantList out;
    const auto themes = Theme::availableIconThemes(); // (displayName, id)
    out.reserve(themes.size());
    for (const auto &t : themes) {
        // Some shipped index.theme files keep the packaging placeholder
        // ("@ThemeName@") in Name=; the directory name is the honest fallback.
        const QString name =
            (t.first.isEmpty() || t.first.contains(QLatin1Char('@'))) ? t.second : t.first;
        out.append(QVariantMap{{QStringLiteral("id"), t.second},
                               {QStringLiteral("name"), name},
                               {QStringLiteral("current"), t.second == m_currentIconTheme}});
    }
    return out;
}

QVariantList AppearanceControl::colorSchemes()
{
    if (!m_scanned)
        scanColorSchemes();
    // The "current" flag is the only part that goes stale between scans.
    for (QVariant &entry : m_colorSchemes) {
        QVariantMap map = entry.toMap();
        map[QStringLiteral("current")] =
            map.value(QStringLiteral("id")).toString() == m_currentColorScheme;
        entry = map;
    }
    return m_colorSchemes;
}

void AppearanceControl::scanColorSchemes()
{
    m_scanned = true;
    m_colorSchemes.clear();
    m_colorSchemesStamp = QDateTime();

    QSet<QString> seen;
    for (const QString &dir : colorSchemeDirs()) {
        const QFileInfo dirInfo(dir);
        if (dirInfo.lastModified() > m_colorSchemesStamp)
            m_colorSchemesStamp = dirInfo.lastModified();

        const auto files = QDir(dir).entryInfoList({QStringLiteral("*.colors")}, QDir::Files);
        for (const QFileInfo &file : files) {
            // The scheme is applied by name, which is the file's base name.
            const QString id = file.completeBaseName();
            if (seen.contains(id))
                continue; // a user scheme shadows the system one
            seen.insert(id);

            QSettings ini(file.absoluteFilePath(), QSettings::IniFormat);
            const QString name = ini.value(QStringLiteral("General/Name"), id).toString();
            // Enough of the scheme to preview it in one row: the window
            // background, the text over it, and the selection color.
            const QString bg =
                kdeColorToHex(ini.value(QStringLiteral("Colors:Window/BackgroundNormal")),
                              QStringLiteral("#000000"));
            const QString fg =
                kdeColorToHex(ini.value(QStringLiteral("Colors:Window/ForegroundNormal")),
                              QStringLiteral("#ffffff"));
            const QString sel =
                kdeColorToHex(ini.value(QStringLiteral("Colors:Selection/BackgroundNormal")),
                              QStringLiteral("#3daee9"));

            m_colorSchemes.append(QVariantMap{{QStringLiteral("id"), id},
                                              {QStringLiteral("name"), name},
                                              {QStringLiteral("bg"), bg},
                                              {QStringLiteral("fg"), fg},
                                              {QStringLiteral("sel"), sel},
                                              {QStringLiteral("current"), id == m_currentColorScheme}});
        }
    }

    std::sort(m_colorSchemes.begin(), m_colorSchemes.end(),
              [](const QVariant &a, const QVariant &b) {
                  return a.toMap().value(QStringLiteral("name")).toString().compare(
                             b.toMap().value(QStringLiteral("name")).toString(),
                             Qt::CaseInsensitive)
                         < 0;
              });
}

void AppearanceControl::refreshIfStale()
{
    if (!m_scanned) {
        scanColorSchemes();
        return;
    }
    QDateTime newest;
    for (const QString &dir : colorSchemeDirs()) {
        const QDateTime stamp = QFileInfo(dir).lastModified();
        if (stamp > newest)
            newest = stamp;
    }
    if (newest > m_colorSchemesStamp)
        scanColorSchemes();
}

QString AppearanceControl::findTool(const QString &name, const QStringList &extraDirs)
{
    const QString onPath = QStandardPaths::findExecutable(name);
    if (!onPath.isEmpty())
        return onPath;
    const QString fromDirs = QStandardPaths::findExecutable(name, extraDirs);
    return fromDirs;
}

void AppearanceControl::applyIconTheme(const QString &id)
{
    if (id.isEmpty())
        return;
    // plasma-changeicons writes kdeglobals *and* notifies the running apps, but
    // it is not on PATH: it is a libexec helper whose directory varies by
    // distribution (Debian multiarch, /opt/kde, plain /usr/libexec).
    static const QStringList libexecDirs = {
        QStringLiteral("/usr/lib/x86_64-linux-gnu/libexec"),
        QStringLiteral("/opt/kde/lib/x86_64-linux-gnu/libexec"),
        QStringLiteral("/usr/libexec"),
        QStringLiteral("/usr/lib/libexec"),
        QStringLiteral("/usr/lib/kf6/libexec"),
        QStringLiteral("/opt/kde/libexec"),
    };
    const QString tool = findTool(QStringLiteral("plasma-changeicons"), libexecDirs);
    if (!tool.isEmpty()) {
        QProcess::startDetached(tool, {id});
        return;
    }
    // Fallback: set it ourselves. The dock follows (Theme watches kdeglobals),
    // but applications already running keep their icons until restarted.
    const QString kwrite = QStandardPaths::findExecutable(
        QStringLiteral("kwriteconfig6"), {QStringLiteral("/opt/kde/bin"), QStringLiteral("/usr/bin")});
    if (!kwrite.isEmpty()) {
        QProcess::startDetached(kwrite, {QStringLiteral("--file"), QStringLiteral("kdeglobals"),
                                         QStringLiteral("--group"), QStringLiteral("Icons"),
                                         QStringLiteral("--key"), QStringLiteral("Theme"), id});
    } else {
        QSettings kde(kdeglobalsPath(), QSettings::IniFormat);
        kde.setValue(QStringLiteral("Icons/Theme"), id);
    }
}

void AppearanceControl::applyColorScheme(const QString &id)
{
    if (id.isEmpty())
        return;
    const QString tool = findTool(QStringLiteral("plasma-apply-colorscheme"),
                                  {QStringLiteral("/opt/kde/bin")});
    if (!tool.isEmpty())
        QProcess::startDetached(tool, {id});
}
