#include "clockwidget2.h"

#include <QDateTime>
#include <QProcess>

ClockWidget2::ClockWidget2(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &ClockWidget2::refresh);
    m_timer.start(1000);
    refresh();
}

void ClockWidget2::refresh()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QString timeFormat = m_format24h
        ? (m_showSeconds ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm"))
        : (m_showSeconds ? QStringLiteral("h:mm:ss AP") : QStringLiteral("h:mm AP"));
    const QString newTime = now.toString(timeFormat);
    const QString newDate = now.toString(QStringLiteral("ddd d MMM"));
    const QString newPopupTime = now.toString(QStringLiteral("HH:mm"));
    const QString newPopupDate = now.toString(QStringLiteral("dddd, d MMMM yyyy"));

    if (m_timeString != newTime || m_dateString != newDate
        || m_popupTimeString != newPopupTime || m_popupDateString != newPopupDate) {
        m_timeString = newTime;
        m_dateString = newDate;
        m_popupTimeString = newPopupTime;
        m_popupDateString = newPopupDate;
        emit changed();
    }
}

QString ClockWidget2::popupTimeString() const
{
    return m_popupTimeString;
}

QString ClockWidget2::popupDateString() const
{
    return m_popupDateString;
}

QString ClockWidget2::timeString() const
{
    return m_timeString;
}

QString ClockWidget2::dateString() const
{
    return m_dateString;
}

void ClockWidget2::launch() const
{
    if (m_command.isEmpty())
        return;
    QStringList parts = QProcess::splitCommand(m_command);
    if (parts.isEmpty())
        return;
    const QString program = parts.takeFirst();
    QProcess::startDetached(program, parts);
}

void ClockWidget2::setFormat24h(bool v)
{
    if (m_format24h == v)
        return;
    m_format24h = v;
    refresh();
}

void ClockWidget2::setShowDate(bool v)
{
    if (m_showDate == v)
        return;
    m_showDate = v;
    refresh();
}

void ClockWidget2::setShowSeconds(bool v)
{
    if (m_showSeconds == v)
        return;
    m_showSeconds = v;
    refresh();
}
