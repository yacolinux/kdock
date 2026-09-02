#include "configarchive.h"

#include "dockconfig.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

#include <private/qzipwriter_p.h>
#include <private/qzipreader_p.h>

QString ConfigArchive::configDir()
{
    return QFileInfo(DockConfig::settingsFilePath()).absolutePath();
}

// The families of settings files that live in the kdock data dir. Keep this
// list as the single source for both export and import: a family present at
// only one end makes a backup look successful while silently not restoring it.
static const QStringList &configPrefixes()
{
    static const QStringList prefixes{QStringLiteral("kdock"),
                                      QStringLiteral("previews"),
                                      QStringLiteral("tilemenu"),
                                      QStringLiteral("controlmanager"),
                                      QStringLiteral("weather"),
                                      QStringLiteral("desktop"),
                                      QStringLiteral("systray"),
                                      QStringLiteral("clipboard")};
    return prefixes;
}

static const QStringList &configGlobs()
{
    static const QStringList globs = [] {
        QStringList out;
        for (const QString &prefix : configPrefixes())
            out.append(prefix + QStringLiteral("*.conf"));
        return out;
    }();
    return globs;
}

static bool isConfigEntry(const QString &name)
{
    // Plain file name, no path separators (anti zip-slip).
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
        || !name.endsWith(QLatin1String(".conf")))
        return false;
    for (const QString &prefix : configPrefixes()) {
        if (name.startsWith(prefix))
            return true;
    }
    return false;
}

constexpr auto kTranslationsDir = "translations";
constexpr auto kTranslationsMarker = "translations/.kdock-archive";

static bool isTranslationEntry(const QString &name)
{
    const QString prefix = QLatin1String(kTranslationsDir) + QLatin1Char('/');
    if (!name.startsWith(prefix) || name == QLatin1String(kTranslationsMarker))
        return false;
    const QString fileName = name.mid(prefix.size());
    // Translation names are made by Translations::createFrom(), which permits
    // only one safe file name. The check also keeps a crafted archive from
    // escaping the translations directory.
    return !fileName.isEmpty() && !fileName.contains(QLatin1Char('/'))
           && !fileName.contains(QLatin1Char('\\'))
           && fileName.endsWith(QLatin1String(".md"));
}

static QString translationsDirPath(const QDir &configDir)
{
    return configDir.filePath(QLatin1String(kTranslationsDir));
}

// Which glob an archive entry belongs to, so importing only clears the families
// the archive actually carries (see importFrom).
static QString familyOf(const QString &name)
{
    for (const QString &glob : configGlobs()) {
        const QString prefix = glob.left(glob.indexOf(QLatin1Char('*')));
        if (name.startsWith(prefix))
            return glob;
    }
    return {};
}

bool ConfigArchive::exportTo(const QString &zipPath, QString *error)
{
    // The dock keeps one QSettings object per live instance. Flush those
    // pending writes before reading the files below; otherwise exporting from
    // the settings dialog can miss the change that was just made there.
    DockConfig::syncAll();

    const QDir dir(configDir());
    const QStringList files = dir.entryList(configGlobs(), QDir::Files);
    if (!files.contains(QStringLiteral("kdock.conf"))) {
        if (error)
            *error = QStringLiteral("No shared configuration file found in %1").arg(dir.path());
        return false;
    }

    QZipWriter zw(zipPath);
    if (zw.status() != QZipWriter::NoError) {
        if (error)
            *error = QStringLiteral("Cannot open %1 for writing").arg(zipPath);
        return false;
    }

    for (const QString &name : files) {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Cannot read %1").arg(f.fileName());
            zw.close();
            return false;
        }
        zw.addFile(name, f.readAll());
    }

    // The selected language only names a file in kdock.conf; the editable
    // translation layers themselves live in this directory and must travel
    // with a complete configuration. The marker lets import distinguish a new
    // archive with an intentionally empty directory from old archives that
    // predate translation backups.
    const QDir translations(translationsDirPath(dir));
    for (const QString &name : translations.entryList({QStringLiteral("*.md")}, QDir::Files)) {
        QFile f(translations.filePath(name));
        if (!f.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Cannot read %1").arg(f.fileName());
            zw.close();
            return false;
        }
        zw.addFile(QLatin1String(kTranslationsDir) + QLatin1Char('/') + name, f.readAll());
    }
    zw.addFile(QLatin1String(kTranslationsMarker), QByteArray());

    // Small manifest for validation / provenance.
    const QString manifest =
        QStringLiteral("{\n  \"app\": \"kdock\",\n  \"version\": 2,\n  \"exported\": \"%1\"\n}\n")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    zw.addFile(QStringLiteral("kdock-export.json"), manifest.toUtf8());

    zw.close();
    if (zw.status() != QZipWriter::NoError) {
        if (error)
            *error = QStringLiteral("Error writing %1").arg(zipPath);
        return false;
    }
    return true;
}

