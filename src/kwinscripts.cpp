#include "kwinscripts.h"

#include "session.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString kwinrcPath()
{
    const QString p = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kwinrc"));
    if (!p.isEmpty())
        return p;
    // fallback: ~/.config/kwinrc
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(QStringLiteral("kwinrc"));
}

QString findToolInternal(const QString &name)
{
    QString t = QStandardPaths::findExecutable(name);
    if (!t.isEmpty())
        return t;
    static const QStringList dirs = {QStringLiteral("/usr/bin"), QStringLiteral("/opt/kde/bin")};
    t = QStandardPaths::findExecutable(name, dirs);
    return t;
}

} // namespace

KWinScripts::KWinScripts(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(400);
    connect(&m_debounce, &QTimer::timeout, this, &KWinScripts::refresh);
}

bool KWinScripts::available() const
{
    return Session::hasKWin();
}

QString KWinScripts::findTool(const QString &name)
{
    return findToolInternal(name);
}

QString KWinScripts::runTool(const QStringList &args, int timeoutMs)
{
    if (args.isEmpty())
        return QStringLiteral("no args");
    const QString tool = findToolInternal(args.first());
    if (tool.isEmpty())
        return QStringLiteral("tool not found: ") + args.first();
    QProcess proc;
    proc.start(tool, args.mid(1));
    if (!proc.waitForFinished(timeoutMs))
        return QStringLiteral("timeout: ") + tool;
    if (proc.exitCode() != 0) {
        QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (err.isEmpty())
            err = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (err.isEmpty())
            err = QStringLiteral("exit %1").arg(proc.exitCode());
        return err;
    }
    return {};
}

QList<KWinScripts::ScriptInfo> KWinScripts::scan() const
{
    QList<ScriptInfo> out;
    QSet<QString> seen;

    const QStringList roots = {
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("kwin/scripts")),
        QStringLiteral("/usr/share/kwin/scripts"),
    };

    for (const QString &root : roots) {
        QDir dir(root);
        if (!dir.exists())
            continue;
        const bool userScope = root.startsWith(QDir::homePath());
        const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &name : entries) {
            if (seen.contains(name))
                continue;
            const QString metaPath = dir.filePath(name + QStringLiteral("/metadata.json"));
            QFileInfo fi(metaPath);
            if (!fi.exists())
                continue;
            QFile f(metaPath);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (!doc.isObject())
                continue;
            QJsonObject obj = doc.object();
            if (obj.value(QStringLiteral("KPackageStructure")).toString() != QStringLiteral("KWin/Script"))
                continue;
            QJsonObject plugin = obj.value(QStringLiteral("KPlugin")).toObject();
            ScriptInfo info;
            info.id = plugin.value(QStringLiteral("Id")).toString(name);
            info.name = plugin.value(QStringLiteral("Name")).toString(name);
            info.description = plugin.value(QStringLiteral("Description")).toString();
            info.version = plugin.value(QStringLiteral("Version")).toString();
            info.api = obj.value(QStringLiteral("X-Plasma-API")).toString();
            info.path = dir.filePath(name);
            info.userScope = userScope;
            info.enabled = isEnabled(info.id);
            info.loaded = isLoaded(info.id);
            out.append(info);
            seen.insert(name);
        }
    }
    // Sort by name case-insensitive for stable UI
    std::sort(out.begin(), out.end(), [](const ScriptInfo &a, const ScriptInfo &b) {
        return a.name.toLower() < b.name.toLower();
    });
    return out;
}

bool KWinScripts::isEnabled(const QString &id) const
{
    const QString path = kwinrcPath();
    if (path.isEmpty())
        return false;
    QSettings s(path, QSettings::IniFormat);
    s.sync();
    const QString key = id + QStringLiteral("Enabled");
    // [Plugins] group — QSettings maps it as "Plugins/<key>"
    return s.value(QStringLiteral("Plugins/") + key, false).toBool();
}

bool KWinScripts::isLoaded(const QString &id) const
{
    if (!Session::hasKWin())
        return false;
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                      QStringLiteral("/Scripting"),
                                                      QStringLiteral("org.kde.kwin.Scripting"),
                                                      QStringLiteral("isScriptLoaded"));
    msg.setArguments({id});
    // Short timeout: KWin can be busy right after a restart, and a default 25s
    // block per script (8*25s worst) would freeze the UI for seconds. 800ms is
    // enough for a local call and matches the other KWin call sites.
    QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 800);
    if (reply.type() == QDBusMessage::ErrorMessage)
        return false;
    if (reply.arguments().isEmpty())
        return false;
    return reply.arguments().first().toBool();
}

QString KWinScripts::writeEnabled(const QString &id, bool on) const
{
    // Use kwriteconfig6 so we keep KConfig syntax ($Version etc.) intact —
    // same reason QtCompat::syncKdeUiSettings does not use QSettings to write kdeglobals.
    QString tool = findToolInternal(QStringLiteral("kwriteconfig6"));
    if (!tool.isEmpty()) {
        QProcess proc;
        proc.start(tool, {QStringLiteral("--file"), QStringLiteral("kwinrc"),
                          QStringLiteral("--group"), QStringLiteral("Plugins"),
                          QStringLiteral("--key"), id + QStringLiteral("Enabled"),
                          on ? QStringLiteral("true") : QStringLiteral("false")});
        if (!proc.waitForFinished(1200))
            return QStringLiteral("timeout kwriteconfig6");
        if (proc.exitCode() != 0) {
            QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
            return err.isEmpty() ? QStringLiteral("kwriteconfig6 failed %1").arg(proc.exitCode()) : err;
        }
        return {};
    }
    // Fallback: QSettings (may lose $ markers but functional)
    const QString path = kwinrcPath();
    if (path.isEmpty())
        return QStringLiteral("kwinrc not found");
    QSettings s(path, QSettings::IniFormat);
    s.setValue(QStringLiteral("Plugins/") + id + QStringLiteral("Enabled"), on);
    s.sync();
    if (s.status() != QSettings::NoError)
        return QStringLiteral("QSettings write failed");
    return {};
}

