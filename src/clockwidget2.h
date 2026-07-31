// Clock widget with enhanced tooltip (same display as ClockWidget, but with
// a larger, styled tooltip showing time in yellow on gray background).

#pragma once

#include <QObject>
#include <QTimer>

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

    // Launch the configured command (see setCommand). No-op if empty.
    Q_INVOKABLE void launch() const;

signals:
    void changed();

private:
    void refresh();

    QString m_command;
    bool m_format24h = true;
    bool m_showDate = false;
    bool m_showSeconds = false;
    QString m_timeString;
    QString m_dateString;
    QString m_popupTimeString;
    QString m_popupDateString;
    QTimer m_timer;
};
