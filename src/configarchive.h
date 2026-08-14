// Export/import of the whole kdock configuration as a .zip. The config lives in
// ~/.local/share/kdock/ as kdock.conf (shared: relanzadores, script runners +
// their scripts, ...) plus one kdock-<dock>.conf per dock, and the .conf of each
// accessory binary (previews, tilemenu, controlmanager, weather). Uses QtCore's
// private QZipWriter/QZipReader so there is no external (zip/unzip) runtime
// dependency.
//
// A *preset* is one of those same archives, kept inside the config directory
// (~/.local/share/kdock/presets/<name>.zip) so the Settings dialog can list it
// in a combo and apply it with one click. Same format as an export on purpose:
// an exported .zip can be dropped in as a preset and vice versa.

#pragma once

#include <QString>
#include <QStringList>

class ConfigArchive
{
public:
    // Absolute path of the config directory (~/.local/share/kdock).
    static QString configDir();

    // Absolute path of the presets directory (<configDir>/presets). Created on
    // demand by savePreset(); the readers tolerate it not existing.
    static QString presetsDir();

    // Names (without the .zip) of the saved presets, sorted case-insensitively.
    static QStringList presetNames();

    // Absolute path of the preset with that name. The name is used as a file
    // name, so it goes through sanitizePresetName() first.
    static QString presetPath(const QString &name);

    // A preset name reduced to what can be a file name: path separators and the
    // other characters that would break a file name are replaced by '_', and
    // leading/trailing whitespace is dropped. Returns an empty string for a name
    // that has nothing left (the callers refuse those).
    static QString sanitizePresetName(const QString &name);

    // Save the current configuration as the preset `name` (overwrites).
    static bool savePreset(const QString &name, QString *error = nullptr);

    static bool deletePreset(const QString &name, QString *error = nullptr);
    static bool renamePreset(const QString &from, const QString &to, QString *error = nullptr);

    // Copy an exported .zip into the presets directory under `name`. Validates
    // it is a kdock archive first, so a wrong file is refused before it lands in
    // the list.
    static bool importPreset(const QString &zipPath, const QString &name,
                             QString *error = nullptr);

    // Zip every kdock*.conf into `zipPath` (+ a small manifest). Returns false
    // and sets *error on failure.
    static bool exportTo(const QString &zipPath, QString *error = nullptr);

    // Restore config from `zipPath`. Validates it contains kdock.conf, backs up
    // the current config, then replaces the kdock*.conf files. Rejects entries
    // whose name isn't a plain kdock*.conf (anti zip-slip).
    static bool importFrom(const QString &zipPath, QString *error = nullptr);

    // Whether `zipPath` looks like an archive importFrom() would accept (it
    // carries kdock.conf). Cheap validation for the UI, which refuses a wrong
    // file before copying it anywhere.
    static bool isConfigArchive(const QString &zipPath);

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
