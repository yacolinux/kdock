#include "scriptrunnersmanager.h"
#include "scriptrunnerconfig.h"
#include "dockconfig.h"

#include <QDebug>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

ScriptRunnersManager::ScriptRunnersManager(QObject *parent)
    : QObject(parent)
    , m_settings(DockConfig::settingsFilePath(), QSettings::IniFormat)
{
    load();
}

QQmlListProperty<ScriptRunnerConfig> ScriptRunnersManager::items()
{
    return QQmlListProperty<ScriptRunnerConfig>(this, nullptr, itemCount, itemAt);
}

int ScriptRunnersManager::count() const
{
    return m_items.size();
}

ScriptRunnerConfig *ScriptRunnersManager::createScriptRunner(const QString &title)
{
    const QString id = generateId();
    auto *config = new ScriptRunnerConfig(id, &m_settings, this);
    config->setTitle(title);

    m_items.append(config);
    saveIds();
    emit itemsChanged();

    return config;
}

void ScriptRunnersManager::removeScriptRunner(const QString &id)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i)->id() == id) {
            m_settings.remove(QStringLiteral("scriptrunners/%1").arg(id));
            auto *config = m_items.takeAt(i);
            config->deleteLater();
            saveIds();
            emit itemsChanged();
            return;
        }
    }
}

ScriptRunnerConfig *ScriptRunnersManager::get(const QString &id) const
{
    for (auto *config : m_items) {
        if (config->id() == id)
            return config;
    }
    return nullptr;
}

ScriptRunnerConfig *ScriptRunnersManager::getByIndex(int index) const
{
    if (index < 0 || index >= m_items.size())
        return nullptr;
    return m_items.at(index);
}

QStringList ScriptRunnersManager::ids() const
{
    QStringList result;
    for (const auto *cfg : m_items)
        result.append(cfg->id());
    return result;
}

QStringList ScriptRunnersManager::visibleIds(bool primary, const QStringList &hidden,
                                             const QStringList &shown) const
{
    QStringList result;
    for (const auto *cfg : m_items) {
        const QString id = cfg->id();
        const bool visible = primary ? !hidden.contains(id) : shown.contains(id);
        if (visible)
            result.append(id);
    }
    return result;
}

void ScriptRunnersManager::run(const QString &id, const QString &screenName)
{
    ScriptRunnerConfig *cfg = get(id);
    if (!cfg)
        return;
    const QString path = cfg->scriptPath();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        qWarning() << "ScriptRunner" << id << "has no valid script file:" << path;
        return;
    }
    // Fire-and-forget: don't block the dock, don't leave zombies. If the script
    // is executable, run it directly so its shebang is honored (a #!/bin/bash
    // script must not run under dash/sh; a .py under its interpreter, etc.);
    // otherwise fall back to `sh <path>`. Export the launching dock's monitor as
    // KDOCK_SCREEN so scripts (e.g. next-wall.sh) can target it.
    QProcess process;
    if (QFileInfo(path).isExecutable()) {
        process.setProgram(path);
    } else {
        process.setProgram(QStringLiteral("sh"));
        process.setArguments({path});
    }
    if (!screenName.isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("KDOCK_SCREEN"), screenName);
        process.setProcessEnvironment(env);
    }
    if (!process.startDetached())
        qWarning() << "ScriptRunner" << id << "failed to start:" << path;
}

void ScriptRunnersManager::load()
{
    const QStringList ids = m_settings.value(QStringLiteral("scriptrunners/ids")).toStringList();
    for (const QString &id : ids) {
        auto *config = new ScriptRunnerConfig(id, &m_settings, this);
        m_items.append(config);
    }
}

void ScriptRunnersManager::saveIds()
{
    QStringList ids;
    for (const auto *config : m_items)
        ids.append(config->id());
    m_settings.setValue(QStringLiteral("scriptrunners/ids"), ids);
}

QString ScriptRunnersManager::generateId()
{
    int counter = m_settings.value(QStringLiteral("scriptrunners/counter"), 0).toInt();
    m_settings.setValue(QStringLiteral("scriptrunners/counter"), counter + 1);
    return QStringLiteral("scriptrunner_%1").arg(counter);
}

qsizetype ScriptRunnersManager::itemCount(QQmlListProperty<ScriptRunnerConfig> *prop)
{
    auto *mgr = static_cast<ScriptRunnersManager *>(prop->object);
    return mgr->m_items.size();
}

ScriptRunnerConfig *ScriptRunnersManager::itemAt(QQmlListProperty<ScriptRunnerConfig> *prop, qsizetype index)
{
    auto *mgr = static_cast<ScriptRunnersManager *>(prop->object);
    if (index >= 0 && index < mgr->m_items.size())
        return mgr->m_items.at(index);
    return nullptr;
}
