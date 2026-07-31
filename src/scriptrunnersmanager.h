// Manages the collection of Script Runners (dock buttons that run shell
// scripts). Exposed to QML as "scriptRunners". Mirrors RelanzadoresManager.

#pragma once

#include <QObject>
#include <QSettings>
#include <QQmlListProperty>
#include <QStringList>

class ScriptRunnerConfig;

class ScriptRunnersManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQmlListProperty<ScriptRunnerConfig> items READ items NOTIFY itemsChanged)
    Q_PROPERTY(int count READ count NOTIFY itemsChanged)

public:
    explicit ScriptRunnersManager(QObject *parent = nullptr);

    QQmlListProperty<ScriptRunnerConfig> items();
    int count() const;

    Q_INVOKABLE ScriptRunnerConfig *createScriptRunner(const QString &title);
    Q_INVOKABLE void removeScriptRunner(const QString &id);
    Q_INVOKABLE ScriptRunnerConfig *get(const QString &id) const;
    Q_INVOKABLE ScriptRunnerConfig *getByIndex(int index) const;
    QStringList ids() const;

    // Ids to show on a dock given its per-dock config lists. The primary dock
    // uses `hidden` (default all shown); others use `shown` (default none).
    Q_INVOKABLE QStringList visibleIds(bool primary, const QStringList &hidden,
                                       const QStringList &shown) const;

    // Run the given script runner's script via `sh -c` (fire-and-forget).
    // `screenName` (the launching dock's monitor connector) is exported as
    // KDOCK_SCREEN so scripts can target that monitor (e.g. next-wall.sh).
    Q_INVOKABLE void run(const QString &id, const QString &screenName = QString());

signals:
    void itemsChanged();

private:
    void load();
    void saveIds();
    QString generateId();

    static qsizetype itemCount(QQmlListProperty<ScriptRunnerConfig> *prop);
    static ScriptRunnerConfig *itemAt(QQmlListProperty<ScriptRunnerConfig> *prop, qsizetype index);

    QSettings m_settings;
    QList<ScriptRunnerConfig *> m_items;
};
