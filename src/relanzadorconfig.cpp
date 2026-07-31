#include "relanzadorconfig.h"

RelanzadorConfig::RelanzadorConfig(const QString &id, int nestingLevel, QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_nestingLevel(nestingLevel)
    , m_settings(settings)
{
    const QString prefix = QStringLiteral("relanzadores/%1/").arg(m_id);
    m_title = m_settings->value(prefix + QStringLiteral("title"), m_id).toString();
    m_iconName = m_settings->value(prefix + QStringLiteral("iconName"), QStringLiteral("folder")).toString();
    m_pinned = m_settings->value(prefix + QStringLiteral("pinned")).toStringList();
    m_showVolume = m_settings->value(prefix + QStringLiteral("showVolume"), false).toBool();
    m_showBrightness = m_settings->value(prefix + QStringLiteral("showBrightness"), false).toBool();
    m_showClock = m_settings->value(prefix + QStringLiteral("showClock"), false).toBool();
    m_showAutohide = m_settings->value(prefix + QStringLiteral("showAutohide"), false).toBool();
    m_showSystray = m_settings->value(prefix + QStringLiteral("showSystray"), false).toBool();
    m_hasSubRelanzador = m_settings->value(prefix + QStringLiteral("hasSubRelanzador"), false).toBool();
    m_subRelanzadorId = m_settings->value(prefix + QStringLiteral("subRelanzadorId")).toString();
}

void RelanzadorConfig::setTitle(const QString &title)
{
    if (m_title == title)
        return;
    m_title = title;
    save();
    emit titleChanged();
}

void RelanzadorConfig::setIconName(const QString &name)
{
    if (m_iconName == name)
        return;
    m_iconName = name;
    save();
    emit iconNameChanged();
}

void RelanzadorConfig::setPinned(const QStringList &pinned)
{
    if (m_pinned == pinned)
        return;
    m_pinned = pinned;
    save();
    emit pinnedChanged();
}

void RelanzadorConfig::setShowVolume(bool show)
{
    if (m_showVolume == show)
        return;
    m_showVolume = show;
    save();
    emit showVolumeChanged();
}

void RelanzadorConfig::setShowBrightness(bool show)
{
    if (m_showBrightness == show)
        return;
    m_showBrightness = show;
    save();
    emit showBrightnessChanged();
}

void RelanzadorConfig::setShowClock(bool show)
{
    if (m_showClock == show)
        return;
    m_showClock = show;
    save();
    emit showClockChanged();
}

void RelanzadorConfig::setShowAutohide(bool show)
{
    if (m_showAutohide == show)
        return;
    m_showAutohide = show;
    save();
    emit showAutohideChanged();
}

void RelanzadorConfig::setShowSystray(bool show)
{
    if (m_showSystray == show)
        return;
    m_showSystray = show;
    save();
    emit showSystrayChanged();
}

void RelanzadorConfig::setHasSubRelanzador(bool has)
{
    if (m_hasSubRelanzador == has)
        return;
    m_hasSubRelanzador = has;
    save();
    emit hasSubRelanzadorChanged();
}

void RelanzadorConfig::setSubRelanzadorId(const QString &id)
{
    if (m_subRelanzadorId == id)
        return;
    m_subRelanzadorId = id;
    save();
    emit subRelanzadorIdChanged();
}

void RelanzadorConfig::save()
{
    const QString prefix = QStringLiteral("relanzadores/%1/").arg(m_id);
    m_settings->setValue(prefix + QStringLiteral("title"), m_title);
    m_settings->setValue(prefix + QStringLiteral("iconName"), m_iconName);
    m_settings->setValue(prefix + QStringLiteral("pinned"), m_pinned);
    m_settings->setValue(prefix + QStringLiteral("showVolume"), m_showVolume);
    m_settings->setValue(prefix + QStringLiteral("showBrightness"), m_showBrightness);
    m_settings->setValue(prefix + QStringLiteral("showClock"), m_showClock);
    m_settings->setValue(prefix + QStringLiteral("showAutohide"), m_showAutohide);
    m_settings->setValue(prefix + QStringLiteral("showSystray"), m_showSystray);
    m_settings->setValue(prefix + QStringLiteral("hasSubRelanzador"), m_hasSubRelanzador);
    m_settings->setValue(prefix + QStringLiteral("subRelanzadorId"), m_subRelanzadorId);
}