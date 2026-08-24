// XDG application menu backend (KMenu/Kickoff-like), exposed to QML as
// "appMenu". Groups installed .desktop apps by freedesktop main category,
// supports search, and editable favorites (stored in DockConfig).
//
// On top of the category grouping it also shows the submenus declared in the
// XDG .menu files (see xdgmenutree.h): the ones KDE's menu editor writes and the
// ones browsers create for their web apps. Those list their members by filename
// and their .desktop files often carry no Categories at all, so the category
// path alone cannot see them.

#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

#include "xdgmenutree.h"

class DesktopEntryIndex;
class DockConfig;

class AppMenu : public QObject
{
    Q_OBJECT
public:
    AppMenu(DesktopEntryIndex *apps, DockConfig *config, QObject *parent = nullptr);

    // Sidebar rows: { key, label, icon, depth }. "Favorites" and "All
    // Applications" first, then the present categories, then the .menu
    // submenus (nested ones carry depth > 0). The key is what to hand back to
    // appsInCategory(); it is prefixed ("cat:" / "menu:") so a submenu called
    // e.g. "Games" can never collide with the category of the same name.
    Q_INVOKABLE QVariantList sections() const;

    // Each entry is a map { id, name, icon, comment, favorite }.
    Q_INVOKABLE QVariantList appsInCategory(const QString &category) const;
    Q_INVOKABLE QVariantList search(const QString &query) const;
    // The same map for a single id, or an empty map if the index no longer knows
    // it. Lets a caller (the tile menu's recent-apps strip) turn a stored id back
    // into a name and icon without reaching into DesktopEntryIndex itself.
    Q_INVOKABLE QVariantMap appById(const QString &id) const;
    Q_INVOKABLE QVariantList favorites() const;

    Q_INVOKABLE void launch(const QString &id) const;
    // Opens the app that edits the XDG menus (config.menuEditorApp, KDE's menu
    // editor by default) — the tool that creates the submenus this menu shows.
    Q_INVOKABLE void launchMenuEditor() const;
    Q_INVOKABLE bool isFavorite(const QString &id) const;
    Q_INVOKABLE void addFavorite(const QString &id);
    Q_INVOKABLE void removeFavorite(const QString &id);
    Q_INVOKABLE void toggleFavorite(const QString &id);
    Q_INVOKABLE void moveFavorite(int from, int to);

signals:
    void changed();
    void favoritesChanged();

private:
    QVariantMap entryToMap(const QString &id) const;
    QString primaryCategory(const QStringList &cats) const;
    void rebuild();
    // Depth-first walk of m_tree appending one section row per node.
    void appendMenuSections(QVariantList &out, const QList<XdgMenuNode> &nodes,
                            const QString &parentKey, int depth) const;
    // The node addressed by a "menu:<path>" key, or nullptr.
    const XdgMenuNode *nodeForPath(const QString &path) const;

    DesktopEntryIndex *m_apps;
    DockConfig *m_config;
    QStringList m_presentCategories; // friendly labels that have >=1 app, in canonical order
    QList<XdgMenuNode> m_tree;       // submenus from the XDG .menu files
};
