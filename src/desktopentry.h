// XDG desktop entry index: maps Wayland app_ids to .desktop files
// (name, icon, launch command). No KDE code, plain freedesktop.org spec.

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

struct DesktopEntry
{
    QString id;      // desktop file id, e.g. "org.kde.dolphin"
    QString name;
    QString icon;
    QString exec;
    QString wmClass;
    QStringList categories; // raw XDG Categories= tokens
    QString comment;        // Comment= (used as menu tooltip)
    bool noDisplay = false;

    bool isValid() const { return !id.isEmpty(); }
};

class DesktopEntryIndex : public QObject
{
    Q_OBJECT
public:
    explicit DesktopEntryIndex(QObject *parent = nullptr);

    void reload();

    DesktopEntry byId(const QString &id) const;
    DesktopEntry forAppId(const QString &appId) const;
    QList<DesktopEntry> all() const; // launchable entries, sorted by name

    static void launch(const DesktopEntry &entry);
    // Parse a .desktop file on disk (a path chosen directly, not an installed
    // id) and return the entry; invalid if it is not a launchable Application.
    static DesktopEntry fromFile(const QString &path);

private:
    void addFile(const QString &path);
    // Shared .desktop parser: fills the entry from path; false when the file is
    // not a launchable Application (used by both addFile and fromFile).
    static bool parseFile(const QString &path, DesktopEntry *out);
    // Chromium web app (PWA) lookup by browser + 32-char extension id, used as
    // the last resort of forAppId(). See the comment on the definition.
    DesktopEntry pwaEntry(const QString &browser, const QString &extId) const;
    // Entry whose binary lives in a directory of this name — the identity a
    // Chromium browser falls back to for its own windows ("msedge").
    DesktopEntry installDirEntry(const QString &token) const;
    bool betterInstallDirMatch(const QString &candidate, const QString &current) const;

    QHash<QString, DesktopEntry> m_byLowerId; // lowercase id -> entry
    QHash<QString, QString> m_wmClassToId;    // lowercase StartupWMClass -> lowercase id
    // Install-directory name -> lowercase id, filled on first use (see
    // installDirEntry). Mutable: forAppId() is const and this is a cache.
    mutable QHash<QString, QString> m_installDirToId;
    mutable bool m_installDirsBuilt = false;
};
