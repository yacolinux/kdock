// Standard Qt Widgets configuration dialog (shown as a regular xdg-shell
// window, keyboard-interactive).

#pragma once

#include <QColor>
#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

#include <functional>

class DockConfig;
class DockManager;
class DesktopEntryIndex;
class SystrayHost;
class RelanzadoresManager;
class ScriptRunnersManager;
class AudioControl;
class BrightnessControl;
class ScreenBrightness;
class BatteryControl;
class AppearanceControl;
class ColoredTabWidget;
class IconColorProvider;
class PreviewsLauncher;
class WeatherConfig;
class TileMenuLauncher;
class ControlManagerLauncher;
class Theme;
class ThemePickerButton;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QVBoxLayout;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    SettingsDialog(DockConfig *config, DesktopEntryIndex *apps, SystrayHost *systray = nullptr,
                   RelanzadoresManager *relanzadores = nullptr, DockManager *manager = nullptr,
                   Theme *theme = nullptr, AudioControl *audio = nullptr,
                   AppearanceControl *appearance = nullptr, QWidget *parent = nullptr);

    // Switch the dialog to the Audio tab (used by the volume widget's
    // right-click). No-op when the tab isn't present.
    void showAudioTab();
    // Same for the Redes tab (network widget's right-click).
    void showNetworkTab();
    // Same for the VideoEnergía tab (brightness widget's right-click).
    void showVideoTab();
    // Same for the Docks tab, also selecting the row of the given dockId
    // (used by the dock's right-click → Dock → Nombre).
    void showMonitorsTab(const QString &dockId);
    // Same for the Traducciones tab. A language change rebuilds the dialog
    // (see DockWindow::retranslate), which reopens it here.
    void showTranslationsTab();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *createGeneralTab();
    // Look & feel hub: every icon-set / color option of the dock, copied
    // (synced) from the tabs where they originally live. Nothing is removed
    // from those tabs; each duplicated control re-reads the config on the
    // corresponding *Changed signal, so both copies stay in step.
    QWidget *createColoresTab();
    // Every font size option of the dock, copied (synced) from the Widgets tab.
    QWidget *createFuentesTab();
    QWidget *createWidgetsTab();
    QWidget *createMenuTab();
    QWidget *createTileMenuGroup(QWidget *parent);
    // Same shape, in the Widgets tab: kdock only owns the widget, everything
    // about the panel lives in its own settings dialog.
    QWidget *createControlManagerGroup(QWidget *parent);
    QWidget *createAudioTab();
    // Brightness of every detected monitor plus the power profile. Not
    // per-dock: the machine's screens look the same whichever dock is selected.
    // This is where the dock's brightness widget sends its right click, and the
    // only place the monitors it does *not* drive can be dimmed.
    QWidget *createVideoTab();
    void rebuildVideoTab();
    void scheduleVideoRebuild();
    // One brightness row (icon + label + slider + %). `setter` gets 0..1.
    QWidget *makeBrightnessRow(QWidget *parent, const QString &label, qreal value,
                               std::function<void(qreal)> setter);
    // Devices + connections editor for NetworkManager. Not per-dock: the
    // machine's networks look the same whichever dock is selected.
    QWidget *createNetworkTab();
    // Repopulate the Audio tab's device rows from the current AudioControl state.
    void rebuildAudioTab();
    // Queue a rebuild for the next event-loop turn (coalesced). Never call
    // rebuildAudioTab() straight from a row widget's own signal — it would
    // delete that widget mid-emission and crash.
    void scheduleAudioRebuild();
    // Dark mode: the per-dock switch, the app-wide one with its exception list,
    // and the two colors of the dark scheme (accent + background).
    QWidget *createDarkModeTab();
    // Swatch button + "Breeze Dark" reset idiom shared by the DarkMode and
    // Colores tabs. Returns the refresh lambda so the caller can re-run it on
    // DockConfig::darkModeChanged (both colors are app-wide statics).
    std::function<void()> makeColorButton(QPushButton *btn, QColor (*get)(),
                                          void (*set)(const QColor &),
                                          const QString &title);
    // The dock's own icon-theme override (Theme::setIconTheme), which lives in
    // both General ("Icon theme") and Colores ("Iconset del dock") as synced
    // copies: the picker re-reads itself on Theme::changed, so editing either
    // one shows in the other.
    ThemePickerButton *makeDockIconThemePicker(QWidget *parent);
    // One of the two widget icon sets (light / dark dock background), likewise
    // duplicated in General and Colores.
    ThemePickerButton *makeWidgetIconSetPicker(QWidget *parent, bool darkBg);
    // One "when dark mode is on, also change…" row (system scheme, system icon
    // set, dock icon set), shared by the DarkMode and Colores tabs. Re-selects
    // itself from the config on darkModeChanged. "kind" picks the list
    // ("icons"/"colors"); the two "special" texts are the leading empty-id entry
    // of each selector, and they do NOT mean the same thing: on the dark side
    // empty is "do not touch it", on the normal side it is "put back whatever
    // was there" (DockConfig::darkAppearancePrevious).
    void addDarkAppearanceExtrasRow(QFormLayout *form, QWidget *parent, int item,
                                    const QString &title, const QString &tip, const QString &kind,
                                    const QString &specialDark, const QString &specialNormal);
    // The three rows above together (system scheme, system icon set, dock icon
    // set), built from AppearanceControl/Theme. Shared by the DarkMode and
    // Colores tabs.
    void addDarkAppearanceExtras(QFormLayout *form, QWidget *parent);
    // ColorAuto: the color scheme generated from the wallpaper (AutoColorScheme).
    // Not per-dock — every key it edits is an app-wide static, so the tab looks
    // the same whichever dock opened the dialog.
    QWidget *createColorAutoTab();
    // One "icon set for light / for dark schemes" row of that tab: checkbox plus
    // a PickValue picker, disabled together. Same shape as
    // addDarkAppearanceExtrasRow(), one value instead of two.
    void addColorAutoIconsetRow(QFormLayout *form, QWidget *parent, bool dark,
                                const QString &title, const QString &tip);
    // Repaint the "saved as default" line and the enabled/disabled state of the
    // whole tab. No-op when the tab isn't built.
    void reloadColorAutoDefaults();
    QWidget *createLayoutTab();
    QWidget *createRelanzadoresTab();
    QWidget *createScriptRunnersTab();
    QWidget *createBackupTab();
    QWidget *createMonitorsTab();
    // Wallpapers per virtual desktop. Not per-dock: it drives the whole
    // session's containments, so the tab looks the same whichever dock is
    // selected. Desktop 1 is read-only there — it belongs to KDE.
    QWidget *createWallpapersTab();
    // Repaint the read-only "what KDE has on desktop 1" list from the stored
    // snapshot. No-op when the tab isn't built.
    void reloadWallpaperSnapshot();
    // Toggle + "Configurar" for the accessory previews binary. Only added when
    // kdock-previews is actually installed (see PreviewsLauncher).
    QWidget *createPreviewsTab();
    // Language of the whole interface: lists the .md translation layers found in
    // ~/.local/share/kdock/translations and edits them with the default editor.
    // Not per-dock — the setting lives in the shared kdock.conf.
    QWidget *createTranslationsTab();
    void reloadTranslationsList();
    // (Re)populate the tabs for the currently selected monitor's config.
    void buildTabs();
    // Tint every tab so they can be told apart at a glance. The colors are the
    // dominant colors of the dock's own app icons (same computation as the
    // running-app background in Dock.qml), so which tab gets which color is not
    // deterministic — it follows whatever is pinned/running.
    void applyTabColors();
    QList<QColor> tabPalette(int count) const;
    // Switch the dialog to edit another dock's config.
    void selectDock(const QString &dockId);
    // Recompute the current dockId from the monitor + slot combos and select it.
    void selectFromCombos();
    void updateEnabledCheck();
    // Second row of the top bar: which virtual desktop the dock being edited
    // belongs to, and its name from the Docks tab. Both only tell the user
    // *which* dock this is — the bindings themselves are edited in that tab —
    // so this has to be re-run whenever the selected dock, its alias or its
    // desktops change.
    void reloadDockHeader();
    // The desktop binding of a dock as one combo entry: its label and the
    // (opaque) key that identifies the group of docks sharing it.
    QString desktopBindingLabel(const QList<int> &desktops) const;
    static QString desktopBindingKey(const QList<int> &desktops);
    void reloadLayoutList();
    // Second list of the Layout tab: the pinned launchers with the two static
    // separators of the apps block (separator1/separator2) drawn where they
    // land, so they are placed by moving a row instead of typing an index.
    void reloadAppSeparatorList();
    // Position of separator <which> (1 or 2), or -1 when it is off.
    int appSeparatorPos(int which) const;
    void setAppSeparatorPos(int which, int pos);
    // Whether separator <which> keeps its room but draws no line.
    bool appSeparatorTransparent(int which) const;
    void setAppSeparatorTransparent(int which, bool on);
    static QString sectionLabel(const QString &token);
    // How a dock is named to the user: "<monitor> — Dock <n>", plus ": <alias>"
    // when it has one. Shared by the Docks tab list, the top bar and the
    // systray "already taken by" note.
    static QString dockLabel(const QString &dockId);
    // The two "hide what is not plugged in" checkboxes of the Docks tab,
    // persisted in the shared kdock.conf (both default to hiding). Kept out of
    // DockConfig on purpose: they are a property of the dialog, not of a dock.
    static constexpr const char *kHideOfflineDocksKey = "ui/hideOfflineDocks";
    static constexpr const char *kHideOfflineMonitorsKey = "ui/hideOfflineMonitors";
    static bool hideOfflinePref(const char *key);
    static void setHideOfflinePref(const char *key, bool on);
    void reloadPinnedList();
    void savePinnedList();
    void addPinnedApp();
    void reloadFavoritesList();
    void saveFavoritesList();
    void reloadRelanzadoresList();
    void reloadRelanzadorApps();
    void reloadScriptRunnersList();
    void reloadScriptRunnerEditor();
    void reloadDocksList();
    void reloadMonitorsForSelectedDock();
    void reloadDesktopsForSelectedDock();

    DockConfig *m_config;
    DesktopEntryIndex *m_apps;
    RelanzadoresManager *m_relanzadores;
    ScriptRunnersManager *m_scriptRunners = nullptr;
    AudioControl *m_audio = nullptr;
    // Pulled from the manager (not from the constructor, which the probes call
    // with a null one): the three backends of the VideoEnergía tab.
    BrightnessControl *m_brightness = nullptr;
    ScreenBrightness *m_screens = nullptr;
    BatteryControl *m_battery = nullptr;
    AppearanceControl *m_appearance = nullptr;
    Theme *m_theme = nullptr;
    DockManager *m_manager = nullptr;
    QString m_dockId;                       // dock currently being edited
    QComboBox *m_monitorSelector = nullptr;
    QComboBox *m_slotSelector = nullptr;
    QComboBox *m_desktopSelector = nullptr;
    QLabel *m_dockNameLabel = nullptr;
    QCheckBox *m_enabledCheck = nullptr;
    QComboBox *m_edge;
    QComboBox *m_alignment;
    QSpinBox *m_iconSize;
    QSpinBox *m_spacing;
    QSpinBox *m_margin;
    QComboBox *m_hideMode;
    QSlider *m_opacity;
    QLabel *m_alignmentNote;
    QSpinBox *m_dockLength;
    QListWidget *m_pinnedList;
    QListWidget *m_favoritesList = nullptr;
    QListWidget *m_layoutList;
    QListWidget *m_appSepList = nullptr;
    // Which apps-block separator (1, 2 or 0 for none) the second Layout list
    // has selected. Every edit rebuilds that list, and the row a separator sits
    // on changes as it moves, so the selection is tracked by separator instead
    // of by row: without this Up/Down deselect what they just moved.
    int m_appSepSelected = 0;
    QCheckBox *m_appSepTransparent = nullptr;
    QListWidget *m_relanzadoresList;
    QListWidget *m_relanzadorAppsList;
    QListWidget *m_scriptRunnersList = nullptr;
    QLineEdit *m_scriptRunnerTitle = nullptr;
    QPushButton *m_scriptRunnerIconButton = nullptr;
    QLineEdit *m_scriptRunnerPath = nullptr;
    QPushButton *m_scriptRunnerBrowse = nullptr;
    ColoredTabWidget *m_tabWidget;
    IconColorProvider *m_iconColors = nullptr;
    // Audio tab: the three device-section containers are repopulated live from
    // AudioControl::changed(); m_audioSliderDown suppresses the rebuild while a
    // volume slider is being dragged so the handle doesn't jump.
    int m_audioTabIndex = -1;
    int m_networkTabIndex = -1;
    int m_translationsTabIndex = -1;
    QListWidget *m_translationsList = nullptr;
    int m_monitorsTabIndex = -1;
    QGroupBox *m_audioOutGroup = nullptr;
    QGroupBox *m_audioInGroup = nullptr;
    QGroupBox *m_audioAppGroup = nullptr;
    QVBoxLayout *m_audioOutLayout = nullptr;
    QVBoxLayout *m_audioInLayout = nullptr;
    QVBoxLayout *m_audioAppLayout = nullptr;
    bool m_audioSliderDown = false;
    bool m_audioRebuildQueued = false;
    // VideoEnergía tab, same shape as Audio: the brightness rows are rebuilt
    // from ScreenBrightness::changed (monitors come and go, and KDE moves the
    // values behind our back), and m_videoSliderDown suppresses that while a
    // slider is under the pointer.
    int m_videoTabIndex = -1;
    QVBoxLayout *m_videoBrightnessLayout = nullptr;
    QComboBox *m_videoWheelTarget = nullptr;
    QGroupBox *m_videoPowerGroup = nullptr;
    QVBoxLayout *m_videoPowerLayout = nullptr;
    QWidget *m_videoAllButtons = nullptr;
    bool m_videoSliderDown = false;
    bool m_videoRebuildQueued = false;
    QString m_selectedRelanzadorId;
    QString m_selectedScriptRunnerId;
    QListWidget *m_docksList = nullptr;
    QCheckBox *m_hideOfflineDocks = nullptr;
    QListWidget *m_monitorsList = nullptr;
    QCheckBox *m_hideOfflineMonitors = nullptr;
    // Virtual desktops the selected dock is bound to (no box checked = base
    // dock, shown wherever its monitor has no desktop-specific dock).
    QListWidget *m_desktopsList = nullptr;
    QPushButton *m_duplicateForDesktopButton = nullptr;
    QLabel *m_desktopsNote = nullptr;
    QPushButton *m_applyPreviewButton = nullptr;
    QPushButton *m_deleteDockButton = nullptr;
    QString m_selectedTabDockId;
    void updateMonitorsTabButtons();
    // Previews tab. The launcher outlives the tab widgets (buildTabs() recreates
    // them whenever another dock is selected).
    PreviewsLauncher *m_previewsLauncher = nullptr;
    // Owned by the dialog: the Fuentes tab edits the weather window's font
    // size, and going through the config object (not raw QSettings) is what
    // makes a running kdock-weather notice — its config watches the file.
    WeatherConfig *m_weatherConfig = nullptr;
    TileMenuLauncher *m_tileLauncher = nullptr;
    ControlManagerLauncher *m_cmLauncher = nullptr;
    QCheckBox *m_previewsEnabled = nullptr;
    QLabel *m_previewsStatus = nullptr;

    // Wallpapers tab: the read-only view of the stored desktop-1 config, kept
    // as a member because a capture lands asynchronously.
    QListWidget *m_wallpaperSnapshotList = nullptr;

    // ColorAuto tab. Members because the saved defaults are re-read whenever the
    // feature is toggled (enabling captures them) and because everything below
    // the master switch follows its state.
    QLabel *m_colorAutoDefaults = nullptr;
    QWidget *m_colorAutoBody = nullptr;
};
