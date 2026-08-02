// Applies the optional *system-wide* side effects of dark mode: the KDE color
// scheme, the KDE icon theme and kdock's own icon-theme override.
//
// These are not like the dock's own colors. Those are overridden at read time,
// so leaving dark mode restores them for free; a KDE color scheme is global
// state that has to be written and written back. So each item carries a value
// for both modes (configured in Settings → DarkMode) and switching applies the
// matching one — no snapshot to go stale, and nothing to guess when the mode is
// turned off from a session that never turned it on.

#pragma once

#include <QObject>

class AppearanceControl;
class Theme;

class DarkModeAppearance : public QObject
{
    Q_OBJECT
public:
    DarkModeAppearance(Theme *theme, AppearanceControl *appearance, QObject *parent = nullptr);

    // Apply the enabled items for the mode the docks are in now, if that is not
    // the mode already applied. Runs on every dark-mode ping, so it has to be
    // cheap and idempotent: the accent color emits the same signal. Public so
    // startup can call it once the docks exist.
    void sync();

private:
    void apply(bool dark);

    Theme *m_theme = nullptr;
    AppearanceControl *m_appearance = nullptr;
};
