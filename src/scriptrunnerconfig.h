// Configuration for a single Script Runner (a dock button that runs a shell
// script). Persisted in QSettings under scriptrunners/<id>/.

#pragma once

#include <QObject>
#include <QSettings>

class ScriptRunnerConfig : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QString iconName READ iconName WRITE setIconName NOTIFY iconNameChanged)
    Q_PROPERTY(QString scriptPath READ scriptPath WRITE setScriptPath NOTIFY scriptPathChanged)

public:
    explicit ScriptRunnerConfig(const QString &id, QSettings *settings, QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString title() const { return m_title; }
    QString iconName() const { return m_iconName; }
    // Absolute path to the script file to run (empty until the user picks one).
    QString scriptPath() const { return m_scriptPath; }

    void setTitle(const QString &title);
    void setIconName(const QString &name);
    void setScriptPath(const QString &path);

signals:
    void titleChanged();
    void iconNameChanged();
    void scriptPathChanged();

private:
    void save();

    QString m_id;
    QSettings *m_settings;
    QString m_title;
    QString m_iconName;
    QString m_scriptPath;
};
