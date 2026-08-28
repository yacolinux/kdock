#include "desktopentry.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>

DesktopEntryIndex::DesktopEntryIndex(QObject *parent)
    : QObject(parent)
{
    reload();
}

void DesktopEntryIndex::reload()
{
    m_byLowerId.clear();
    m_wmClassToId.clear();
    m_installDirToId.clear();
    m_installDirsBuilt = false;

    // Earlier locations (user dirs) take precedence over later ones.
    const QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &dir : dirs) {
        QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
            addFile(it.next());
    }
}

void DesktopEntryIndex::addFile(const QString &path)
{
    const QString lowerId = QFileInfo(path).completeBaseName().toLower();
    if (m_byLowerId.contains(lowerId))
        return; // already provided by a higher-priority dir

    DesktopEntry e;
    if (!parseFile(path, &e))
        return;

    m_byLowerId.insert(lowerId, e);
    if (!e.wmClass.isEmpty())
        m_wmClassToId.insert(e.wmClass.toLower(), lowerId);
}

bool DesktopEntryIndex::parseFile(const QString &path, DesktopEntry *out)
{
    DesktopEntry e;
    e.id = QFileInfo(path).completeBaseName();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&f);
    bool inEntry = false;
    bool isApplication = false;
    bool hidden = false;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inEntry = (line == QLatin1String("[Desktop Entry]"));
            continue;
        }
        if (!inEntry || line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();

        if (key == QLatin1String("Type"))
            isApplication = (value == QLatin1String("Application"));
        else if (key == QLatin1String("Name") && e.name.isEmpty())
            e.name = value;
        else if (key == QLatin1String("Icon"))
            e.icon = value;
        else if (key == QLatin1String("Exec"))
            e.exec = value;
        else if (key == QLatin1String("StartupWMClass"))
            e.wmClass = value;
        else if (key == QLatin1String("Categories"))
            e.categories = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        else if (key == QLatin1String("Comment") && e.comment.isEmpty())
            e.comment = value;
        else if (key == QLatin1String("NoDisplay"))
            e.noDisplay = (value == QLatin1String("true"));
        else if (key == QLatin1String("Hidden"))
            hidden = (value == QLatin1String("true"));
    }

    if (!isApplication || hidden || e.exec.isEmpty())
        return false;
    *out = e;
    return true;
}

DesktopEntry DesktopEntryIndex::fromFile(const QString &path)
{
    DesktopEntry e;
    if (!parseFile(path, &e))
        return {};
    return e;
}

DesktopEntry DesktopEntryIndex::byId(const QString &id) const
{
    return m_byLowerId.value(id.toLower());
}

DesktopEntry DesktopEntryIndex::forAppId(const QString &appId) const
{
    const QString lower = appId.toLower();
    const DesktopEntry exact = m_byLowerId.value(lower);

    // Chromium can leave a NoDisplay copy under the exact app_id while the
    // visible launcher has the extra underscore used by some Chromium builds:
    //   window:  msedge-<extid>-Default
    //   visible: msedge-_<extid>-Default
    // Returning the exact hidden copy makes the model key differ from the
    // user's pinned launcher, so the icon stays idle and monitor-wide appsel
    // filters fail to recognize it. Resolve a recognizable PWA deterministically
    // before accepting an exact NoDisplay match; a visible exact entry still
    // keeps the normal priority.
    static const QRegularExpression pwaExtIdRe(
        QStringLiteral("^(msedge|chrome|chromium|edge|brave|vivaldi)-[_-]?([a-p]{32})(?:-default)?$"));
    const QRegularExpressionMatch pwaMatch = pwaExtIdRe.match(lower);
    DesktopEntry pwa;
    if (pwaMatch.hasMatch()) {
        pwa = pwaEntry(pwaMatch.captured(1), pwaMatch.captured(2));
        if (pwa.isValid() && (!exact.isValid() || exact.noDisplay))
            return pwa;
    }

    if (exact.isValid())
        return exact;
    if (m_wmClassToId.contains(lower))
        return m_byLowerId.value(m_wmClassToId.value(lower));

    // "org.kde.dolphin" -> "dolphin"
    const QString lastSegment = lower.section(QLatin1Char('.'), -1);
    if (lastSegment != lower && m_byLowerId.contains(lastSegment))
        return m_byLowerId.value(lastSegment);

    // "dolphin" -> "org.kde.dolphin"
    for (auto it = m_byLowerId.constBegin(); it != m_byLowerId.constEnd(); ++it) {
        if (it.key().endsWith(QLatin1Char('.') + lower))
            return it.value();
    }

    // Chromium browsers name their windows after the *install directory* of the
    // real binary when the window has no better identity of its own: a freshly
    // opened Edge window arrives as "msedge" (from
    // /opt/microsoft/msedge/microsoft-edge), never as "microsoft-edge", and no
    // .desktop declares that name. Without this the window misses its pinned
    // launcher, gets a stray icon of its own, and the launcher — still looking
    // empty — starts another instance on the next click.
    return installDirEntry(lower);
}

