#include "xdgmenutree.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QXmlStreamReader>

namespace {

QStringList menuDirs()
{
    QStringList dirs;
    for (const QString &base : QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation))
        dirs << base + QStringLiteral("/menus");
    return dirs;
}

// The root menu file, per spec: $XDG_MENU_PREFIX + "applications.menu", searched
// through the config dirs (user first). The unprefixed name is tried as well so
// a session that sets no prefix still finds something.
QString rootMenuFile()
{
    QStringList names;
    const QString prefix = qEnvironmentVariable("XDG_MENU_PREFIX");
    if (!prefix.isEmpty())
        names << prefix + QStringLiteral("applications.menu");
    names << QStringLiteral("applications.menu");

    for (const QString &dir : menuDirs()) {
        for (const QString &name : std::as_const(names)) {
            const QString path = dir + QLatin1Char('/') + name;
            if (QFileInfo::exists(path))
                return path;
        }
    }
    return {};
}

// Name=/Icon= of a .directory file, honouring the locale suffixes. Searched in
// the standard desktop-directories dirs (user first).
void readDirectoryFile(const QString &fileName, QString *label, QString *icon)
{
    QString path;
    for (const QString &base : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        const QString candidate = base + QStringLiteral("/desktop-directories/") + fileName;
        if (QFileInfo::exists(candidate)) {
            path = candidate;
            break;
        }
    }
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    const QString full = QLocale::system().name();               // es_AR
    const QString lang = full.section(QLatin1Char('_'), 0, 0);    // es
    int bestNameRank = -1;                                        // 2 = es_AR, 1 = es, 0 = plain

    QTextStream in(&f);
    bool inEntry = false;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inEntry = (line == QLatin1String("[Desktop Entry]"));
            continue;
        }
        if (!inEntry || line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (value.isEmpty())
            continue;

        if (key == QLatin1String("Icon")) {
            *icon = value;
        } else if (key.startsWith(QLatin1String("Name"))) {
            int rank = -1;
            if (key == QLatin1String("Name"))
                rank = 0;
            else if (key == QLatin1String("Name[") + lang + QLatin1Char(']'))
                rank = 1;
            else if (key == QLatin1String("Name[") + full + QLatin1Char(']'))
                rank = 2;
            if (rank > bestNameRank) {
                bestNameRank = rank;
                *label = value;
            }
        }
    }
}

void appendUnique(QStringList &dst, const QStringList &src)
{
    for (const QString &s : src) {
        if (!dst.contains(s))
            dst.append(s);
    }
}

// Two <Menu> elements with the same <Name> at the same level are one menu: their
// contents are concatenated. This is what makes the browser-generated
// applications-merged/*.menu and the hand-made applications-kmenuedit.menu add
// up instead of shadowing each other.
void mergeInto(QList<XdgMenuNode> &list, const XdgMenuNode &node);

void mergeChildren(XdgMenuNode &dst, const XdgMenuNode &src)
{
    appendUnique(dst.includeFiles, src.includeFiles);
    appendUnique(dst.excludeFiles, src.excludeFiles);
    if (!src.label.isEmpty())
        dst.label = src.label;      // a later <Directory> wins
    if (!src.icon.isEmpty())
        dst.icon = src.icon;
    for (const XdgMenuNode &child : src.children)
        mergeInto(dst.children, child);
}

void mergeInto(QList<XdgMenuNode> &list, const XdgMenuNode &node)
{
    for (XdgMenuNode &existing : list) {
        if (existing.name == node.name) {
            mergeChildren(existing, node);
            return;
        }
    }
    list.append(node);
}

XdgMenuNode parseFile(const QString &path, QSet<QString> &seen);

// A relative <MergeFile> is *not* only looked up next to the file that declares
// it. The system menu ends with <MergeFile>applications-kmenuedit.menu</MergeFile>
// while the editor writes that file into ~/.config/menus/ — resolving it against
// /etc/xdg/menus alone finds nothing and every hand-made submenu disappears.
// User config dirs are searched first, as KDE's own resolver does.
QString resolveMergeFile(const QString &ref, const QString &fileDir)
{
    if (QDir::isAbsolutePath(ref))
        return ref;
    for (const QString &dir : menuDirs()) {
        const QString candidate = dir + QLatin1Char('/') + ref;
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return fileDir + QLatin1Char('/') + ref;
}

// Merge every *.menu of a directory, in name order so the result is stable.
void mergeDir(XdgMenuNode &into, const QString &dirPath, QSet<QString> &seen)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return;
    const QStringList files = dir.entryList({QStringLiteral("*.menu")}, QDir::Files, QDir::Name);
    for (const QString &name : files)
        mergeChildren(into, parseFile(dir.filePath(name), seen));
}

