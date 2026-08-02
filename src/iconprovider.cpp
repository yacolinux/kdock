#include "iconprovider.h"

#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace {

// Every directory an icon theme can live in. Same criterion as
// Theme::availableIconThemes(): Qt's own list plus the XDG data dirs, because
// the two do not always agree on ~/.local/share/icons.
QStringList iconSearchPaths()
{
    static QStringList paths;
    if (!paths.isEmpty())
        return paths;
    paths = QIcon::themeSearchPaths();
    for (const QString &dir : QStandardPaths::locateAll(
             QStandardPaths::GenericDataLocation, QStringLiteral("icons"),
             QStandardPaths::LocateDirectory)) {
        if (!paths.contains(dir))
            paths << dir;
    }
    paths.removeDuplicates();
    return paths;
}

// One entry of a theme's Directories= list, with what the spec needs to pick
// the best size: Type (Fixed/Scalable/Threshold), Size, MinSize/MaxSize.
struct ThemeDir {
    QString path; // absolute, no trailing slash
    int size = 0;
    int minSize = 0;
    int maxSize = 0;
    int threshold = 2;
    bool scalable = false;
};

struct ThemeInfo {
    QList<ThemeDir> dirs;
    QStringList inherits;
    bool valid = false;
};

// Parse <base>/<theme>/index.theme for every base that has one: a theme can be
// split across search paths (a user copy shadowing a system one).
const ThemeInfo &themeInfo(const QString &themeId)
{
    static QHash<QString, ThemeInfo> cache;
    auto it = cache.constFind(themeId);
    if (it != cache.constEnd())
        return it.value();

    ThemeInfo info;
    for (const QString &base : iconSearchPaths()) {
        const QString root = base + QLatin1Char('/') + themeId;
        const QString index = root + QStringLiteral("/index.theme");
        if (!QFileInfo::exists(index))
            continue;
        info.valid = true;
        QSettings ini(index, QSettings::IniFormat);
        ini.beginGroup(QStringLiteral("Icon Theme"));
        const QStringList dirs = ini.value(QStringLiteral("Directories")).toStringList()
                                 + ini.value(QStringLiteral("ScaledDirectories")).toStringList();
        const QStringList parents = ini.value(QStringLiteral("Inherits")).toStringList();
        ini.endGroup();

        for (const QString &parent : parents) {
            const QString id = parent.trimmed();
            if (!id.isEmpty() && !info.inherits.contains(id))
                info.inherits << id;
        }
        for (const QString &sub : dirs) {
            const QString name = sub.trimmed();
            if (name.isEmpty())
                continue;
            ini.beginGroup(name);
            ThemeDir d;
            d.path = root + QLatin1Char('/') + name;
            d.size = ini.value(QStringLiteral("Size")).toInt();
            d.threshold = ini.value(QStringLiteral("Threshold"), 2).toInt();
            const QString type =
                ini.value(QStringLiteral("Type"), QStringLiteral("Threshold")).toString();
            d.scalable = type.compare(QLatin1String("Scalable"), Qt::CaseInsensitive) == 0;
            d.minSize = ini.value(QStringLiteral("MinSize"), d.size).toInt();
            d.maxSize = ini.value(QStringLiteral("MaxSize"), d.size).toInt();
            ini.endGroup();
            if (d.size > 0)
                info.dirs.append(d);
        }
    }
    return *cache.insert(themeId, info);
}

// Order for the directory scan: 0 when the directory covers the wanted size
// (freedesktop's "directory matches size"), else how far off it is. A scalable
// directory loses to an exact bitmap but beats a wrong one.
int sizeDistance(const ThemeDir &d, int wanted)
{
    if (qAbs(d.size - wanted) <= d.threshold)
        return 0;
    if (d.scalable && wanted >= d.minSize && wanted <= d.maxSize)
        return 1;
    return 2 + qAbs(d.size - wanted);
}

QString fileInDir(const QString &dir, const QString &name)
{
    for (const char *ext : {".png", ".svg", ".svgz", ".xpm"}) {
        const QString path = dir + QLatin1Char('/') + name + QLatin1String(ext);
        if (QFileInfo::exists(path))
            return path;
    }
    return QString();
}

} // namespace

QString IconProvider::resolveInTheme(const QString &themeId, const QString &name, int size)
{
    // Breadth-first over the Inherits chain (Gruvbox-Plus-Light inherits
    // Gruvbox-Plus-Dark inherits FlatWoken…) with a visited set: the chains in
    // the wild do contain cycles.
    QStringList queue{themeId};
    QSet<QString> seen{themeId};
    while (!queue.isEmpty()) {
        const ThemeInfo &info = themeInfo(queue.takeFirst());
        if (!info.valid)
            continue;

        QList<const ThemeDir *> dirs;
        dirs.reserve(info.dirs.size());
        for (const ThemeDir &d : info.dirs)
            dirs.append(&d);
        std::stable_sort(dirs.begin(), dirs.end(), [size](const ThemeDir *a, const ThemeDir *b) {
            return sizeDistance(*a, size) < sizeDistance(*b, size);
        });
        for (const ThemeDir *d : std::as_const(dirs)) {
            const QString path = fileInDir(d->path, name);
            if (!path.isEmpty())
                return path;
        }

        for (const QString &parent : info.inherits) {
            if (!seen.contains(parent)) {
                seen.insert(parent);
                queue.append(parent);
            }
        }
    }
    return QString();
}

QPixmap IconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    // "name@rev[@theme]": the revision busts the cache on theme changes, the
    // optional theme resolves this icon against another icon set (see header).
    const QString name = id.section(QLatin1Char('@'), 0, 0);
    const int revision = id.section(QLatin1Char('@'), 1, 1).toInt();
    const QString themeOverride = id.section(QLatin1Char('@'), 2, 2);

    const QSize wanted = requestedSize.isValid() ? requestedSize : QSize(64, 64);

    if (!themeOverride.isEmpty()) {
        if (revision != m_revision) {
            m_paths.clear();
            m_revision = revision;
        }
        const int px = qMax(wanted.width(), wanted.height());
        const QString key =
            themeOverride + QLatin1Char('|') + name + QLatin1Char('|') + QString::number(px);
        auto it = m_paths.constFind(key);
        if (it == m_paths.constEnd())
            it = m_paths.insert(key, resolveInTheme(themeOverride, name, px));
        // An empty path means no theme in the override's chain carries the
        // name (custom app icons, an uninstalled theme): fall through to the
        // dock's own icon set below instead of drawing a missing-icon
        // placeholder — the rule the old QIcon::hasThemeIcon() guard had.
        if (!it.value().isEmpty()) {
            const QPixmap pm = QIcon(it.value()).pixmap(wanted);
            if (!pm.isNull()) {
                if (size)
                    *size = pm.size();
                return pm;
            }
        }
    }

    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull() && name.contains(QLatin1Char('/')))
        icon = QIcon(name); // absolute path in the Icon= field
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));

    QPixmap pm = icon.pixmap(wanted);
    if (size)
        *size = pm.size();
    return pm;
}
