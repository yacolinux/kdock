#include "theme.h"

#include "dockconfig.h"

#include <QDir>
#include <QGuiApplication>
#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QPalette>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

static QColor parseKdeColor(const QVariant &value, const QColor &fallback)
{
    // kdeglobals stores colors as "r,g,b" (QSettings may hand that back
    // as a QStringList because of the commas)
    QStringList parts;
    if (value.userType() == QMetaType::QStringList)
        parts = value.toStringList();
    else if (value.isValid())
        parts = value.toString().split(QLatin1Char(','));
    if (parts.size() < 3)
        return fallback;
    return QColor(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(),
                  parts[2].trimmed().toInt(),
                  parts.size() > 3 ? parts[3].trimmed().toInt() : 255);
}

Theme::Theme(QObject *parent)
    : QObject(parent)
{
    QSettings shared(DockConfig::settingsFilePath(), QSettings::IniFormat);
    m_iconThemeOverride = shared.value(QStringLiteral("iconTheme")).toString();

    reload();

    const QString path =
        QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kdeglobals"));
    if (!path.isEmpty()) {
        m_watcher.addPath(path);
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this, path] {
            // Editors replace the file; re-add the path and debounce a bit.
            QTimer::singleShot(200, this, [this, path] {
                if (!m_watcher.files().contains(path))
                    m_watcher.addPath(path);
                reload();
            });
        });
    }
}

void Theme::reload()
{
    const QPalette pal = qGuiApp->palette();
    m_background      = pal.color(QPalette::Window);
    m_foreground      = pal.color(QPalette::WindowText);
    m_highlight       = pal.color(QPalette::Highlight);
    m_view            = pal.color(QPalette::Base);
    m_viewText        = pal.color(QPalette::Text);
    m_button          = pal.color(QPalette::Button);
    m_buttonText      = pal.color(QPalette::ButtonText);
    m_highlightedText = pal.color(QPalette::HighlightedText);

    const QString path =
        QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kdeglobals"));
    if (!path.isEmpty()) {
        QSettings kde(path, QSettings::IniFormat);

        m_background      = parseKdeColor(kde.value("Colors:Window/BackgroundNormal"), m_background);
        m_foreground      = parseKdeColor(kde.value("Colors:Window/ForegroundNormal"), m_foreground);
        m_highlight       = parseKdeColor(kde.value("Colors:Selection/BackgroundNormal"), m_highlight);
        m_view            = parseKdeColor(kde.value("Colors:View/BackgroundNormal"), m_view);
        m_viewText        = parseKdeColor(kde.value("Colors:View/ForegroundNormal"), m_viewText);
        m_button          = parseKdeColor(kde.value("Colors:Button/BackgroundNormal"), m_button);
        m_buttonText      = parseKdeColor(kde.value("Colors:Button/ForegroundNormal"), m_buttonText);
        m_highlightedText = parseKdeColor(kde.value("Colors:Selection/ForegroundNormal"), m_highlightedText);

        m_kdeIconTheme = kde.value("Icons/Theme").toString();
    }

    // The user's override (Settings → General) wins over the KDE theme.
    const QString effectiveIcons =
        !m_iconThemeOverride.isEmpty() ? m_iconThemeOverride : m_kdeIconTheme;
    if (!effectiveIcons.isEmpty())
        QIcon::setThemeName(effectiveIcons);

    if (QIcon::fallbackThemeName().isEmpty())
        QIcon::setFallbackThemeName(QStringLiteral("breeze"));

    applyAppPalette();

    ++m_revision;
    emit changed();
}

void Theme::applyAppPalette()
{
    // Propagate the KDE colors to widgets (settings dialog) and
    // Qt Quick Controls (context menus) so the whole app matches.
    QPalette pal = qGuiApp->palette();
    pal.setColor(QPalette::Window, m_background);
    pal.setColor(QPalette::WindowText, m_foreground);
    pal.setColor(QPalette::Base, m_view);
    pal.setColor(QPalette::Text, m_viewText);
    pal.setColor(QPalette::Button, m_button);
    pal.setColor(QPalette::ButtonText, m_buttonText);
    pal.setColor(QPalette::Highlight, m_highlight);
    pal.setColor(QPalette::HighlightedText, m_highlightedText);
    pal.setColor(QPalette::ToolTipBase, m_background);
    pal.setColor(QPalette::ToolTipText, m_foreground);
    QApplication::setPalette(pal);
}

void Theme::setIconTheme(const QString &id)
{
    if (m_iconThemeOverride == id)
        return;
    m_iconThemeOverride = id;
    QSettings shared(DockConfig::settingsFilePath(), QSettings::IniFormat);
    shared.setValue(QStringLiteral("iconTheme"), id);
    reload(); // re-applies QIcon::setThemeName + bumps revision + emits changed()
}

QList<QPair<QString, QString>> Theme::availableIconThemes()
{
    QStringList searchPaths = QIcon::themeSearchPaths();
    for (const QString &dir : QStandardPaths::locateAll(
             QStandardPaths::GenericDataLocation, QStringLiteral("icons"),
             QStandardPaths::LocateDirectory)) {
        if (!searchPaths.contains(dir))
            searchPaths << dir;
    }
    searchPaths.removeDuplicates();

    QList<QPair<QString, QString>> themes;
    QSet<QString> seenIds;
    for (const QString &base : searchPaths) {
        const auto dirs = QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &id : dirs) {
            if (id == QLatin1String("hicolor") || seenIds.contains(id))
                continue;
            const QString index = base + QLatin1Char('/') + id + QStringLiteral("/index.theme");
            if (!QFileInfo::exists(index))
                continue;
            QSettings ini(index, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("Icon Theme"));
            // Real icon themes list their size subdirs; cursor themes don't.
            const bool isIconTheme = ini.contains(QStringLiteral("Directories"));
            const bool hidden = ini.value(QStringLiteral("Hidden"), false).toBool();
            const QString name = ini.value(QStringLiteral("Name"), id).toString();
            ini.endGroup();
            if (!isIconTheme || hidden)
                continue;
            seenIds.insert(id);
            themes.append({name, id});
        }
    }
    std::sort(themes.begin(), themes.end(),
              [](const auto &a, const auto &b) { return a.first.compare(b.first, Qt::CaseInsensitive) < 0; });
    return themes;
}