bool ConfigArchive::importFrom(const QString &zipPath, QString *error)
{
    QZipReader zr(zipPath);
    if (!zr.exists() || zr.status() != QZipReader::NoError) {
        if (error)
            *error = QStringLiteral("Cannot open %1").arg(zipPath);
        return false;
    }

    // Collect valid config entries; require kdock.conf to be present.
    QList<QPair<QString, QByteArray>> entries;
    QStringList families;
    bool hasShared = false;
    bool hasTranslations = false;
    for (const QZipReader::FileInfo &fi : zr.fileInfoList()) {
        if (!fi.isFile)
            continue;
        if (isConfigEntry(fi.filePath)) {
            if (fi.filePath == QLatin1String("kdock.conf"))
                hasShared = true;
            const QString family = familyOf(fi.filePath);
            if (!family.isEmpty() && !families.contains(family))
                families.append(family);
            entries.append({fi.filePath, zr.fileData(fi.filePath)});
        } else if (fi.filePath == QLatin1String(kTranslationsMarker)) {
            hasTranslations = true;
        } else if (isTranslationEntry(fi.filePath)) {
            hasTranslations = true; // Also accept an early archive without the marker.
            entries.append({fi.filePath, zr.fileData(fi.filePath)});
        }
    }
    if (!hasShared) {
        if (error)
            *error = QStringLiteral("Not a valid kdock config archive (missing kdock.conf)");
        return false;
    }

    const QDir dir(configDir());
    dir.mkpath(QStringLiteral("."));

    // Back up the current config before replacing it.
    const QString backup =
        QStringLiteral("backup-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QStringList current = dir.entryList(configGlobs(), QDir::Files);
    const QDir translations(translationsDirPath(dir));
    const QStringList currentTranslations =
        translations.entryList({QStringLiteral("*.md")}, QDir::Files);
    if (!current.isEmpty() || !currentTranslations.isEmpty()) {
        dir.mkpath(backup);
        for (const QString &name : current)
            QFile::copy(dir.filePath(name), dir.filePath(backup + QLatin1Char('/') + name));
        if (!currentTranslations.isEmpty()) {
            dir.mkpath(backup + QLatin1String("/translations"));
            for (const QString &name : currentTranslations) {
                QFile::copy(translations.filePath(name),
                            dir.filePath(backup + QLatin1String("/translations/") + name));
            }
        }
    }

    // Clean replace, but only of the families the archive carries: an archive
    // made before the tile menu existed must not delete the layout the user has
    // built since. (Everything is in the backup directory either way.)
    const QStringList doomed = dir.entryList(families, QDir::Files);
    for (const QString &name : doomed)
        QFile::remove(dir.filePath(name));
    if (hasTranslations) {
        for (const QString &name : currentTranslations)
            QFile::remove(translations.filePath(name));
    }

    for (const auto &e : entries) {
        const QString target = isTranslationEntry(e.first)
                                   ? translationsDirPath(dir) + QLatin1Char('/')
                                         + e.first.mid(QStringLiteral("translations/").size())
                                   : dir.filePath(e.first);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile f(target);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error)
                *error = QStringLiteral("Cannot write %1").arg(target);
            return false;
        }
        if (f.write(e.second) != e.second.size()) {
            if (error)
                *error = QStringLiteral("Cannot write %1").arg(target);
            return false;
        }
    }
    return true;
}

bool ConfigArchive::isConfigArchive(const QString &zipPath)
{
    QZipReader zr(zipPath);
    if (!zr.exists() || zr.status() != QZipReader::NoError)
        return false;
    for (const QZipReader::FileInfo &fi : zr.fileInfoList()) {
        if (fi.isFile && fi.filePath == QLatin1String("kdock.conf"))
            return true;
    }
    return false;
}

QString ConfigArchive::presetsDir()
{
    return configDir() + QStringLiteral("/presets");
}

QString ConfigArchive::sanitizePresetName(const QString &name)
{
    QString out = name.simplified();
    // Everything that would make this a path or an awkward file name. The list
    // is deliberately wider than POSIX needs: a preset name also ends up in a
    // combo and in an error message.
    static const QRegularExpression bad(QStringLiteral("[/\\\\:*?\"<>|]"));
    out.replace(bad, QStringLiteral("_"));
    while (out.startsWith(QLatin1Char('.')))
        out.remove(0, 1);
    return out.trimmed();
}

