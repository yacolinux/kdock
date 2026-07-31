// Export/import of the whole kdock configuration as a .zip. The config lives in
// ~/.local/share/kdock/ as kdock.conf (shared: relanzadores, script runners +
// their scripts, ...) plus one kdock-<dock>.conf per dock. Uses QtCore's private
// QZipWriter/QZipReader so there is no external (zip/unzip) runtime dependency.

#pragma once

#include <QString>

class ConfigArchive
{
public:
    // Absolute path of the config directory (~/.local/share/kdock).
    static QString configDir();

    // Zip every kdock*.conf into `zipPath` (+ a small manifest). Returns false
    // and sets *error on failure.
    static bool exportTo(const QString &zipPath, QString *error = nullptr);

    // Restore config from `zipPath`. Validates it contains kdock.conf, backs up
    // the current config, then replaces the kdock*.conf files. Rejects entries
    // whose name isn't a plain kdock*.conf (anti zip-slip).
    static bool importFrom(const QString &zipPath, QString *error = nullptr);

    // Export just the menu favorites (a list of .desktop ids) as a small JSON
    // file: { "app": "kdock-favorites", "version": 1, "favorites": [...] }.
    static bool exportFavorites(const QString &path, const QStringList &favorites,
                                QString *error = nullptr);

    // Import a favorites JSON written by exportFavorites(). Validates the "app"
    // marker and that "favorites" is an array of strings. Returns false and sets
    // *error on failure; otherwise fills *favorites.
    static bool importFavorites(const QString &path, QStringList *favorites,
                                QString *error = nullptr);
};
