#include "scriptrunnerconfig.h"

ScriptRunnerConfig::ScriptRunnerConfig(const QString &id, QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_settings(settings)
{
    const QString prefix = QStringLiteral("scriptrunners/%1/").arg(m_id);
    m_title = m_settings->value(prefix + QStringLiteral("title"), m_id).toString();
    m_iconName = m_settings->value(prefix + QStringLiteral("iconName"),
                                   QStringLiteral("utilities-terminal")).toString();
    m_scriptPath = m_settings->value(prefix + QStringLiteral("scriptPath")).toString();
}

void ScriptRunnerConfig::setTitle(const QString &title)
{
    if (m_title == title)
        return;
    m_title = title;
    save();
    emit titleChanged();
}

void ScriptRunnerConfig::setIconName(const QString &name)
{
    if (m_iconName == name)
        return;
    m_iconName = name;
    save();
    emit iconNameChanged();
}

void ScriptRunnerConfig::setScriptPath(const QString &path)
{
    if (m_scriptPath == path)
        return;
    m_scriptPath = path;
    save();
    emit scriptPathChanged();
}

void ScriptRunnerConfig::save()
{
    const QString prefix = QStringLiteral("scriptrunners/%1/").arg(m_id);
    m_settings->setValue(prefix + QStringLiteral("title"), m_title);
    m_settings->setValue(prefix + QStringLiteral("iconName"), m_iconName);
    m_settings->setValue(prefix + QStringLiteral("scriptPath"), m_scriptPath);
}
