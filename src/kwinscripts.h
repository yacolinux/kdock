#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QTimer>

// KWin scripts installed via KPackage (KWin/Script) — the tiling / window-
// management scripts that show up in ~/.local/share/kwin/scripts/ and
// /usr/share/kwin/scripts/. KWin 6.6.6 on Wayland, org.kde.KWin /Scripting.
//
// Two sources of truth:
//   - FS + metadata.json (what is installed, its Name/Version/API)
//   - kwinrc [Plugins] <id>Enabled (whether KWin will load it on next
//     reconfigure — the persistent flag)
//   - D-Bus org.kde.kwin.Scripting.isScriptLoaded (whether it is actually
//     running right now — runtime flag, may lag the file)
//
// LXQt note: LXQt does not touch kwinrc [Plugins]; a script being
// Enabled=false is not "LXQt interfering" — it is the installer/systemsettings
// default. This module just makes the flag visible and togglable.

class KWinScripts : public QObject
{
    Q_OBJECT

public:
    explicit KWinScripts(QObject *parent = nullptr);

    struct ScriptInfo
    {
        QString id;          // KPlugin Id (dir name)
        QString name;        // KPlugin Name
        QString description;
        QString version;
        QString api;         // javascript / declarativescript
        QString path;        // dir on disk
        bool enabled = false; // kwinrc [Plugins] idEnabled
        bool loaded = false;  // D-Bus isScriptLoaded
        bool userScope = true; // ~/.local vs /usr
    };

    bool available() const; // hasKWin() — org.kde.KWin on bus
    QVariantList scripts() const; // list of QVariantMap, QML/Widgets friendly
    QList<ScriptInfo> scriptInfos() const;

    // Toggle persistent flag + reconfigure KWin. Returns error string or empty on success.
    QString setEnabled(const QString &id, bool on);
    // Force KWin to re-read kwinrc (cheap, ~30 ms). Returns error or empty.
    QString reconfigure();
    // Install a .kwinscript package. Returns error or empty.
    QString install(const QString &filePath);
    // Uninstall (kpackagetool6 -r). Returns error or empty.
    QString uninstall(const QString &id);

    // Refresh from disk + D-Bus (called on demand, also on file watcher).
    void refresh();

signals:
    void changed();

private:
    QList<ScriptInfo> scan() const;
    bool isLoaded(const QString &id) const;
    bool isEnabled(const QString &id) const;
    QString writeEnabled(const QString &id, bool on) const;
    static QString findTool(const QString &name);
    static QString runTool(const QStringList &args, int timeoutMs = 5000);

    QTimer m_debounce;
    QList<ScriptInfo> m_cache;
    bool m_watching = false;
    void ensureWatched();
};
