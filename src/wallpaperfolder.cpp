#include "wallpaperfolder.h"

#include <QDir>
#include <QDirIterator>
#include <QRandomGenerator>

#include <algorithm>

namespace WallpaperFolder {

QStringList images(const QStringList &folders)
{
    static const QStringList kFilters = {
        QStringLiteral("*.jpg"),  QStringLiteral("*.jpeg"), QStringLiteral("*.png"),
        QStringLiteral("*.webp"), QStringLiteral("*.bmp"),  QStringLiteral("*.gif"),
        QStringLiteral("*.svg"),  QStringLiteral("*.svgz")};

    QStringList files;
    for (const QString &folder : folders) {
        if (folder.isEmpty())
            continue;
        QDirIterator it(folder, kFilters, QDir::Files);
        while (it.hasNext())
            files << it.next();
    }
    files.removeDuplicates();

    // Stable order so "next" is deterministic regardless of how the files came
    // off the filesystem.
    std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return files;
}

QString next(const QStringList &folders, const QString &currentPath)
{
    const QStringList files = images(folders);
    if (files.isEmpty())
        return {};
    const int idx = files.indexOf(currentPath);
    // idx < 0 (current not in the set) → start at the first image.
    return files.at((idx + 1) % files.size());
}

QString randomOther(const QStringList &folders, const QStringList &recent)
{
    const QStringList files = images(folders);
    if (files.isEmpty())
        return {};
    if (files.size() == 1)
        return files.first();

    // Drop the oldest constraint until something is left to choose from, so a
    // folder of two pictures still alternates instead of returning nothing and
    // a folder of one is handled above. `recent` is oldest first, so dropping
    // from the front keeps the *most* recent exclusions the longest — the last
    // image shown is the one it matters most not to repeat.
    QStringList avoid = recent;
    while (true) {
        QStringList candidates;
        for (const QString &f : files) {
            if (!avoid.contains(f))
                candidates << f;
        }
        if (!candidates.isEmpty()) {
            return candidates.at(int(QRandomGenerator::global()->bounded(
                quint32(candidates.size()))));
        }
        if (avoid.isEmpty())
            break;
        avoid.removeFirst();
    }
    return files.first(); // unreachable: `avoid` empty leaves every file a candidate
}

} // namespace WallpaperFolder
