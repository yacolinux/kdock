#include "appmenu.h"

#include "desktopentry.h"
#include "dockconfig.h"

#include <QProcess>
#include <QVariantMap>

#include <array>

namespace {
struct CategoryDef
{
    const char *label;
    std::array<const char *, 4> tokens; // XDG main-category tokens; nullptr-terminated-ish
};

// Canonical order (KMenu-like). First matching token wins as an app's group.
const std::array<CategoryDef, 11> kCategories = {{
    {"Development", {"Development", nullptr, nullptr, nullptr}},
    {"Education",   {"Education", nullptr, nullptr, nullptr}},
    {"Games",       {"Game", nullptr, nullptr, nullptr}},
    {"Graphics",    {"Graphics", nullptr, nullptr, nullptr}},
    {"Internet",    {"Network", nullptr, nullptr, nullptr}},
    {"Multimedia",  {"AudioVideo", "Audio", "Video", nullptr}},
    {"Office",      {"Office", nullptr, nullptr, nullptr}},
    {"Science",     {"Science", "Math", "Education", nullptr}},
    {"Settings",    {"Settings", nullptr, nullptr, nullptr}},
    {"System",      {"System", nullptr, nullptr, nullptr}},
    {"Utilities",   {"Utility", "Accessories", nullptr, nullptr}},
}};
const char *kOther = "Other";

// Section-key prefixes. Keys are not translated (unlike the labels shown), so
// the current selection survives a language change.
const QLatin1String kCatPrefix("cat:");
const QLatin1String kMenuPrefix("menu:");
const QLatin1String kFavoritesKey("cat:__favorites__");
const QLatin1String kAllKey("cat:__all__");
} // namespace

AppMenu::AppMenu(DesktopEntryIndex *apps, DockConfig *config, QObject *parent)
    : QObject(parent)
    , m_apps(apps)
    , m_config(config)
{
    rebuild();
    connect(m_config, &DockConfig::menuFavoritesChanged, this, &AppMenu::favoritesChanged);
}

QString AppMenu::primaryCategory(const QStringList &cats) const
{
    for (const CategoryDef &def : kCategories) {
        for (const char *tok : def.tokens) {
            if (!tok)
                break;
            if (cats.contains(QLatin1String(tok)))
                return QString::fromLatin1(def.label);
        }
    }
    return QString::fromLatin1(kOther);
}

void AppMenu::rebuild()
{
    m_tree = loadXdgMenuTree();

    QStringList present;
    bool hasOther = false;
    for (const DesktopEntry &e : m_apps->all()) {
        const QString cat = primaryCategory(e.categories);
        if (cat == QLatin1String(kOther))
            hasOther = true;
        else if (!present.contains(cat))
            present.append(cat);
    }
    // Reorder to canonical order.
    m_presentCategories.clear();
    for (const CategoryDef &def : kCategories) {
        const QString label = QString::fromLatin1(def.label);
        if (present.contains(label))
            m_presentCategories.append(label);
    }
    if (hasOther)
        m_presentCategories.append(QString::fromLatin1(kOther));
    emit changed();
}

void AppMenu::appendMenuSections(QVariantList &out, const QList<XdgMenuNode> &nodes,
                                const QString &parentKey, int depth) const
{
    for (const XdgMenuNode &node : nodes) {
        const QString path = parentKey.isEmpty() ? node.name
                                                 : parentKey + QLatin1Char('/') + node.name;
        QVariantMap m;
        m[QStringLiteral("key")] = kMenuPrefix + path;
        m[QStringLiteral("label")] = node.label;
        m[QStringLiteral("icon")] = node.icon;
        m[QStringLiteral("depth")] = depth;
        out.append(m);
        appendMenuSections(out, node.children, path, depth + 1);
    }
}

QVariantList AppMenu::sections() const
{
    const auto row = [](const QString &key, const QString &label, const QString &icon) {
        QVariantMap m;
        m[QStringLiteral("key")] = key;
        m[QStringLiteral("label")] = label;
        m[QStringLiteral("icon")] = icon;
        m[QStringLiteral("depth")] = 0;
        return QVariant(m);
    };

    // Favorites and All Applications stay text-only: they are not menus, and
    // giving them an icon narrows the row enough to elide "All Applications".
    QVariantList list;
    list.append(row(kFavoritesKey, tr("Favorites"), QString()));
    list.append(row(kAllKey, tr("All Applications"), QString()));
    for (const QString &cat : m_presentCategories)
        list.append(row(kCatPrefix + cat, cat, QString()));
    appendMenuSections(list, m_tree, QString(), 0);
    return list;
}

