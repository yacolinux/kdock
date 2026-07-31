// XDG application menu backend (KMenu/Kickoff-like), exposed to QML as
// "appMenu". Groups installed .desktop apps by freedesktop main category,
// supports search, and editable favorites (stored in DockConfig).

#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

class DesktopEntryIndex;
class DockConfig;

class AppMenu : public QObject
{
    Q_OBJECT
    // Sidebar labels: "Favorites", "All Applications", then present categories.
    Q_PROPERTY(QStringList categories READ categories NOTIFY changed)
public:
    AppMenu(DesktopEntryIndex *apps, DockConfig *config, QObject *parent = nullptr);

    QStringList categories() const;

    // Each entry is a map { id, name, icon, comment, favorite }.
    Q_INVOKABLE QVariantList appsInCategory(const QString &category) const;
    Q_INVOKABLE QVariantList search(const QString &query) const;
    Q_INVOKABLE QVariantList favorites() const;

    Q_INVOKABLE void launch(const QString &id) const;
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

    DesktopEntryIndex *m_apps;
    DockConfig *m_config;
    QStringList m_presentCategories; // friendly labels that have >=1 app, in canonical order
};
