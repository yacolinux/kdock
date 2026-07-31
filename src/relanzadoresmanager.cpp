#include "relanzadoresmanager.h"
#include "relanzadorconfig.h"
#include "relanzadormodel.h"
#include "dockconfig.h"

#include <QAbstractListModel>
#include <QUuid>

RelanzadoresManager::RelanzadoresManager(DesktopEntryIndex *apps, QObject *parent)
    : QObject(parent)
    , m_settings(DockConfig::settingsFilePath(), QSettings::IniFormat)
    , m_apps(apps)
{
    load();
}

QQmlListProperty<RelanzadorConfig> RelanzadoresManager::items()
{
    return QQmlListProperty<RelanzadorConfig>(this, nullptr, itemCount, itemAt);
}

int RelanzadoresManager::count() const
{
    return m_items.size();
}

RelanzadorConfig* RelanzadoresManager::createRelanzador(const QString &title)
{
    const QString id = generateId();
    const int nestingLevel = 1; // top-level relanzadores are always level 1

    auto *config = new RelanzadorConfig(id, nestingLevel, &m_settings, this);
    config->setTitle(title);

    m_items.append(config);
    saveIds();
    emit itemsChanged();

    return config;
}

void RelanzadoresManager::removeRelanzador(const QString &id)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i)->id() == id) {
            // Check if any other relanzador references this as sub-relanzador
            for (auto *other : m_items) {
                if (other->subRelanzadorId() == id) {
                    other->setHasSubRelanzador(false);
                    other->setSubRelanzadorId(QString());
                }
            }

            // Remove from settings
            m_settings.remove(QStringLiteral("relanzadores/%1").arg(id));

            // Remove from list
            auto *config = m_items.takeAt(i);
            config->deleteLater();

            saveIds();
            emit itemsChanged();
            return;
        }
    }
}

RelanzadorConfig* RelanzadoresManager::get(const QString &id) const
{
    for (auto *config : m_items) {
        if (config->id() == id)
            return config;
    }
    return nullptr;
}

RelanzadorConfig* RelanzadoresManager::getByIndex(int index) const
{
    if (index < 0 || index >= m_items.size())
        return nullptr;
    return m_items.at(index);
}

void RelanzadoresManager::load()
{
    const QStringList ids = m_settings.value(QStringLiteral("relanzadores/ids")).toStringList();
    for (const QString &id : ids) {
        const int nestingLevel = m_settings.value(
            QStringLiteral("relanzadores/%1/nestingLevel").arg(id), 1).toInt();
        auto *config = new RelanzadorConfig(id, nestingLevel, &m_settings, this);
        m_items.append(config);
    }
}

void RelanzadoresManager::saveIds()
{
    QStringList ids;
    for (const auto *config : m_items)
        ids.append(config->id());
    m_settings.setValue(QStringLiteral("relanzadores/ids"), ids);
}

QString RelanzadoresManager::generateId()
{
    // Generate a short unique ID
    int counter = m_settings.value(QStringLiteral("relanzadores/counter"), 0).toInt();
    m_settings.setValue(QStringLiteral("relanzadores/counter"), counter + 1);
    return QStringLiteral("relanzador_%1").arg(counter);
}

qsizetype RelanzadoresManager::itemCount(QQmlListProperty<RelanzadorConfig> *prop)
{
    auto *mgr = static_cast<RelanzadoresManager*>(prop->object);
    return mgr->m_items.size();
}

RelanzadorConfig* RelanzadoresManager::itemAt(QQmlListProperty<RelanzadorConfig> *prop, qsizetype index)
{
    auto *mgr = static_cast<RelanzadoresManager*>(prop->object);
    if (index >= 0 && index < mgr->m_items.size())
        return mgr->m_items.at(index);
    return nullptr;
}

QAbstractListModel* RelanzadoresManager::getModel(const QString &id)
{
    if (m_models.contains(id))
        return m_models.value(id);

    RelanzadorConfig *cfg = get(id);
    if (!cfg)
        return nullptr;

    auto *model = new RelanzadorModel(cfg, m_apps, this);
    m_models.insert(id, model);
    return model;
}

QStringList RelanzadoresManager::ids() const
{
    QStringList result;
    for (const auto *cfg : m_items)
        result.append(cfg->id());
    return result;
}

QStringList RelanzadoresManager::visibleIds(bool primary, const QStringList &hidden,
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