// Entry whose executable lives in a directory named `token`, i.e. the fallback
// identity Chromium hands out (msedge, brave, chrome, vivaldi). Built lazily:
// it needs the symlink resolved (/usr/bin/microsoft-edge-stable ->
// /opt/microsoft/msedge/microsoft-edge), which is too much work to do for every
// entry up front, and this is only ever reached when everything else missed.
DesktopEntry DesktopEntryIndex::installDirEntry(const QString &token) const
{
    if (!m_installDirsBuilt) {
        m_installDirsBuilt = true;
        // Directories that say nothing about which application this is.
        static const QStringList generic = {
            QStringLiteral("bin"),   QStringLiteral("sbin"),  QStringLiteral("usr"),
            QStringLiteral("local"), QStringLiteral("opt"),   QStringLiteral("games"),
            QStringLiteral("share"), QStringLiteral("libexec"),
        };
        for (auto it = m_byLowerId.constBegin(); it != m_byLowerId.constEnd(); ++it) {
            const QStringList parts = QProcess::splitCommand(it.value().exec);
            if (parts.isEmpty())
                continue;
            const QString real = QFileInfo(parts.first()).canonicalFilePath();
            if (real.isEmpty())
                continue;
            const QString dir = QFileInfo(real).dir().dirName().toLower();
            if (dir.isEmpty() || generic.contains(dir))
                continue;
            // The browser itself wins over its own web apps, which share the
            // binary but pass --app-id; otherwise keep the lowest id so the
            // pick never depends on hash order.
            const QString current = m_installDirToId.value(dir);
            if (current.isEmpty() || betterInstallDirMatch(it.key(), current))
                m_installDirToId.insert(dir, it.key());
        }
    }
    return m_byLowerId.value(m_installDirToId.value(token));
}

bool DesktopEntryIndex::betterInstallDirMatch(const QString &candidate,
                                              const QString &current) const
{
    const auto isWebApp = [this](const QString &id) {
        const DesktopEntry &e = m_byLowerId[id];
        return e.exec.contains(QLatin1String("--app-id="))
               || e.wmClass.startsWith(QLatin1String("crx_"), Qt::CaseInsensitive);
    };
    const bool candWeb = isWebApp(candidate);
    const bool curWeb = isWebApp(current);
    if (candWeb != curWeb)
        return !candWeb;
    const DesktopEntry &cand = m_byLowerId[candidate];
    const DesktopEntry &cur = m_byLowerId[current];
    if (cand.noDisplay != cur.noDisplay)
        return !cand.noDisplay;
    return candidate < current;
}

// Resolve one Chromium web app from its browser and extension id. The same web
// app is often installed under several browsers (and Edge writes a second,
// NoDisplay copy of each entry), so several .desktop files can carry the same
// extension id: pick deterministically instead of taking whatever the hash
// hands out first, or the dock would group an Edge window under the Brave
// launcher and could even change its mind between runs.
DesktopEntry DesktopEntryIndex::pwaEntry(const QString &browser, const QString &extId) const
{
    // Chromium writes StartupWMClass=crx_<extid>; Edge doubles the underscore.
    QStringList candidates;
    for (const QString &wmClass : {QStringLiteral("crx__") + extId,
                                   QStringLiteral("crx_") + extId}) {
        const QString id = m_wmClassToId.value(wmClass);
        if (!id.isEmpty() && !candidates.contains(id))
            candidates.append(id);
    }
    // ...plus any entry whose id embeds the extension id, for the web apps
    // whose .desktop has no StartupWMClass at all.
    for (auto it = m_byLowerId.constBegin(); it != m_byLowerId.constEnd(); ++it) {
        if (it.key().contains(extId) && !candidates.contains(it.key()))
            candidates.append(it.key());
    }
    if (candidates.isEmpty())
        return {};

    // Same browser first, then a visible entry over a NoDisplay duplicate, then
    // the lowest id so the choice never depends on hash order.
    std::sort(candidates.begin(), candidates.end(),
              [this, &browser](const QString &a, const QString &b) {
                  const bool aBrowser = a.startsWith(browser);
                  const bool bBrowser = b.startsWith(browser);
                  if (aBrowser != bBrowser)
                      return aBrowser;
                  const bool aShown = !m_byLowerId.value(a).noDisplay;
                  const bool bShown = !m_byLowerId.value(b).noDisplay;
                  if (aShown != bShown)
                      return aShown;
                  return a < b;
              });
    return m_byLowerId.value(candidates.first());
}

QList<DesktopEntry> DesktopEntryIndex::all() const
{
    QList<DesktopEntry> list;
    for (const DesktopEntry &e : m_byLowerId) {
        if (!e.noDisplay)
            list.append(e);
    }
    std::sort(list.begin(), list.end(), [](const DesktopEntry &a, const DesktopEntry &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return list;
}

void DesktopEntryIndex::launch(const DesktopEntry &entry)
{
    if (entry.exec.isEmpty())
        return;
    QStringList parts = QProcess::splitCommand(entry.exec);
    // Strip freedesktop Exec field codes (%f, %u, %U, ...)
    parts.erase(std::remove_if(parts.begin(), parts.end(),
                               [](const QString &p) { return p.startsWith(QLatin1Char('%')); }),
                parts.end());
    if (parts.isEmpty())
        return;
    const QString program = parts.takeFirst();
    QProcess::startDetached(program, parts);
}
