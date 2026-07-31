#include "appmenu.h"

#include "desktopentry.h"
#include "dockconfig.h"

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

QStringList AppMenu::categories() const
{
    QStringList result{tr("Favorites"), tr("All Applications")};
    result += m_presentCategories;
    return result;
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
    if (category == tr("Favorites"))
        return favorites();

    QVariantList list;
    const bool all = (category == tr("All Applications"));
    for (const DesktopEntry &e : m_apps->all()) {
        if (all || primaryCategory(e.categories) == category)
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
