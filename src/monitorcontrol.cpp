#include "monitorcontrol.h"

#include "kwinshortcut.h"

MonitorControl::MonitorControl(QObject *parent)
    : QObject(parent)
{
    // Available whenever we're on KWin, mirroring the overview / move-to-desktop
    // widgets. (It used to also require >1 connected monitor, which hid the
    // button on single-monitor states even when the user enabled it.)
    m_available = KWinShortcut::available();
}

void MonitorControl::moveToNextScreen()
{
    if (!m_available)
        return;

    // KWin registers the "Window to Next Screen" global shortcut, which moves
    // the active window to the next monitor. Invoking it via kglobalaccel keeps
    // this consistent with the overview/next-desktop widgets.
    KWinShortcut::invoke(QStringLiteral("Window to Next Screen"));
}

void MonitorControl::moveToPreviousScreen()
{
    if (!m_available)
        return;

    // The symmetric shortcut, bound to the widget's right click. On a
    // two-monitor session both directions land on the same screen; it only
    // starts to matter from three onwards.
    KWinShortcut::invoke(QStringLiteral("Window to Previous Screen"));
}