const XdgMenuNode *AppMenu::nodeForPath(const QString &path) const
{
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QList<XdgMenuNode> *level = &m_tree;
    const XdgMenuNode *found = nullptr;
    for (const QString &part : parts) {
        found = nullptr;
        for (const XdgMenuNode &node : *level) {
            if (node.name == part) {
                found = &node;
                break;
            }
        }
        if (!found)
            return nullptr;
        level = &found->children;
    }
    return found;
}

QVariantMap AppMenu::entryToMap(const QString &id) const
{
    const DesktopEntry e = m_apps->byId(id);
    QVariantMap m;
    m[QStringLiteral("id")] = e.isValid() ? e.id : id;
    m[QStringLiteral("name")] = e.isValid() && !e.name.isEmpty() ? e.name : id;
    m[QStringLiteral("icon")] = e.isValid() && !e.icon.isEmpty()
                                    ? e.icon : QStringLiteral("application-x-executable");
    m[QStringLiteral("comment")] = e.comment;
    m[QStringLiteral("favorite")] = isFavorite(id);
    return m;
}

QVariantList AppMenu::appsInCategory(const QString &category) const
{
    if (category == kFavoritesKey)
        return favorites();

    // A submenu from the .menu files: its members are listed by filename, in the
    // order the menu declares them (that order is the user's, so it is kept).
    if (category.startsWith(kMenuPrefix)) {
        QVariantList list;
        const XdgMenuNode *node = nodeForPath(category.mid(kMenuPrefix.size()));
        if (!node)
            return list;
        for (const QString &id : node->includeFiles) {
            if (node->excludeFiles.contains(id))
                continue;
            const DesktopEntry e = m_apps->byId(id);
            if (e.isValid() && !e.noDisplay)
                list.append(entryToMap(e.id));
        }
        return list;
    }

    QVariantList list;
    const bool all = (category == kAllKey);
    const QString label = category.startsWith(kCatPrefix) ? category.mid(kCatPrefix.size())
                                                          : category;
    for (const DesktopEntry &e : m_apps->all()) {
        if (all || primaryCategory(e.categories) == label)
            list.append(entryToMap(e.id));
    }
    return list;
}

QVariantList AppMenu::search(const QString &query) const
{
    const QString q = query.trimmed();
    if (q.isEmpty())
        return {};
    QVariantList list;
    for (const DesktopEntry &e : m_apps->all()) {
        if (e.name.contains(q, Qt::CaseInsensitive)
            || e.comment.contains(q, Qt::CaseInsensitive)
            || e.id.contains(q, Qt::CaseInsensitive))
            list.append(entryToMap(e.id));
    }
    return list;
}

QVariantList AppMenu::favorites() const
{
    QVariantList list;
    for (const QString &id : m_config->menuFavorites()) {
        const DesktopEntry e = m_apps->byId(id);
        if (e.isValid())
            list.append(entryToMap(id));
    }
    return list;
}

void AppMenu::launch(const QString &id) const
{
    const DesktopEntry e = m_apps->byId(id);
    if (e.isValid())
        DesktopEntryIndex::launch(e);
}

void AppMenu::launchMenuEditor() const
{
    const QString app = m_config->menuEditorApp().trimmed();
    if (app.isEmpty())
        return;
    // A .desktop id first (that is what the settings picker stores), through
    // forAppId() so a bare name like "kmenuedit" also resolves to
    // org.kde.kmenuedit. Anything else is run as a plain command, which keeps a
    // hand-written value with arguments working on a machine without that
    // .desktop file.
    const DesktopEntry entry = m_apps->forAppId(app);
    if (entry.isValid()) {
        DesktopEntryIndex::launch(entry);
        return;
    }
    QStringList parts = QProcess::splitCommand(app);
    if (parts.isEmpty())
        return;
    const QString program = parts.takeFirst();
    QProcess::startDetached(program, parts);
}

bool AppMenu::isFavorite(const QString &id) const
{
    return m_config->menuFavorites().contains(id);
}

void AppMenu::addFavorite(const QString &id)
{
    if (id.isEmpty())
        return;
    QStringList favs = m_config->menuFavorites();
    if (favs.contains(id))
        return;
    favs.append(id);
    m_config->setMenuFavorites(favs);
}

void AppMenu::removeFavorite(const QString &id)
{
    QStringList favs = m_config->menuFavorites();
    if (favs.removeAll(id) > 0)
        m_config->setMenuFavorites(favs);
}

void AppMenu::toggleFavorite(const QString &id)
{
    if (isFavorite(id))
        removeFavorite(id);
    else
        addFavorite(id);
}

void AppMenu::moveFavorite(int from, int to)
{
    QStringList favs = m_config->menuFavorites();
    if (from < 0 || to < 0 || from >= favs.size() || to >= favs.size() || from == to)
        return;
    favs.move(from, to);
    m_config->setMenuFavorites(favs);
}