QString KWinScripts::setEnabled(const QString &id, bool on)
{
    if (!available())
        return QStringLiteral("KWin no responde");
    QString err = writeEnabled(id, on);
    if (!err.isEmpty())
        return err;
    err = reconfigure();
    // refresh after a short delay — KWin re-reads async
    QTimer::singleShot(600, this, &KWinScripts::refresh);
    if (!err.isEmpty())
        return err;
    return {};
}

QString KWinScripts::reconfigure()
{
    if (!Session::hasKWin())
        return QStringLiteral("KWin no responde");
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                      QStringLiteral("/KWin"),
                                                      QStringLiteral("org.kde.KWin"),
                                                      QStringLiteral("reconfigure"));
    QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 1500);
    if (reply.type() == QDBusMessage::ErrorMessage)
        return reply.errorMessage();
    return {};
}

QString KWinScripts::install(const QString &filePath)
{
    QFileInfo fi(filePath);
    if (!fi.exists())
        return QStringLiteral("archivo no existe: ") + filePath;
    const QString tool = findToolInternal(QStringLiteral("kpackagetool6"));
    if (tool.isEmpty())
        return QStringLiteral("kpackagetool6 no encontrado");

    // Try install, if already exists try upgrade
    QProcess proc;
    proc.start(tool, {QStringLiteral("--type"), QStringLiteral("KWin/Script"), QStringLiteral("-i"), filePath});
    proc.waitForFinished(8000);
    if (proc.exitCode() == 0) {
        QTimer::singleShot(600, this, &KWinScripts::refresh);
        return {};
    }
    QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    // If already installed, try upgrade
    if (err.contains(QStringLiteral("already installed"), Qt::CaseInsensitive)
        || err.contains(QStringLiteral("exists"), Qt::CaseInsensitive)) {
        QProcess proc2;
        proc2.start(tool, {QStringLiteral("--type"), QStringLiteral("KWin/Script"), QStringLiteral("-u"), filePath});
        proc2.waitForFinished(8000);
        if (proc2.exitCode() == 0) {
            QTimer::singleShot(600, this, &KWinScripts::refresh);
            return {};
        }
        QString err2 = QString::fromUtf8(proc2.readAllStandardError()).trimmed();
        return err2.isEmpty() ? err : err2;
    }
    if (err.isEmpty())
        err = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    return err.isEmpty() ? QStringLiteral("kpackagetool6 falló") : err;
}

QString KWinScripts::uninstall(const QString &id)
{
    const QString tool = findToolInternal(QStringLiteral("kpackagetool6"));
    if (tool.isEmpty())
        return QStringLiteral("kpackagetool6 no encontrado");
    QProcess proc;
    proc.start(tool, {QStringLiteral("--type"), QStringLiteral("KWin/Script"), QStringLiteral("-r"), id});
    proc.waitForFinished(5000);
    if (proc.exitCode() != 0) {
        QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return err.isEmpty() ? QStringLiteral("kpackagetool6 -r falló") : err;
    }
    QTimer::singleShot(400, this, &KWinScripts::refresh);
    return {};
}

QVariantList KWinScripts::scripts() const
{
    QVariantList out;
    // Return cached if available, else scan
    const QList<ScriptInfo> infos = m_cache.isEmpty() ? const_cast<KWinScripts *>(this)->scan() : m_cache;
    for (const ScriptInfo &s : infos) {
        QVariantMap m;
        m[QStringLiteral("id")] = s.id;
        m[QStringLiteral("name")] = s.name;
        m[QStringLiteral("description")] = s.description;
        m[QStringLiteral("version")] = s.version;
        m[QStringLiteral("api")] = s.api;
        m[QStringLiteral("path")] = s.path;
        m[QStringLiteral("enabled")] = s.enabled;
        m[QStringLiteral("loaded")] = s.loaded;
        m[QStringLiteral("userScope")] = s.userScope;
        out.append(m);
    }
    return out;
}

QList<KWinScripts::ScriptInfo> KWinScripts::scriptInfos() const
{
    return m_cache.isEmpty() ? const_cast<KWinScripts *>(this)->scan() : m_cache;
}

void KWinScripts::refresh()
{
    m_cache = scan();
    ensureWatched();
    emit changed();
}

void KWinScripts::ensureWatched()
{
    if (m_watching)
        return;
    m_watching = true;
    // Watch kwinrc dir for changes (KConfig uses atomic rename, so watch dir)
    const QString path = kwinrcPath();
    QFileInfo fi(path);
    const QString dir = fi.absolutePath();
    // Use QFileSystemWatcher implicitly via QTimer polling fallback if needed;
    // For now, simple: rely on manual refresh + debounce on next call.
    // We add a real watcher if Qt can watch the file.
    auto *watcher = new QFileSystemWatcher(this);
    if (QFileInfo::exists(path))
        watcher->addPath(path);
    if (QFileInfo::exists(dir))
        watcher->addPath(dir);
    connect(watcher, &QFileSystemWatcher::fileChanged, this, [this, watcher, path](const QString &) {
        m_debounce.start();
        // Re-add after atomic replace
        if (!watcher->files().contains(path) && QFileInfo::exists(path))
            watcher->addPath(path);
    });
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
}
