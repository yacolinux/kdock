#include "clockwidget2.h"

#include "desktopentry.h"

#include <QDateTime>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

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

void ClockWidget2::launch()
{
    if (m_command.trimmed().isEmpty())
        return;

    // Toggle: a second click closes the app this widget launched on the first
    // one — the whole point of pairing the clock with a calendar. terminate()
    // sends SIGTERM, which Qt apps honour by default; force-close shortly after
    // for the ones that ignore it.
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->terminate();
        QPointer<QProcess> proc = m_proc;
        QTimer::singleShot(2500, [proc] {
            if (proc && proc->state() != QProcess::NotRunning)
                proc->kill();
        });
        return;
    }

    const QStringList parts = QProcess::splitCommand(m_command.trimmed());
    if (parts.isEmpty())
        return;
    const QString program = parts.first();
    const QStringList args = parts.mid(1);

    // Resolve the program the way a user types it in Settings → Widgets →
    // "Clock click app". Four forms, checked in this order:
    //   1. a full path to a .desktop file ("/usr/share/applications/orage.desktop")
    //      — read its Exec= and launch that;
    //   2. a full path to a binary ("/usr/local/bin/kdock-calendar");
    //   3. a bare command name on PATH ("orage");
    //   4. a .desktop id ("kdock-calendar"), resolved through the app index —
    //      the hop that makes a bare id work when the binary is not on PATH or
    //      the id differs from the executable name.
    // Every path lands on startTracked() so the next click can close the app.
    if (program.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
        const DesktopEntry e = DesktopEntryIndex::fromFile(program);
        if (e.isValid())
            startTrackedExec(e.exec);
        return;
    }
    if (program.contains(QLatin1Char('/'))) {
        startTracked(program, args);
        return;
    }
    const QString onPath = QStandardPaths::findExecutable(program);
    if (!onPath.isEmpty()) {
        startTracked(onPath, args);
        return;
    }
    if (m_apps) {
        const DesktopEntry e = m_apps->byId(program);
        if (e.isValid()) {
            startTrackedExec(e.exec);
            return;
        }
    }
    // Last resort: let the shell try the bare name as-is.
    startTracked(program, args);
}

void ClockWidget2::startTracked(const QString &program, const QStringList &args)
{
    if (!m_proc)
        m_proc = new QProcess(this);
    m_proc->setProgram(program);
    m_proc->setArguments(args);
    m_proc->start();
}

void ClockWidget2::startTrackedExec(const QString &exec)
{
    QStringList parts = QProcess::splitCommand(exec);
    // Strip freedesktop Exec field codes (%f, %u, %U, ...) the same way
    // DesktopEntryIndex::launch does.
    parts.erase(std::remove_if(parts.begin(), parts.end(),
                               [](const QString &p) { return p.startsWith(QLatin1Char('%')); }),
                parts.end());
    if (parts.isEmpty())
        return;
    const QString program = parts.takeFirst();
    startTracked(program, parts);
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
