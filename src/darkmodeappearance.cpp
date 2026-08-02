#include "darkmodeappearance.h"

#include "appearancecontrol.h"
#include "dockconfig.h"
#include "theme.h"

DarkModeAppearance::DarkModeAppearance(Theme *theme, AppearanceControl *appearance, QObject *parent)
    : QObject(parent)
    , m_theme(theme)
    , m_appearance(appearance)
{
    connect(DockConfig::darkModeNotifier(), &DarkModeNotifier::changed, this,
            &DarkModeAppearance::sync);
    // Not called here: at construction time no DockConfig exists yet, so
    // anyDarkModeActive() would read false and "restore" a session that is
    // meant to come up dark. The first ping arrives as soon as anything
    // touches the mode; DockManager calls sync() once the docks are up.
}

void DarkModeAppearance::sync()
{
    const bool dark = DockConfig::anyDarkModeActive();
    // The applied state is persisted, not a member: it describes the *system*,
    // which outlives this process. Without it a restart with the mode already
    // on would re-apply (harmless) and, worse, a restart with it off would skip
    // the restore and leave the desktop dark forever.
    if (dark == DockConfig::darkAppearanceApplied())
        return;
    apply(dark);
    DockConfig::setDarkAppearanceApplied(dark);
}

void DarkModeAppearance::apply(bool dark)
{
    const auto valueFor = [dark](int item) {
        return DockConfig::darkAppearanceEnabled(item)
                   ? DockConfig::darkAppearanceValue(item, dark)
                   : QString();
    };

    if (m_appearance) {
        // Both are no-ops on an empty id, which is also how a not-yet-seeded
        // "normal" value arrives — better to leave the desktop alone than to
        // apply a guess.
        m_appearance->applyColorScheme(valueFor(DockConfig::SystemColorScheme));
        m_appearance->applyIconTheme(valueFor(DockConfig::SystemIconTheme));
    }
    if (m_theme && DockConfig::darkAppearanceEnabled(DockConfig::DockIconTheme)) {
        // Unlike the two above, an empty id is meaningful here: it clears the
        // override so the dock follows the KDE icon theme again.
        m_theme->setIconTheme(DockConfig::darkAppearanceValue(DockConfig::DockIconTheme, dark));
    }
}