QString ConfigArchive::presetPath(const QString &name)
{
    return presetsDir() + QLatin1Char('/') + sanitizePresetName(name) + QStringLiteral(".zip");
}

QStringList ConfigArchive::presetNames()
{
    const QDir dir(presetsDir());
    QStringList names;
    for (const QString &file : dir.entryList({QStringLiteral("*.zip")}, QDir::Files))
        names.append(file.chopped(4));
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return names;
}

bool ConfigArchive::savePreset(const QString &name, QString *error)
{
    const QString clean = sanitizePresetName(name);
    if (clean.isEmpty()) {
        if (error)
            *error = QStringLiteral("Empty preset name");
        return false;
    }
    if (!QDir().mkpath(presetsDir())) {
        if (error)
            *error = QStringLiteral("Cannot create %1").arg(presetsDir());
        return false;
    }
    const QString path = presetPath(clean);
    // QZipWriter appends to an existing file, so overwriting a preset without
    // removing it first leaves both copies inside the archive.
    QFile::remove(path);
    return exportTo(path, error);
}

bool ConfigArchive::deletePreset(const QString &name, QString *error)
{
    const QString path = presetPath(name);
    if (!QFile::exists(path)) {
        if (error)
            *error = QStringLiteral("No such preset: %1").arg(name);
        return false;
    }
    if (!QFile::remove(path)) {
        if (error)
            *error = QStringLiteral("Cannot delete %1").arg(path);
        return false;
    }
    return true;
}

bool ConfigArchive::renamePreset(const QString &from, const QString &to, QString *error)
{
    const QString clean = sanitizePresetName(to);
    if (clean.isEmpty()) {
        if (error)
            *error = QStringLiteral("Empty preset name");
        return false;
    }
    const QString src = presetPath(from);
    const QString dst = presetPath(clean);
    if (src == dst)
        return true;
    if (QFile::exists(dst) && !QFile::remove(dst)) {
        if (error)
            *error = QStringLiteral("Cannot replace %1").arg(dst);
        return false;
    }
    if (!QFile::rename(src, dst)) {
        if (error)
            *error = QStringLiteral("Cannot rename %1").arg(src);
        return false;
    }
    return true;
}

bool ConfigArchive::importPreset(const QString &zipPath, const QString &name, QString *error)
{
    if (!isConfigArchive(zipPath)) {
        if (error)
            *error = QStringLiteral("Not a valid kdock config archive (missing kdock.conf)");
        return false;
    }
    const QString clean = sanitizePresetName(name);
    if (clean.isEmpty()) {
        if (error)
            *error = QStringLiteral("Empty preset name");
        return false;
    }
    if (!QDir().mkpath(presetsDir())) {
        if (error)
            *error = QStringLiteral("Cannot create %1").arg(presetsDir());
        return false;
    }
    const QString dst = presetPath(clean);
    QFile::remove(dst);
    if (!QFile::copy(zipPath, dst)) {
        if (error)
            *error = QStringLiteral("Cannot copy %1 to %2").arg(zipPath, dst);
        return false;
    }
    return true;
}

bool ConfigArchive::exportFavorites(const QString &path, const QStringList &favorites,
                                    QString *error)
{
    QJsonArray arr;
    for (const QString &id : favorites)
        arr.append(id);

    QJsonObject root;
    root[QStringLiteral("app")] = QStringLiteral("kdock-favorites");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("exported")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root[QStringLiteral("favorites")] = arr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("Cannot open %1 for writing").arg(path);
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool ConfigArchive::importFavorites(const QString &path, QStringList *favorites,
                                    QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open %1").arg(path);
        return false;
    }

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("Not a valid JSON file: %1").arg(perr.errorString());
        return false;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("app")).toString() != QLatin1String("kdock-favorites")) {
        if (error)
            *error = QStringLiteral("Not a kdock favorites file");
        return false;
    }
    if (!root.value(QStringLiteral("favorites")).isArray()) {
        if (error)
            *error = QStringLiteral("Missing \"favorites\" list");
        return false;
    }

    QStringList list;
    for (const QJsonValue &v : root.value(QStringLiteral("favorites")).toArray()) {
        const QString id = v.toString();
        if (!id.isEmpty() && !list.contains(id))
            list.append(id);
    }
    if (favorites)
        *favorites = list;
    return true;
}
