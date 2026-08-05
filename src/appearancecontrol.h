// Global KDE appearance: lists the installed icon themes and color schemes and
// applies them system-wide, exposed to QML as "appearance". One shared instance
// backs both dock widgets (icon-theme picker and color-scheme picker).
//
// Listing is done by reading the files, not by calling the Plasma tools: their
// --list output is translated, so parsing it breaks on a non-English desktop.
// Applying does go through the Plasma tools, which is what tells the running
// applications to reload (writing kdeglobals alone only convinces new ones).
//
// The current values come from ~/.config/kdeglobals; the shared Theme already
// watches that file, so this class just re-reads it when Theme says so.
//
// It also owns the *favourites*: the ids the user pinned to the top of either
// list. There is one list per kind for the whole process, persisted in the
// shared kdock.conf, so every dock's picker and the Settings dialog show the
// same order - this class is already the single instance all of them talk to.

#pragma once

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>

class Theme;

class AppearanceControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentIconTheme READ currentIconTheme NOTIFY changed)
    Q_PROPERTY(QString currentColorScheme READ currentColorScheme NOTIFY changed)

public:
    explicit AppearanceControl(Theme *theme = nullptr, QObject *parent = nullptr);

    QString currentIconTheme() const { return m_currentIconTheme; }
    QString currentColorScheme() const { return m_currentColorScheme; }

    // Installed icon themes: { id, name, current, fav }.
    Q_INVOKABLE QVariantList iconThemes();
    // Installed color schemes: { id, name, bg, fg, sel, current, fav }, the
    // three colors being #RRGGBB strings taken from the scheme so a row can
    // preview it.
    Q_INVOKABLE QVariantList colorSchemes();

    // Both lists come out favourites first, alphabetical within each block, so
    // no caller has to repeat the ordering.
    //
    // "kind" is "icons" or "colors" - the same string the QML popup already
    // carries as its mode, so a row can pass it straight through.
    Q_INVOKABLE bool isFavorite(const QString &kind, const QString &id) const;
    Q_INVOKABLE void setFavorite(const QString &kind, const QString &id, bool on);

    // Rescan if any of the color-scheme directories changed since the last one.
    // Cheap enough to call every time the popup opens (a few stat() calls);
    // the scan itself parses ~450 INI files on a full KDE install.
    Q_INVOKABLE void refreshIfStale();

    // Apply system-wide. Both are fire-and-forget (startDetached): the tools
    // take a moment and nothing here depends on their result. Deliberately
    // leaves kdock's own icon-theme override alone (see Theme::setIconTheme):
    // a dock with an override keeps its icons and only the rest of the desktop
    // follows.
    Q_INVOKABLE void applyIconTheme(const QString &id);
    Q_INVOKABLE void applyColorScheme(const QString &id);

signals:
    void changed();
    // A favourite was added or removed: the lists reordered, nothing applied.
    void favoritesChanged();

private:
    void readCurrent();
    void scanColorSchemes();
    void loadFavorites();
    QSet<QString> &favoriteSet(const QString &kind);
    const QSet<QString> &favoriteSet(const QString &kind) const;
    // Favourites first, then by name, both case-insensitive. Takes the list by
    // value because colorSchemes() hands over a copy of its cache.
    static QVariantList sortedByFavorite(QVariantList entries);
    // Absolute path of a Plasma helper, or empty. plasma-changeicons is not on
    // PATH: it lives in a libexec dir that varies by distribution.
    static QString findTool(const QString &name, const QStringList &extraDirs);

    QString m_currentIconTheme;
    QString m_currentColorScheme;

    QVariantList m_colorSchemes;
    QDateTime m_colorSchemesStamp; // newest mtime of the scanned directories
    bool m_scanned = false;

    // Ids only: an uninstalled theme keeps its mark, it just does not show up
    // in the list until it is back.
    QSet<QString> m_favoriteIcons;
    QSet<QString> m_favoriteColors;
};
