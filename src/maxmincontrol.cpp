#include "maxmincontrol.h"

#include "kwinshortcut.h"

MaxMinControl::MaxMinControl(QObject *parent)
    : QObject(parent)
{
    m_available = KWinShortcut::sessionIsKde();
}

void MaxMinControl::maximize()
{
    if (!m_available)
        return;

    // Acts on whatever KWin considers active. The dock never takes that away:
    // its layer surface asks for no keyboard interactivity, so clicking a
    // widget leaves the focused window focused.
    KWinShortcut::invoke(QStringLiteral("Window Maximize"));
}

void MaxMinControl::minimize()
{
    if (!m_available)
        return;

    KWinShortcut::invoke(QStringLiteral("Window Minimize"));
}
