// Pragmatic reader of the XDG menu-spec files (.menu XML), used to recover the
// submenus a user builds with KDE's menu editor and the ones browsers create for
// their web apps. Those live in ~/.config/menus/*.menu and list their members by
// *filename*, so grouping .desktop files by their Categories key (what AppMenu
// does) cannot see them at all — web-app .desktop files carry no Categories.
//
// This is deliberately a subset of the spec: only the pieces needed to recover
// explicit, file-listed submenus. Category-based matching (<Category>, <And>,
// <Or>, <Not>, <OnlyUnallocated>) is ignored on purpose, since AppMenu already
// covers that ground with its own category table. No <Layout> or <Move> either.

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct XdgMenuNode
{
    QString name;              // <Name> — the merge key within a level
    QString label;             // .directory Name= (falls back to name)
    QString icon;              // .directory Icon= (may be empty)
    QStringList includeFiles;  // desktop ids (basename, no ".desktop"), lowercased
    QStringList excludeFiles;
    QList<XdgMenuNode> children;
};

// Top-level submenus that list their members by filename, in document order.
// Empty when no menu file is found (a non-XDG session, say), which simply means
// the app menu keeps only its category grouping.
QList<XdgMenuNode> loadXdgMenuTree();
