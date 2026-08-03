#include "appmenu.h"

#include "desktopentry.h"
#include "dockconfig.h"

#include <QIcon>
#include <QProcess>
#include <QVariantMap>

#include <array>

namespace {
// Icon candidates for one row, best first. The freedesktop "categories" names
// are near-universal but not guaranteed, so each entry carries a fallback or
// two; see pickIcon().
using IconCandidates = std::array<const char *, 3>;

struct CategoryDef
{
    const char *label;
    IconCandidates icons;
    std::array<const char *, 4> tokens; // XDG main-category tokens; nullptr-terminated-ish
};

// Canonical order (KMenu-like). First matching token wins as an app's group.
const std::array<CategoryDef, 11> kCategories = {{
    {"Development", {"applications-development", "applications-engineering", nullptr},
     {"Development", nullptr, nullptr, nullptr}},
    {"Education",   {"applications-education", "applications-science", nullptr},
     {"Education", nullptr, nullptr, nullptr}},
    {"Games",       {"applications-games", "applications-toys", nullptr},
     {"Game", nullptr, nullptr, nullptr}},
    {"Graphics",    {"applications-graphics", "applications-interfacedesign", nullptr},
     {"Graphics", nullptr, nullptr, nullptr}},
    {"Internet",    {"applications-internet", "applications-network", nullptr},
     {"Network", nullptr, nullptr, nullptr}},
    {"Multimedia",  {"applications-multimedia", "applications-audio", nullptr},
     {"AudioVideo", "Audio", "Video", nullptr}},
    {"Office",      {"applications-office", "x-office-document", nullptr},
     {"Office", nullptr, nullptr, nullptr}},
    {"Science",     {"applications-science", "applications-engineering", nullptr},
     {"Science", "Math", "Education", nullptr}},
    {"Settings",    {"preferences-system", "applications-system", "preferences-desktop"},
     {"Settings", nullptr, nullptr, nullptr}},
    {"System",      {"applications-system", "preferences-system", nullptr},
     {"System", nullptr, nullptr, nullptr}},
    {"Utilities",   {"applications-utilities", "applications-accessories", nullptr},
     {"Utility", "Accessories", nullptr, nullptr}},
}};
const char *kOther = "Other";
const IconCandidates kOtherIcons{"applications-other", "applications-utilities", nullptr};
const IconCandidates kFavoritesIcons{"bookmarks", "emblem-favorite", "starred-symbolic"};
const IconCandidates kAllIcons{"applications-all", "applications-other", nullptr};
// A .menu submenu whose .directory file carries no Icon= — most of them do not.
const IconCandidates kSubmenuIcons{"folder", "applications-other", nullptr};

// First candidate the current icon theme actually has. Falls back to the
// primary name rather than to nothing: every row in the sidebar carries an icon
// by design, and a blank one on a theme missing the standard names would put
// the indentation back out of step.
QString pickIcon(const IconCandidates &candidates)
{
    for (const char *name : candidates) {
        if (!name)
            break;
        if (QIcon::hasThemeIcon(QLatin1String(name)))
            return QString::fromLatin1(name);
    }
    return QString::fromLatin1(candidates.front());
}

const IconCandidates &iconsForCategory(const QString &label)
{
    for (const CategoryDef &def : kCategories) {
        if (label == QLatin1String(def.label))
            return def.icons;
    }
    return kOtherIcons;
}

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
        // Most .directory files carry no Icon=; give those a folder rather than
        // a hole, so the submenus line up with the categories above them.
        m[QStringLiteral("icon")] = node.icon.isEmpty() ? pickIcon(kSubmenuIcons) : node.icon;
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

    // Every row carries an icon, including these two: the sidebar reads as one
    // list, and only the .menu submenus having one (their .directory files
    // supply it) made the rest look unfinished. The rows are wider for it, which
    // is why AppMenuPopup's sidebar is sized for "All Applications" plus an icon.
    QVariantList list;
    list.append(row(kFavoritesKey, tr("Favorites"), pickIcon(kFavoritesIcons)));
    list.append(row(kAllKey, tr("All Applications"), pickIcon(kAllIcons)));
    for (const QString &cat : m_presentCategories)
        list.append(row(kCatPrefix + cat, cat, pickIcon(iconsForCategory(cat))));
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
