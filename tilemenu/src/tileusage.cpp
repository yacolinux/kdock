#include "tileusage.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardPaths>

QString TileUsage::filePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + QStringLiteral("/kdock");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/tilemenu-usage.conf");
}

TileUsage::TileUsage(QObject *parent)
    : QObject(parent)
    , m_settings(filePath(), QSettings::IniFormat)
{
    load();
}

void TileUsage::load()
{
    const QByteArray statsBlob =
        m_settings.value(QStringLiteral("stats")).toString().toUtf8();
    const QJsonObject stats = QJsonDocument::fromJson(statsBlob).object();
    for (auto it = stats.constBegin(); it != stats.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        Stat s;
        s.count = o.value(QStringLiteral("c")).toInt();
        s.lastMs = static_cast<qint64>(o.value(QStringLiteral("t")).toDouble());
        m_stats.insert(it.key(), s);
    }

    const QByteArray recentBlob =
        m_settings.value(QStringLiteral("recent")).toString().toUtf8();
    const QJsonArray recent = QJsonDocument::fromJson(recentBlob).array();
    for (const QJsonValue &v : recent) {
        const QString id = v.toString();
        if (!id.isEmpty() && !m_recent.contains(id))
            m_recent.append(id);
    }
    while (m_recent.size() > kRecentCap)
        m_recent.removeLast();
}

void TileUsage::save()
{
    QJsonObject stats;
    for (auto it = m_stats.constBegin(); it != m_stats.constEnd(); ++it) {
        QJsonObject o;
        o.insert(QStringLiteral("c"), it.value().count);
        // qint64 milliseconds exceed int; store as double (JSON has no int64).
        o.insert(QStringLiteral("t"), static_cast<double>(it.value().lastMs));
        stats.insert(it.key(), o);
    }
    m_settings.setValue(QStringLiteral("stats"),
                        QString::fromUtf8(QJsonDocument(stats).toJson(QJsonDocument::Compact)));

    QJsonArray recent;
    for (const QString &id : m_recent)
        recent.append(id);
    m_settings.setValue(QStringLiteral("recent"),
                        QString::fromUtf8(QJsonDocument(recent).toJson(QJsonDocument::Compact)));
    m_settings.sync();
}

void TileUsage::recordLaunch(const QString &id)
{
    if (id.isEmpty())
        return;

    Stat &s = m_stats[id];
    ++s.count;
    s.lastMs = QDateTime::currentMSecsSinceEpoch();

    m_recent.removeAll(id);
    m_recent.prepend(id);
    while (m_recent.size() > kRecentCap)
        m_recent.removeLast();

    save();
    emit changed();
}

int TileUsage::count(const QString &id) const
{
    return m_stats.value(id).count;
}

qint64 TileUsage::lastMs(const QString &id) const
{
    return m_stats.value(id).lastMs;
}