XdgMenuNode parseMenu(QXmlStreamReader &xml, const QString &fileDir, QSet<QString> &seen)
{
    XdgMenuNode node;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("Menu"))
            break;
        if (token != QXmlStreamReader::StartElement)
            continue;

        const QStringView tag = xml.name();
        if (tag == QLatin1String("Menu")) {
            mergeInto(node.children, parseMenu(xml, fileDir, seen));
        } else if (tag == QLatin1String("Name")) {
            node.name = xml.readElementText().trimmed();
        } else if (tag == QLatin1String("Directory")) {
            readDirectoryFile(xml.readElementText().trimmed(), &node.label, &node.icon);
        } else if (tag == QLatin1String("Include") || tag == QLatin1String("Exclude")) {
            const bool include = (tag == QLatin1String("Include"));
            const QString closing = xml.name().toString();
            while (!xml.atEnd()) {
                const auto t = xml.readNext();
                if (t == QXmlStreamReader::EndElement && xml.name() == closing)
                    break;
                if (t != QXmlStreamReader::StartElement)
                    continue;
                if (xml.name() == QLatin1String("Filename")) {
                    // <Filename> holds a desktop-file id; strip the extension so
                    // it matches DesktopEntryIndex's ids.
                    QString id = xml.readElementText().trimmed();
                    if (id.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive))
                        id.chop(8);
                    id = id.toLower();
                    if (!id.isEmpty())
                        appendUnique(include ? node.includeFiles : node.excludeFiles, {id});
                } else {
                    // <Category>, <And>, <Or>, <Not>, <All/>: category matching is
                    // AppMenu's job, not this parser's.
                    xml.skipCurrentElement();
                }
            }
        } else if (tag == QLatin1String("MergeFile")) {
            const QString ref = xml.readElementText().trimmed();
            if (!ref.isEmpty())
                mergeChildren(node, parseFile(resolveMergeFile(ref, fileDir), seen));
        } else if (tag == QLatin1String("MergeDir")) {
            const QString ref = xml.readElementText().trimmed();
            if (!ref.isEmpty()) {
                mergeDir(node, QDir::isAbsolutePath(ref) ? ref : fileDir + QLatin1Char('/') + ref,
                         seen);
            }
        } else if (tag == QLatin1String("DefaultMergeDirs")) {
            // Spec: menus/<basename-of-this-file>-merged/. In practice
            // xdg-desktop-menu writes into menus/applications-merged/ regardless
            // of the session's XDG_MENU_PREFIX, so both are merged — without the
            // second one the browsers' web-app submenu is invisible.
            QStringList names{QStringLiteral("applications-merged")};
            const QString prefix = qEnvironmentVariable("XDG_MENU_PREFIX");
            if (!prefix.isEmpty())
                names.prepend(prefix + QStringLiteral("applications-merged"));
            for (const QString &dir : menuDirs()) {
                for (const QString &name : std::as_const(names))
                    mergeDir(node, dir + QLatin1Char('/') + name, seen);
            }
            xml.skipCurrentElement();
        }
    }
    return node;
}

XdgMenuNode parseFile(const QString &path, QSet<QString> &seen)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty() || seen.contains(canonical))
        return {};               // missing, or a merge cycle
    seen.insert(canonical);

    QFile f(canonical);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    const QString dir = QFileInfo(canonical).absolutePath();
    QXmlStreamReader xml(&f);
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("Menu"))
            return parseMenu(xml, dir, seen);
    }
    return {};
}

// Keep only what this parser is for: menus that name their members by filename.
// A menu with none of its own is still kept when a descendant qualifies, so it
// can act as the container its children are nested in.
bool prune(XdgMenuNode &node)
{
    QList<XdgMenuNode> kept;
    for (XdgMenuNode &child : node.children) {
        if (prune(child))
            kept.append(child);
    }
    node.children = kept;

    if (node.name.startsWith(QLatin1Char('.')))
        return false;            // kmenuedit's ".hidden" bucket
    return !node.includeFiles.isEmpty() || !node.children.isEmpty();
}

// Collapse a chain of empty single-child menus into the child. Installers do
// write these: NoMachine's .menu nests <Menu>Internet</Menu> five deep before
// getting to its actual entry, which would otherwise be five useless sidebar
// rows leading to one real submenu.
void collapse(XdgMenuNode &node)
{
    for (XdgMenuNode &child : node.children)
        collapse(child);
    for (XdgMenuNode &child : node.children) {
        while (child.includeFiles.isEmpty() && child.children.size() == 1)
            child = child.children.first();
    }
}

// A menu without a <Directory> (or whose .directory is missing) still needs
// something to show in the sidebar.
void labelFallback(XdgMenuNode &node)
{
    if (node.label.isEmpty())
        node.label = node.name;
    for (XdgMenuNode &child : node.children)
        labelFallback(child);
}

} // namespace

QList<XdgMenuNode> loadXdgMenuTree()
{
    const QString root = rootMenuFile();
    if (root.isEmpty())
        return {};

    QSet<QString> seen;
    XdgMenuNode tree = parseFile(root, seen);
    prune(tree);
    collapse(tree);
    labelFallback(tree);
    return tree.children;
}
