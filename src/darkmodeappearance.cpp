#include "darkmodeappearance.h"

#include "appearancecontrol.h"
#include "autocolorscheme.h"
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
    //
    // It is also the source of truth that makes re-entry a no-op: while it says
    // the mode is on, nothing here runs, so more clicks on "go dark" can never
    // touch what the normal mode restores. That value is the user's to change,
    // from Settings -> DarkMode.
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
        // While ColorAuto owns the system, the live color scheme is one *kdock*
        // generated from the wallpaper, not the user's. Take ColorAuto's own
        // saved default instead, or leaving dark mode restores a throwaway
        // scheme — whose file is deleted on the next wallpaper change, so there
        // is no way back at all. Same failure the darkAppearanceSelfApplied()
        // guard below exists for. Only the color scheme needs this: ColorAuto's
        // icon sets go to kdock's own override (Theme::setIconTheme), never to
        // the KDE icon theme this loop snapshots.
        const QString liveColors = AutoColorScheme::applied()
                                       ? AutoColorScheme::userColorScheme()
                                       : m_appearance->currentColorScheme();
        for (auto [item, live] :
             {std::pair{int(DockConfig::SystemColorScheme), liveColors},
              std::pair{int(DockConfig::SystemIconTheme), m_appearance->currentIconTheme()}}) {
            // Only a value the *user* put there is worth remembering. The tools
            // that apply these are startDetached (~900 ms), so leaving the mode
            // and coming back right away reads a system that still holds the
            // dark id kdock just wrote — and storing that as "what was there
            // before" makes leaving dark mode restore dark, forever. That is
            // the bug this guard exists for (2026-08-10).
            if (live.isEmpty() || live == DockConfig::darkAppearanceSelfApplied(item)
                || live == DockConfig::darkAppearanceValue(item, true))
                continue;
            // Not gated on darkAppearanceEnabled(): the snapshot is free
            // bookkeeping, and without it turning an item on *while already
            // dark* would leave nothing to restore on the way back. What the
            // flag governs is what gets applied, in valueFor() below.
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
        // than to apply a guess. Whatever does go out is recorded as ours, so
        // the snapshot above can tell it apart from a choice of the user's.
        const QString colors = valueFor(DockConfig::SystemColorScheme);
        const QString icons = valueFor(DockConfig::SystemIconTheme);
        m_appearance->applyColorScheme(colors);
        m_appearance->applyIconTheme(icons);
        if (!colors.isEmpty())
            DockConfig::setDarkAppearanceSelfApplied(DockConfig::SystemColorScheme, colors);
        if (!icons.isEmpty())
            DockConfig::setDarkAppearanceSelfApplied(DockConfig::SystemIconTheme, icons);
    }
    if (m_theme && DockConfig::darkAppearanceEnabled(DockConfig::DockIconTheme)) {
        // Unlike the two above, an empty id is meaningful here: it clears the
        // override so the dock follows the KDE icon theme again.
        m_theme->setIconTheme(DockConfig::darkAppearanceValue(DockConfig::DockIconTheme, dark));
    }
}
