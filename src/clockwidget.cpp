#include "clockwidget.h"

#include <QDateTime>

ClockWidget::ClockWidget(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &ClockWidget::refresh);
    m_timer.start(1000);
    refresh();
}

QString ClockWidget::timeString() const
{
    return m_timeString;
}

QString ClockWidget::dateString() const
{
    return m_dateString;
}

void ClockWidget::setFormat24h(bool v)
{
    if (m_format24h == v)
        return;
    m_format24h = v;
    refresh();
    emit changed();
}

void ClockWidget::setShowDate(bool v)
{
    if (m_showDate == v)
        return;
    m_showDate = v;
    refresh();
    emit changed();
}

void ClockWidget::setShowSeconds(bool v)
{
    if (m_showSeconds == v)
        return;
    m_showSeconds = v;
    refresh();
    emit changed();
}

void ClockWidget::refresh()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QString timeFmt = m_format24h
        ? (m_showSeconds ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm"))
        : (m_showSeconds ? QStringLiteral("h:mm:ss AP") : QStringLiteral("h:mm AP"));
    m_timeString = now.toString(timeFmt);

    if (m_showDate)
        m_dateString = now.toString(QStringLiteral("ddd d MMM"));
    else
        m_dateString.clear();

    emit changed();
}
