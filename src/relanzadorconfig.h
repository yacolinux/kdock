// Configuration for a single Relanzador (nested dock widget).
// Persisted in QSettings under relanzadores/<id>/.

#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>

class RelanzadorConfig : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QString iconName READ iconName WRITE setIconName NOTIFY iconNameChanged)
    Q_PROPERTY(QStringList pinned READ pinned WRITE setPinned NOTIFY pinnedChanged)
    Q_PROPERTY(bool showVolume READ showVolume WRITE setShowVolume NOTIFY showVolumeChanged)
    Q_PROPERTY(bool showBrightness READ showBrightness WRITE setShowBrightness NOTIFY showBrightnessChanged)
    Q_PROPERTY(bool showClock READ showClock WRITE setShowClock NOTIFY showClockChanged)
    Q_PROPERTY(bool showAutohide READ showAutohide WRITE setShowAutohide NOTIFY showAutohideChanged)
    Q_PROPERTY(bool showSystray READ showSystray WRITE setShowSystray NOTIFY showSystrayChanged)
    Q_PROPERTY(bool hasSubRelanzador READ hasSubRelanzador WRITE setHasSubRelanzador NOTIFY hasSubRelanzadorChanged)
    Q_PROPERTY(QString subRelanzadorId READ subRelanzadorId WRITE setSubRelanzadorId NOTIFY subRelanzadorIdChanged)
    Q_PROPERTY(int nestingLevel READ nestingLevel CONSTANT)

public:
    explicit RelanzadorConfig(const QString &id, int nestingLevel, QSettings *settings, QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString title() const { return m_title; }
    QString iconName() const { return m_iconName; }
    QStringList pinned() const { return m_pinned; }
    bool showVolume() const { return m_showVolume; }
    bool showBrightness() const { return m_showBrightness; }
    bool showClock() const { return m_showClock; }
    bool showAutohide() const { return m_showAutohide; }
    bool showSystray() const { return m_showSystray; }
    bool hasSubRelanzador() const { return m_hasSubRelanzador; }
    QString subRelanzadorId() const { return m_subRelanzadorId; }
    int nestingLevel() const { return m_nestingLevel; }

    void setTitle(const QString &title);
    void setIconName(const QString &name);
    void setPinned(const QStringList &pinned);
    void setShowVolume(bool show);
    void setShowBrightness(bool show);
    void setShowClock(bool show);
    void setShowAutohide(bool show);
    void setShowSystray(bool show);
    void setHasSubRelanzador(bool has);
    void setSubRelanzadorId(const QString &id);

signals:
    void titleChanged();
    void iconNameChanged();
    void pinnedChanged();
    void showVolumeChanged();
    void showBrightnessChanged();
    void showClockChanged();
    void showAutohideChanged();
    void showSystrayChanged();
    void hasSubRelanzadorChanged();
    void subRelanzadorIdChanged();

private:
    void save();

    QString m_id;
    int m_nestingLevel;
    QSettings *m_settings;
    QString m_title;
    QString m_iconName;
    QStringList m_pinned;
    bool m_showVolume = false;
    bool m_showBrightness = false;
    bool m_showClock = false;
    bool m_showAutohide = false;
    bool m_showSystray = false;
    bool m_hasSubRelanzador = false;
    QString m_subRelanzadorId;
};