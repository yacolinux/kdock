// Clock widget with enhanced tooltip (same display as ClockWidget, but with
// a larger, styled tooltip showing time in yellow on gray background).

#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

class DesktopEntryIndex;
class QProcess;

class ClockWidget2 : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString timeString READ timeString NOTIFY changed)
    Q_PROPERTY(QString dateString READ dateString NOTIFY changed)
    Q_PROPERTY(QString popupTimeString READ popupTimeString NOTIFY changed)
    Q_PROPERTY(QString popupDateString READ popupDateString NOTIFY changed)
    Q_PROPERTY(bool format24h READ format24h WRITE setFormat24h NOTIFY changed)
    Q_PROPERTY(bool showDate READ showDate WRITE setShowDate NOTIFY changed)
    Q_PROPERTY(bool showSeconds READ showSeconds WRITE setShowSeconds NOTIFY changed)

public:
    explicit ClockWidget2(QObject *parent = nullptr);

    QString timeString() const;
    QString dateString() const;
    QString popupTimeString() const;
    QString popupDateString() const;
    bool format24h() const { return m_format24h; }
    bool showDate() const { return m_showDate; }
    bool showSeconds() const { return m_showSeconds; }

    void setFormat24h(bool v);
    void setShowDate(bool v);
    void setShowSeconds(bool v);
    void setCommand(const QString &v) { m_command = v; }

    // App index used to resolve the command when it names a .desktop id
    // instead of a binary (see launch()). Shared instance, app-wide lifetime.
    void setApps(DesktopEntryIndex *apps) { m_apps = apps; }

    // Toggle the configured app: the first click launches it, the second click
    // closes the instance this widget started (see setCommand). No-op if empty.
    Q_INVOKABLE void launch();

signals:
    void changed();

private:
    void refresh();
    // Starts the program as a tracked child so the next click can close it.
    void startTracked(const QString &program, const QStringList &args);
    // Same, but parses a .desktop Exec= line (and strips %-field codes).
    void startTrackedExec(const QString &exec);

    QString m_command;
    DesktopEntryIndex *m_apps = nullptr;
    QProcess *m_proc = nullptr;   // the instance this widget launched, if alive
    bool m_format24h = true;
    bool m_showDate = false;
    bool m_showSeconds = false;
    QString m_timeString;
    QString m_dateString;
    QString m_popupTimeString;
    QString m_popupDateString;
    QTimer m_timer;
};
