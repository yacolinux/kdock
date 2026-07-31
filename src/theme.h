// Reads KDE color scheme + icon theme straight from ~/.config/kdeglobals
// (plain INI, XDG config dir). No KDE libraries; falls back to QPalette
// when the file is absent, and live-reloads when it changes.

#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>

class Theme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QColor highlight READ highlight NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit Theme(QObject *parent = nullptr);

    QColor background() const { return m_background; }
    QColor foreground() const { return m_foreground; }
    QColor highlight() const { return m_highlight; }
    int revision() const { return m_revision; }

    // Global icon-theme override (process-wide QIcon theme). Empty = follow the
    // KDE kdeglobals Icons/Theme. Persisted in the shared kdock.conf.
    QString iconTheme() const { return m_iconThemeOverride; }
    Q_INVOKABLE void setIconTheme(const QString &id);

    // (displayName, id) of every installed icon theme, sorted by name.
    static QList<QPair<QString, QString>> availableIconThemes();

signals:
    void changed();

private:
    void reload();
    void applyAppPalette();

    QString m_iconThemeOverride;
    QString m_kdeIconTheme; // last Icons/Theme read from kdeglobals

    QColor m_background;
    QColor m_foreground;
    QColor m_highlight;
    QColor m_view, m_viewText, m_button, m_buttonText, m_highlightedText;
    int m_revision = 0;
    QFileSystemWatcher m_watcher;
};
