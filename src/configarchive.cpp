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

#include <private/qzipwriter_p.h>
#include <private/qzipreader_p.h>

QString ConfigArchive::configDir()
{
    return QFileInfo(DockConfig::settingsFilePath()).absolutePath();
}

// The three families of settings files that live in the kdock data dir: the
// docks themselves, the preview strips and the tile menu. The accessory
// binaries keep their own files, and leaving them out of the backup meant a
// restore silently dropped the tile layout the user had built.
static const QStringList &configGlobs()
{
    static const QStringList globs{QStringLiteral("kdock*.conf"),
                                   QStringLiteral("previews*.conf"),
                                   QStringLiteral("tilemenu*.conf"),
                                   QStringLiteral("controlmanager*.conf")};
    return globs;
}

static bool isConfigEntry(const QString &name)
{
    // Plain file name, no path separators (anti zip-slip).
    static const QRegularExpression re(
        QStringLiteral("^(kdock|previews|tilemenu)[\\w.#-]*\\.conf$"));
    return re.match(name).hasMatch();
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
    const QDir dir(configDir());
    const QStringList files = dir.entryList(configGlobs(), QDir::Files);
    if (files.isEmpty()) {
        if (error)
            *error = QStringLiteral("No configuration files found in %1").arg(dir.path());
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
        if (!f.open(QIODevice::ReadOnly))
            continue;
        zw.addFile(name, f.readAll());
    }

    // Small manifest for validation / provenance.
    const QString manifest =
        QStringLiteral("{\n  \"app\": \"kdock\",\n  \"version\": 1,\n  \"exported\": \"%1\"\n}\n")
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
    for (const QZipReader::FileInfo &fi : zr.fileInfoList()) {
        if (!fi.isFile || !isConfigEntry(fi.filePath))
            continue;
        if (fi.filePath == QLatin1String("kdock.conf"))
            hasShared = true;
        const QString family = familyOf(fi.filePath);
        if (!family.isEmpty() && !families.contains(family))
            families.append(family);
        entries.append({fi.filePath, zr.fileData(fi.filePath)});
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
    if (!current.isEmpty()) {
        dir.mkpath(backup);
        for (const QString &name : current)
            QFile::copy(dir.filePath(name), dir.filePath(backup + QLatin1Char('/') + name));
    }

    // Clean replace, but only of the families the archive carries: an archive
    // made before the tile menu existed must not delete the layout the user has
    // built since. (Everything is in the backup directory either way.)
    const QStringList doomed = dir.entryList(families, QDir::Files);
    for (const QString &name : doomed)
        QFile::remove(dir.filePath(name));

    for (const auto &e : entries) {
        QFile f(dir.filePath(e.first));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(e.second);
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
