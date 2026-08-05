#include "darkmodeappearance.h"

#include "appearancecontrol.h"
#include "dockconfig.h"
#include "theme.h"

#include <utility>

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
    // Going dark: remember what the desktop had, *before* overwriting it. That
    // snapshot is what an empty "normal" value restores on the way back — the
    // dialog calls it "(volver al anterior)". Seeding the dialog's combo from
    // the live system used to stand in for this, but it only ran when the user
    // happened to open that tab with the mode off, and it stored an empty
    // string for years because currentColorScheme() read the wrong key: leaving
    // the desktop stuck on the dark scheme after switching back (2026-08-05).
    if (dark && m_appearance) {
        for (auto [item, live] :
             {std::pair{int(DockConfig::SystemColorScheme), m_appearance->currentColorScheme()},
              std::pair{int(DockConfig::SystemIconTheme), m_appearance->currentIconTheme()}}) {
            if (DockConfig::darkAppearanceEnabled(item) && !live.isEmpty())
                DockConfig::setDarkAppearancePrevious(item, live);
        }
    }

    const auto valueFor = [dark](int item) {
        if (!DockConfig::darkAppearanceEnabled(item))
            return QString();
        const QString configured = DockConfig::darkAppearanceValue(item, dark);
        // On the way back an empty value means "whatever was there before", not
        // "do nothing". Empty on the dark side does mean "do nothing", and so
        // does an empty snapshot (nothing was ever captured).
        if (!dark && configured.isEmpty())
            return DockConfig::darkAppearancePrevious(item);
        return configured;
    };

    if (m_appearance) {
        // Both are no-ops on an empty id — better to leave the desktop alone
        // than to apply a guess.
        m_appearance->applyColorScheme(valueFor(DockConfig::SystemColorScheme));
        m_appearance->applyIconTheme(valueFor(DockConfig::SystemIconTheme));
    }
    if (m_theme && DockConfig::darkAppearanceEnabled(DockConfig::DockIconTheme)) {
        // Unlike the two above, an empty id is meaningful here: it clears the
        // override so the dock follows the KDE icon theme again.
        m_theme->setIconTheme(DockConfig::darkAppearanceValue(DockConfig::DockIconTheme, dark));
    }
}
