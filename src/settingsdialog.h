// Standard Qt Widgets configuration dialog (shown as a regular xdg-shell
// window, keyboard-interactive).

#pragma once

#include <QColor>
#include <QDialog>
#include <QList>
#include <QString>

class DockConfig;
class DockManager;
class DesktopEntryIndex;
class SystrayHost;
class RelanzadoresManager;
class ScriptRunnersManager;
class AudioControl;
class AppearanceControl;
class ColoredTabWidget;
class IconColorProvider;
class PreviewsLauncher;
class TileMenuLauncher;
class Theme;
class QButtonGroup;
class QComboBox;
class QCheckBox;
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

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *createGeneralTab();
    QWidget *createWidgetsTab();
    QWidget *createMenuTab();
    QWidget *createTileMenuGroup(QWidget *parent);
    QWidget *createAudioTab();
    // Repopulate the Audio tab's device rows from the current AudioControl state.
    void rebuildAudioTab();
    // Queue a rebuild for the next event-loop turn (coalesced). Never call
    // rebuildAudioTab() straight from a row widget's own signal — it would
    // delete that widget mid-emission and crash.
    void scheduleAudioRebuild();
    // Dark mode: the per-dock switch, the app-wide one with its exception list,
    // and the two colors of the dark scheme (accent + background).
    QWidget *createDarkModeTab();
    QWidget *createLayoutTab();
    QWidget *createRelanzadoresTab();
    QWidget *createScriptRunnersTab();
    QWidget *createBackupTab();
    QWidget *createMonitorsTab();
    // Toggle + "Configurar" for the accessory previews binary. Only added when
    // kdock-previews is actually installed (see PreviewsLauncher).
    QWidget *createPreviewsTab();
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
    void reloadLayoutList();
    // Second list of the Layout tab: the pinned launchers with the two static
    // separators of the apps block (separator1/separator2) drawn where they
    // land, so they are placed by moving a row instead of typing an index.
    void reloadAppSeparatorList();
    // Position of separator <which> (1 or 2), or -1 when it is off.
    int appSeparatorPos(int which) const;
    void setAppSeparatorPos(int which, int pos);
    static QString sectionLabel(const QString &token);
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

    DockConfig *m_config;
    DesktopEntryIndex *m_apps;
    RelanzadoresManager *m_relanzadores;
    ScriptRunnersManager *m_scriptRunners = nullptr;
    AudioControl *m_audio = nullptr;
    AppearanceControl *m_appearance = nullptr;
    Theme *m_theme = nullptr;
    DockManager *m_manager = nullptr;
    QString m_dockId;                       // dock currently being edited
    QComboBox *m_monitorSelector = nullptr;
    QComboBox *m_slotSelector = nullptr;
    QCheckBox *m_enabledCheck = nullptr;
    QComboBox *m_edge;
    QComboBox *m_alignment;
    QSpinBox *m_iconSize;
    QSpinBox *m_spacing;
    QSpinBox *m_margin;
    QCheckBox *m_autohide;
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
    QGroupBox *m_audioOutGroup = nullptr;
    QGroupBox *m_audioInGroup = nullptr;
    QGroupBox *m_audioAppGroup = nullptr;
    QVBoxLayout *m_audioOutLayout = nullptr;
    QVBoxLayout *m_audioInLayout = nullptr;
    QVBoxLayout *m_audioAppLayout = nullptr;
    bool m_audioSliderDown = false;
    bool m_audioRebuildQueued = false;
    QString m_selectedRelanzadorId;
    QString m_selectedScriptRunnerId;
    QListWidget *m_docksList = nullptr;
    QListWidget *m_monitorsList = nullptr;
    QPushButton *m_applyPreviewButton = nullptr;
    QPushButton *m_deleteDockButton = nullptr;
    QString m_selectedTabDockId;
    void updateMonitorsTabButtons();
    // Previews tab. The launcher outlives the tab widgets (buildTabs() recreates
    // them whenever another dock is selected).
    PreviewsLauncher *m_previewsLauncher = nullptr;
    TileMenuLauncher *m_tileLauncher = nullptr;
    QCheckBox *m_previewsEnabled = nullptr;
    QLabel *m_previewsStatus = nullptr;
};
