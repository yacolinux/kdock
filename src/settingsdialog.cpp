#include "settingsdialog.h"

#include "audiocontrol.h"
#include "networksettingswidget.h"
#include "appearancecontrol.h"
#include "coloredtabbar.h"
#include "desktopentry.h"
#include "dockconfig.h"
#include "dockmanager.h"
#include "dockmodel.h"
#include "iconcolorprovider.h"
#include "relanzadorconfig.h"
#include "relanzadoresmanager.h"
#include "configarchive.h"
#include "iconpickerdialog.h"
#include "previewslauncher.h"
#include "tilemenulauncher.h"
#include "scriptrunnerconfig.h"
#include "scriptrunnersmanager.h"
#include "systray.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QGroupBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QScreen>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "theme.h"

namespace {

// Select <id> in <box>, appending an explicit "(not installed)" entry when a
// configured set is missing — without it the combo would silently claim the
// first theme, and the dock would keep using the missing one.
void selectComboData(QComboBox *box, const QString &id)
{
    int idx = box->findData(id);
    if (idx < 0 && !id.isEmpty()) {
        box->addItem(SettingsDialog::tr("%1 (not installed)").arg(id), id);
        idx = box->count() - 1;
    }
    box->setCurrentIndex(qMax(0, idx));
}

} // namespace

SettingsDialog::SettingsDialog(DockConfig *config, DesktopEntryIndex *apps, SystrayHost *systray,
                               RelanzadoresManager *relanzadores, DockManager *manager,
                               Theme *theme, AudioControl *audio,
                               AppearanceControl *appearance, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
    , m_apps(apps)
    , m_relanzadores(relanzadores)
    , m_audio(audio)
    , m_appearance(appearance)
    , m_manager(manager)
    , m_theme(theme)
{
    m_scriptRunners = manager ? manager->scriptRunners() : nullptr;
    m_iconColors = new IconColorProvider(this);
    setWindowTitle(tr("kdock Settings"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("preferences-desktop")));

    auto *mainLayout = new QVBoxLayout(this);

    // Multi-instance: a monitor + dock-slot selector lets one dialog configure
    // every dock. Each dock has its own DockConfig (kdock-<screen>[-<slot>].conf).
    if (m_manager) {
        m_dockId = m_config->dockId(); // the dock that opened the dialog
        if (m_dockId.isEmpty())
            m_dockId = m_config->screenName();
        const QString screen = DockConfig::screenOfDockId(m_dockId);
        auto *bar = new QHBoxLayout;
        bar->addWidget(new QLabel(tr("Monitor:"), this));
        m_monitorSelector = new QComboBox(this);
        for (const QString &name : m_manager->connectedScreens())
            m_monitorSelector->addItem(name, name);
        const int idx = m_monitorSelector->findData(screen);
        if (idx >= 0)
            m_monitorSelector->setCurrentIndex(idx);
        bar->addWidget(m_monitorSelector, 1);

        bar->addWidget(new QLabel(tr("Dock:"), this));
        m_slotSelector = new QComboBox(this);
        for (int slot = 0; slot < DockConfig::kMaxDocksPerScreen; ++slot)
            m_slotSelector->addItem(tr("Dock %1").arg(slot + 1), slot);
        m_slotSelector->setCurrentIndex(DockConfig::slotOfDockId(m_dockId));
        bar->addWidget(m_slotSelector);

        m_enabledCheck = new QCheckBox(tr("Show dock here"), this);
        bar->addWidget(m_enabledCheck);
        mainLayout->addLayout(bar);

        connect(m_monitorSelector, &QComboBox::currentIndexChanged, this,
                &SettingsDialog::selectFromCombos);
        connect(m_slotSelector, &QComboBox::currentIndexChanged, this,
                &SettingsDialog::selectFromCombos);
        connect(m_enabledCheck, &QCheckBox::toggled, this, [this](bool on) {
            if (m_manager && !m_dockId.isEmpty())
                m_manager->setDockEnabled(m_dockId, on);
        });
    }

    m_tabWidget = new ColoredTabWidget(this);
    mainLayout->addWidget(m_tabWidget);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    // "Quit Dock" lives here now (removed from the icon right-click menu). Placed
    // with ResetRole so Qt lays it out on the opposite side from Close.
    auto *quitBtn = box->addButton(tr("Quit Dock"), QDialogButtonBox::ResetRole);
    quitBtn->setIcon(QIcon::fromTheme(QStringLiteral("application-exit")));
    quitBtn->setStyleSheet(QStringLiteral(
        "background-color:#c0392b; color:white; padding:4px 12px; border-radius:3px;"));
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(quitBtn, &QPushButton::clicked, this, [] { QCoreApplication::quit(); });
    mainLayout->addWidget(box);

    buildTabs();
    updateEnabledCheck();

    // Keep the dialog within the screen so the bottom buttons are always visible
    // (tabs scroll internally for overflow).
    // The width no longer follows the tab bar: since the tabs moved to a column
    // down the left side (ColoredTabWidget's ctor) they cost height, not width.
    // What is left is content — the Network tab's list + editor splitter is the
    // widest of them — minus the ~160 px the column takes. Clamped to the
    // screen the same way the height is.
    int w = 1120;
    int h = 900;
    if (QScreen *s = QGuiApplication::primaryScreen()) {
        const QRect avail = s->availableGeometry();
        w = qMin(w, avail.width() - 40);
        h = qMin(h, avail.height() - 80);
    }
    resize(qMax(800, w), qMax(360, h));
}

void SettingsDialog::buildTabs()
{
    // Rebuilding tears down the Monitors tab widgets, so drop any live previews
    // they were tracking to avoid orphaned preview windows.
    if (m_manager)
        m_manager->clearPreviews();
    while (m_tabWidget->count() > 0) {
        QWidget *w = m_tabWidget->widget(0);
        m_tabWidget->removeTab(0);
        delete w;
    }
    // Wrap each tab in a scroll area so tall tabs (General/Widgets) scroll
    // internally instead of pushing the dialog's bottom buttons off-screen.
    const auto addTab = [this](QWidget *content, const QString &title) {
        auto *scroll = new QScrollArea(m_tabWidget);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(content);
        m_tabWidget->addTab(scroll, title);
    };
    addTab(createGeneralTab(), tr("General"));
    // Look & feel hubs: every icon/color option (Colores) and font option
    // (Fuentes) of the dock, synced copies of the controls in the tabs below.
    addTab(createColoresTab(), tr("Colores"));
    addTab(createFuentesTab(), tr("Fuentes"));
    addTab(createWidgetsTab(), tr("Widgets"));
    addTab(createMenuTab(), tr("Menu"));
    addTab(createDarkModeTab(), tr("DarkMode"));
    m_audioTabIndex = -1;
    m_audioOutGroup = m_audioInGroup = m_audioAppGroup = nullptr;
    m_audioOutLayout = m_audioInLayout = m_audioAppLayout = nullptr;
    addTab(createLayoutTab(), tr("Layout"));
    if (m_relanzadores)
        addTab(createRelanzadoresTab(), tr("Relanzadores"));
    if (m_scriptRunners)
        addTab(createScriptRunnersTab(), tr("Script Runner"));
    addTab(createBackupTab(), tr("Backup"));
    m_monitorsTabIndex = -1;
    if (m_manager) {
        addTab(createMonitorsTab(), tr("Monitores"));
        m_monitorsTabIndex = m_tabWidget->count() - 1;
    }
    // Not a per-dock setting: the previews are their own process with their own
    // config, so this tab looks the same whichever dock is selected.
    if (PreviewsLauncher::available())
        addTab(createPreviewsTab(), tr("Previews"));
    // Audio and Redes last: neither is per-dock, and both are what the volume
    // and network widgets' right-click jump to.
    if (m_audio && m_audio->available()) {
        addTab(createAudioTab(), tr("Audio"));
        m_audioTabIndex = m_tabWidget->count() - 1;
    }
    m_networkTabIndex = -1;
    addTab(createNetworkTab(), tr("Redes"));
    m_networkTabIndex = m_tabWidget->count() - 1;
    applyTabColors();
}

void SettingsDialog::applyTabColors()
{
    ColoredTabBar *bar = m_tabWidget->coloredTabBar();
    const int n = m_tabWidget->count();
    const QList<QColor> colors = tabPalette(n);
    bar->clearTabColors();
    for (int i = 0; i < n; ++i)
        bar->setTabColor(i, colors.at(i));
}

QList<QColor> SettingsDialog::tabPalette(int count) const
{
    QList<QColor> colors;
    // Two tints that land near each other defeat the point, so candidates are
    // rejected by Manhattan distance against the ones already taken.
    const auto tooClose = [&colors](const QColor &c) {
        for (const QColor &o : colors) {
            if (qAbs(c.red() - o.red()) + qAbs(c.green() - o.green())
                    + qAbs(c.blue() - o.blue()) < 150)
                return true;
        }
        return false;
    };

    if (DockModel *model = m_manager ? m_manager->modelFor(m_dockId) : nullptr) {
        const int revision = m_theme ? m_theme->revision() : 0;
        const int rows = model->rowCount();
        for (int row = 0; row < rows && colors.size() < count; ++row) {
            const QModelIndex idx = model->index(row, 0);
            if (idx.data(DockModel::IsSeparatorRole).toBool())
                continue;
            const QString iconName = idx.data(DockModel::IconNameRole).toString();
            if (iconName.isEmpty())
                continue;
            const QColor dominant = m_iconColors->dominant(iconName, revision);
            // Washed-out or monochrome icons (and whole icon themes) give tints
            // that read as "slightly off gray"; skip them and let the fallback
            // hues below fill in instead.
            if (!dominant.isValid() || dominant.saturation() < 70)
                continue;
            // Pull into a band that works as a background behind text.
            const QColor tint = QColor::fromHsv(dominant.hue(),
                                                qBound(120, dominant.saturation(), 235),
                                                qBound(130, dominant.value(), 240));
            if (tooClose(tint))
                continue;
            colors.append(tint);
        }
    }

    // Fallback for the tabs left over (no dock running, few apps, monochrome
    // icon theme): golden-angle hues, which are distinct by construction.
    for (int i = 0; colors.size() < count; ++i)
        colors.append(QColor::fromHsv((i * 137 + 15) % 360, 175, 205));
    return colors;
}

void SettingsDialog::selectFromCombos()
{
    if (!m_monitorSelector || !m_slotSelector)
        return;
    const QString screen = m_monitorSelector->currentData().toString();
    const int slot = m_slotSelector->currentData().toInt();
    selectDock(DockConfig::makeDockId(screen, slot));
}

void SettingsDialog::selectDock(const QString &dockId)
{
    if (!m_manager || dockId.isEmpty() || dockId == m_dockId)
        return;
    m_dockId = dockId;
    m_config = m_manager->configFor(dockId); // same object the live dock uses
    m_relanzadores = m_manager->relanzadores();
    m_scriptRunners = m_manager->scriptRunners();
    buildTabs();
    updateEnabledCheck();
}

void SettingsDialog::updateEnabledCheck()
{
    if (!m_enabledCheck || !m_manager)
        return;
    const QSignalBlocker block(m_enabledCheck);
    m_enabledCheck->setChecked(m_manager->isDockEnabled(m_dockId));
}

QWidget *SettingsDialog::createGeneralTab()
{
    auto *tab = new QWidget;
    auto *form = new QFormLayout(tab);

    m_edge = new QComboBox(tab);
    m_edge->addItems({tr("Bottom"), tr("Top"), tr("Left"), tr("Right")});
    m_edge->setCurrentIndex(m_config->edge());
    connect(m_edge, &QComboBox::currentIndexChanged, m_config, &DockConfig::setEdge);
    form->addRow(tr("Screen edge:"), m_edge);

    // Under multi-instance the monitor is fixed per dock (chosen via the
    // top-of-dialog selector), so this per-dock override is only offered in the
    // legacy single-instance mode.
    if (!m_manager) {
        auto *screenCombo = new QComboBox(tab);
        screenCombo->addItem(tr("(Automatic)"), QString());
        const auto screens = QGuiApplication::screens();
        for (QScreen *s : screens)
            screenCombo->addItem(QStringLiteral("%1 (%2x%3)")
                                     .arg(s->name())
                                     .arg(s->geometry().width())
                                     .arg(s->geometry().height()),
                                 s->name());
        const int screenIdx = screenCombo->findData(m_config->screenName());
        screenCombo->setCurrentIndex(screenIdx < 0 ? 0 : screenIdx);
        connect(screenCombo, &QComboBox::currentIndexChanged, this, [this, screenCombo](int i) {
            m_config->setScreenName(screenCombo->itemData(i).toString());
        });
        form->addRow(tr("Screen:"), screenCombo);
    }

    auto *panelMode = new QCheckBox(tr("Stretch across the whole screen edge"), tab);
    panelMode->setChecked(m_config->panelMode());
    connect(panelMode, &QCheckBox::toggled, m_config, &DockConfig::setPanelMode);
    form->addRow(tr("Panel mode:"), panelMode);

    auto *compact = new QCheckBox(tr("No empty borders around the icons"), tab);
    compact->setChecked(m_config->compact());
    connect(compact, &QCheckBox::toggled, m_config, &DockConfig::setCompact);
    form->addRow(tr("Compact:"), compact);

    auto *showAppIcons = new QCheckBox(tr("Mostrar los íconos de aplicaciones"), tab);
    showAppIcons->setChecked(m_config->showAppIcons());
    showAppIcons->setToolTip(tr("Desmarcá para usar este dock como una barra de solo "
                                "widgets: se ocultan los lanzadores anclados y los botones "
                                "de las ventanas abiertas, y el dock adelgaza al tamaño de "
                                "sus widgets. La sección \"Aplicaciones\" se queda en la "
                                "solapa Diseño, en su lugar."));
    connect(showAppIcons, &QCheckBox::toggled, m_config, &DockConfig::setShowAppIcons);
    // The Layout tab marks the apps row as hidden, so it has to follow.
    connect(m_config, &DockConfig::showAppIconsChanged, this, [this] {
        if (m_layoutList)
            reloadLayoutList();
    });
    form->addRow(tr("Íconos de apps:"), showAppIcons);

    m_alignment = new QComboBox(tab);
    m_alignment->addItems({tr("Start (left/top)"), tr("Center"), tr("End (right/bottom)")});
    m_alignment->setCurrentIndex(m_config->alignment());
    connect(m_alignment, &QComboBox::currentIndexChanged, m_config, &DockConfig::setAlignment);
    form->addRow(tr("Icon alignment:"), m_alignment);

    // Alignment has no effect in panel mode when a dynamic separator (spring)
    // is present: the spring expands and pushes the sections toward the edges.
    // Disable the combo and explain why, so the dialog is honest about its
    // effect instead of silently doing nothing.
    m_alignmentNote = new QLabel(tab);
    m_alignmentNote->setWordWrap(true);
    m_alignmentNote->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
    m_alignmentNote->setVisible(false);
    form->addRow(QString(), m_alignmentNote);

    auto updateAlignmentEnabled = [this] {
        const bool hasSpring = m_config->widgetOrder().contains(QStringLiteral("spring"));
        // Alignment is ignored only when the dock spans the whole edge (100%
        // panel mode) AND a spring is present. In fixed-length mode (>0) or
        // floating mode the alignment always applies.
        const bool fullEdge = m_config->panelMode() && m_config->dockLength() == 0;
        const bool disabled = fullEdge && hasSpring;
        m_alignment->setEnabled(!disabled);
        if (disabled) {
            m_alignmentNote->setText(tr("Disabled: a dynamic separator is present. "
                                        "Remove it in the Layout tab to use alignment."));
        }
        m_alignmentNote->setVisible(disabled);
    };
    connect(m_config, &DockConfig::panelModeChanged, this,
            [updateAlignmentEnabled]() { updateAlignmentEnabled(); });
    connect(m_config, &DockConfig::widgetOrderChanged, this,
            [updateAlignmentEnabled]() { updateAlignmentEnabled(); });
    connect(m_config, &DockConfig::dockLengthChanged, this,
            [updateAlignmentEnabled]() { updateAlignmentEnabled(); });
    updateAlignmentEnabled();

    m_dockLength = new QSpinBox(tab);
    m_dockLength->setRange(0, 100);
    m_dockLength->setSingleStep(5);
    m_dockLength->setSuffix(QStringLiteral("%"));
    m_dockLength->setSpecialValueText(tr("Auto"));
    m_dockLength->setValue(m_config->dockLength());
    m_dockLength->setToolTip(tr("0 = auto (panel stretches 100% or adjusts to content). "
                                ">0 = fixed length as a percentage of the screen edge."));
    connect(m_dockLength, &QSpinBox::valueChanged, m_config, &DockConfig::setDockLength);
    form->addRow(tr("Dock length:"), m_dockLength);

    m_iconSize = new QSpinBox(tab);
    m_iconSize->setRange(24, 128);
    m_iconSize->setSingleStep(4);
    m_iconSize->setValue(m_config->iconSize());
    connect(m_iconSize, &QSpinBox::valueChanged, m_config, &DockConfig::setIconSize);
    form->addRow(tr("Icon size:"), m_iconSize);

    // Widget icon scale: shrink widget sections (clock, volume, systray, ...)
    // relative to the icon size, without affecting launchers/relanzadores.
    auto *widgetScale = new QSpinBox(tab);
    widgetScale->setRange(20, 100);
    widgetScale->setSingleStep(5);
    widgetScale->setSuffix(QStringLiteral("%"));
    widgetScale->setValue(m_config->widgetIconScale());
    widgetScale->setToolTip(tr("Size of widget icons as a percentage of the icon "
                               "size. Does not affect launchers or relanzadores."));
    connect(widgetScale, &QSpinBox::valueChanged, m_config, &DockConfig::setWidgetIconScale);
    form->addRow(tr("Widget icon scale:"), widgetScale);

    // Auto-shrink: the dock has a fixed length but its content does not, and a
    // layout that no longer fits draws its sections over one another. Shrinking
    // every icon (and the gaps, and the clock font) keeps them apart.
    {
        auto *autoShrink = new QCheckBox(tr("Shrink icons when they do not fit"), tab);
        autoShrink->setChecked(m_config->autoShrinkIcons());
        autoShrink->setToolTip(tr("When the sections no longer fit along the dock, every "
                                  "icon is scaled down (never past the minimum below) "
                                  "instead of overflowing or overlapping."));
        connect(autoShrink, &QCheckBox::toggled, m_config, &DockConfig::setAutoShrinkIcons);
        connect(m_config, &DockConfig::autoShrinkIconsChanged, autoShrink,
                [this, autoShrink] { autoShrink->setChecked(m_config->autoShrinkIcons()); });
        form->addRow(tr("Auto-shrink:"), autoShrink);

        auto *minIcon = new QSpinBox(tab);
        minIcon->setRange(8, 64);
        minIcon->setSuffix(tr(" px"));
        minIcon->setValue(m_config->autoShrinkMinIconSize());
        minIcon->setToolTip(tr("Smallest size an application icon is shrunk to. Widget "
                               "and systray icons keep their proportion, so they can end "
                               "up smaller than this."));
        connect(minIcon, &QSpinBox::valueChanged, m_config, &DockConfig::setAutoShrinkMinIconSize);
        form->addRow(tr("· Minimum icon size:"), minIcon);

        minIcon->setEnabled(m_config->autoShrinkIcons());
        connect(autoShrink, &QCheckBox::toggled, minIcon, &QWidget::setEnabled);
    }

    // Icon theme (global; overrides the KDE theme for all docks).
    if (m_theme) {
        auto *iconTheme = new QComboBox(tab);
        iconTheme->addItem(tr("(System default)"), QString());
        for (const auto &t : Theme::availableIconThemes())
            iconTheme->addItem(t.first, t.second);
        const int idx = iconTheme->findData(m_theme->iconTheme());
        iconTheme->setCurrentIndex(idx < 0 ? 0 : idx);
        connect(iconTheme, &QComboBox::currentIndexChanged, this, [this, iconTheme](int i) {
            m_theme->setIconTheme(iconTheme->itemData(i).toString());
        });
        // The same combo lives in the Colores tab (and the override is global,
        // so another dock's dialog can change it too): re-read it on Theme
        // reloads instead of going stale.
        connect(m_theme, &Theme::changed, iconTheme,
                [this, iconTheme] { selectComboData(iconTheme, m_theme->iconTheme()); });
        form->addRow(tr("Icon theme:"), iconTheme);
    }

    // Widget icons: the standard icons of the widget sections are monochrome
    // and drawn for one background, so they can vanish over a custom panel
    // color. Pull them from an icon set built for the dock's background
    // instead (Breeze / Breeze Dark by default). Launchers, relanzadores and
    // the systray keep the icon theme above.
    {
        auto *widgetIcons = new QComboBox(tab);
        widgetIcons->addItem(tr("Follow icon theme"), int(DockConfig::FollowIconTheme));
        widgetIcons->addItem(tr("Match dock color"), int(DockConfig::MatchDockColor));
        widgetIcons->addItem(tr("Always dark icons"), int(DockConfig::AlwaysLightBg));
        widgetIcons->addItem(tr("Always light icons"), int(DockConfig::AlwaysDarkBg));
        const int mIdx = widgetIcons->findData(m_config->widgetIconThemeMode());
        widgetIcons->setCurrentIndex(mIdx < 0 ? 1 : mIdx);
        widgetIcons->setToolTip(tr("Icon set used for the widget icons (volume, network, "
                                   "session…). \"Match dock color\" picks the light- or "
                                   "dark-background set from the panel's brightness."));
        form->addRow(tr("Widget icons:"), widgetIcons);

        auto *lightBg = new QComboBox(tab);
        auto *darkBg = new QComboBox(tab);
        for (const auto &t : Theme::availableIconThemes()) {
            lightBg->addItem(t.first, t.second);
            darkBg->addItem(t.first, t.second);
        }
        // A configured set that isn't installed still has to show up, or the
        // combo would claim a set the dock is not actually using.
        const auto selectTheme = [](QComboBox *box, const QString &id) {
            int idx = box->findData(id);
            if (idx < 0 && !id.isEmpty()) {
                box->addItem(tr("%1 (not installed)").arg(id), id);
                idx = box->count() - 1;
            }
            box->setCurrentIndex(qMax(0, idx));
        };
        selectTheme(lightBg, m_config->widgetIconThemeLightBg());
        selectTheme(darkBg, m_config->widgetIconThemeDarkBg());
        lightBg->setToolTip(tr("Icon set for a light dock background (dark icons)."));
        darkBg->setToolTip(tr("Icon set for a dark dock background (light icons)."));
        connect(lightBg, &QComboBox::currentIndexChanged, this, [this, lightBg](int i) {
            m_config->setWidgetIconThemeLightBg(lightBg->itemData(i).toString());
        });
        connect(darkBg, &QComboBox::currentIndexChanged, this, [this, darkBg](int i) {
            m_config->setWidgetIconThemeDarkBg(darkBg->itemData(i).toString());
        });
        form->addRow(tr("· On light dock:"), lightBg);
        form->addRow(tr("· On dark dock:"), darkBg);

        // The two set pickers are only meaningful while an override is active.
        auto syncEnabled = [this, lightBg, darkBg] {
            const bool on = m_config->widgetIconThemeMode() != DockConfig::FollowIconTheme;
            lightBg->setEnabled(on);
            darkBg->setEnabled(on);
        };
        connect(widgetIcons, &QComboBox::currentIndexChanged, this,
                [this, widgetIcons, syncEnabled](int i) {
                    m_config->setWidgetIconThemeMode(widgetIcons->itemData(i).toInt());
                    syncEnabled();
                });
        // The whole group is mirrored in the Colores tab, so it re-reads from
        // the config whenever any of the three values changes (the signal
        // covers all three setters). Setters only emit on a real change, so
        // the loop this creates terminates on its own.
        const auto resync = [this, widgetIcons, lightBg, darkBg, selectTheme, syncEnabled] {
            widgetIcons->setCurrentIndex(qMax(0, widgetIcons->findData(
                                                m_config->widgetIconThemeMode())));
            selectTheme(lightBg, m_config->widgetIconThemeLightBg());
            selectTheme(darkBg, m_config->widgetIconThemeDarkBg());
            syncEnabled();
        };
        connect(m_config, &DockConfig::widgetIconThemeChanged, tab, resync);
        syncEnabled();
    }

    m_spacing = new QSpinBox(tab);
    m_spacing->setRange(0, 32);
    m_spacing->setValue(m_config->spacing());
    connect(m_spacing, &QSpinBox::valueChanged, m_config, &DockConfig::setSpacing);
    form->addRow(tr("Icon spacing:"), m_spacing);

    m_margin = new QSpinBox(tab);
    m_margin->setRange(0, 64);
    m_margin->setValue(m_config->screenMargin());
    connect(m_margin, &QSpinBox::valueChanged, m_config, &DockConfig::setScreenMargin);
    form->addRow(tr("Screen edge margin:"), m_margin);

    m_autohide = new QCheckBox(tr("Hide the dock when not in use"), tab);
    m_autohide->setChecked(m_config->autohide());
    connect(m_autohide, &QCheckBox::toggled, m_config, &DockConfig::setAutohide);
    form->addRow(tr("Auto-hide:"), m_autohide);

    m_opacity = new QSlider(Qt::Horizontal, tab);
    m_opacity->setRange(0, 100);
    m_opacity->setValue(int(m_config->opacity() * 100));
    connect(m_opacity, &QSlider::valueChanged, this,
            [this](int v) { m_config->setOpacity(v / 100.0); });
    form->addRow(tr("Background opacity:"), m_opacity);

    // Per-panel background color (overrides the inherited KDE theme color).
    auto *colorRow = new QHBoxLayout;
    auto *colorBtn = new QPushButton(tab);
    auto *colorReset = new QPushButton(tr("Reset to theme"), tab);
    const auto refreshColorBtn = [this, colorBtn] {
        if (m_config->panelColorSet()) {
            const QColor c = m_config->panelColor();
            colorBtn->setText(c.name(QColor::HexRgb).toUpper());
            colorBtn->setStyleSheet(QStringLiteral(
                "background-color:%1; color:%2; padding:4px 12px; border:1px solid gray;")
                .arg(c.name(), c.lightnessF() > 0.5 ? QStringLiteral("black") : QStringLiteral("white")));
        } else {
            colorBtn->setText(tr("Theme default"));
            colorBtn->setStyleSheet(QString());
        }
    };
    refreshColorBtn();
    connect(colorBtn, &QPushButton::clicked, this, [this, refreshColorBtn] {
        const QColor initial = m_config->panelColorSet() ? m_config->panelColor()
                                                         : palette().window().color();
        const QColor c = QColorDialog::getColor(initial, this, tr("Panel color"));
        if (c.isValid()) {
            m_config->setPanelColor(c);
            refreshColorBtn();
        }
    });
    connect(colorReset, &QPushButton::clicked, this, [this, refreshColorBtn] {
        m_config->setPanelColor(QColor()); // invalid = inherit theme
        refreshColorBtn();
    });
    colorRow->addWidget(colorBtn, 1);
    colorRow->addWidget(colorReset);
    form->addRow(tr("Panel color:"), colorRow);
    // Mirrored in the Colores tab: follow the config (and changes made from
    // another dock's dialog) instead of going stale.
    connect(m_config, &DockConfig::panelColorChanged, colorBtn, refreshColorBtn);

    // Quick-color presets, offered in the right-click "background color"
    // submenu. Compact graphical form: a row of colored swatch buttons. The
    // palette is shared by every dock, so each swatch also follows changes made
    // from another dock's dialog.
    auto *presetsRow = new QHBoxLayout;
    presetsRow->setSpacing(4);
    for (int i = 0; i < DockConfig::kPresetColorCount; ++i) {
        auto *sw = new QPushButton(tab);
        sw->setFixedSize(28, 28);
        const auto refreshSwatch = [this, sw, i] {
            const QColor c(m_config->panelPresetColors().value(i));
            sw->setToolTip(c.name(QColor::HexRgb).toUpper());
            sw->setStyleSheet(QStringLiteral(
                "background-color:%1; border:1px solid gray; border-radius:4px;").arg(c.name()));
        };
        refreshSwatch();
        connect(m_config, &DockConfig::panelPresetColorsChanged, sw, refreshSwatch);
        connect(sw, &QPushButton::clicked, this, [this, i] {
            QStringList presets = m_config->panelPresetColors();
            const QColor c = QColorDialog::getColor(QColor(presets.value(i)), this,
                                                    tr("Quick color %1").arg(i + 1));
            if (c.isValid()) {
                presets[i] = c.name(QColor::HexRgb);
                m_config->setPanelPresetColors(presets);
            }
        });
        presetsRow->addWidget(sw);
    }
    presetsRow->addStretch();
    form->addRow(tr("Quick colors:"), presetsRow);

    // Per-panel tiled background image (overrides/overlays the panel color).
    auto *imgRow = new QHBoxLayout;
    auto *imgBtn = new QPushButton(tab);
    auto *imgClear = new QPushButton(tr("Clear"), tab);
    const auto refreshImgBtn = [this, imgBtn] {
        const QString p = m_config->panelImage();
        imgBtn->setText(p.isEmpty() ? tr("None") : QFileInfo(p).fileName());
    };
    refreshImgBtn();
    connect(imgBtn, &QPushButton::clicked, this, [this, refreshImgBtn] {
        const QString start = m_config->panelImage().isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
            : QFileInfo(m_config->panelImage()).absolutePath();
        const QString p = QFileDialog::getOpenFileName(
            this, tr("Panel image"), start,
            tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.svg)"));
        if (!p.isEmpty()) {
            m_config->setPanelImage(p);
            refreshImgBtn();
        }
    });
    connect(imgClear, &QPushButton::clicked, this, [this, refreshImgBtn] {
        m_config->setPanelImage(QString());
        refreshImgBtn();
    });
    imgRow->addWidget(imgBtn, 1);
    imgRow->addWidget(imgClear);
    form->addRow(tr("Panel image:"), imgRow);

    // Separators (both kinds) and their size live in the Layout tab: they are
    // part of the section order, not a numeric setting.

    return tab;
}

QWidget *SettingsDialog::createColoresTab()
{
    // Look & feel hub: every icon-set / color setting of the dock lives here as
    // a synced copy of the control in its original tab (General, Widgets,
    // DarkMode). Nothing was removed from those tabs: each copy writes to the
    // same DockConfig key (or static) and re-reads it on the key's *Changed
    // signal, so whichever tab the user edits, the others show the same value.
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    // --- Escritorio: los pickers de iconset / esquema de KDE ---
    {
        auto *box = new QGroupBox(tr("Escritorio (KDE)"), tab);
        auto *form = new QFormLayout(box);

        auto *showIconThemes = new QCheckBox(tr("Show icon-theme picker"), box);
        showIconThemes->setChecked(m_config->showIconThemes());
        showIconThemes->setToolTip(tr("Applies the icon theme to the whole desktop. kdock's own "
                                      "icon theme (below) is left alone: while it is set, this "
                                      "dock keeps its icons and only the rest of KDE follows."));
        connect(showIconThemes, &QCheckBox::toggled, m_config, &DockConfig::setShowIconThemes);
        connect(m_config, &DockConfig::showIconThemesChanged, showIconThemes,
                [this, showIconThemes] { showIconThemes->setChecked(m_config->showIconThemes()); });
        form->addRow(tr("Icon theme picker:"), showIconThemes);

        auto *showColorSchemes = new QCheckBox(tr("Show color-scheme picker"), box);
        showColorSchemes->setChecked(m_config->showColorSchemes());
        showColorSchemes->setToolTip(tr("Applies a KDE color scheme system-wide "
                                        "(plasma-apply-colorscheme)."));
        connect(showColorSchemes, &QCheckBox::toggled, m_config, &DockConfig::setShowColorSchemes);
        connect(m_config, &DockConfig::showColorSchemesChanged, showColorSchemes,
                [this, showColorSchemes] {
                    showColorSchemes->setChecked(m_config->showColorSchemes());
                });
        form->addRow(tr("Color scheme picker:"), showColorSchemes);

        // Direct apply, no dock widget needed: the same backend the pickers
        // use (AppearanceControl, which goes through the Plasma tools so the
        // open apps reload). Each combo re-selects itself when kdeglobals
        // changes (AppearanceControl::changed), so choosing from the dock
        // widget's popup shows up here too.
        if (m_appearance) {
            m_appearance->refreshIfStale();

            auto *iconCombo = new QComboBox(box);
            const QVariantList icons = m_appearance->iconThemes();
            for (const QVariant &v : icons) {
                const QVariantMap m = v.toMap();
                iconCombo->addItem(m.value(QStringLiteral("name")).toString(),
                                   m.value(QStringLiteral("id")).toString());
            }
            iconCombo->setToolTip(tr("Applies the icon theme to the whole desktop right now "
                                     "(plasma-changeicons). kdock's own icon theme is left "
                                     "alone: while it is set, this dock keeps its icons and "
                                     "only the rest of KDE follows."));
            const auto syncIcon = [this, iconCombo] {
                const int i = iconCombo->findData(m_appearance->currentIconTheme());
                if (i >= 0 && i != iconCombo->currentIndex())
                    iconCombo->setCurrentIndex(i);
            };
            syncIcon();
            connect(iconCombo, &QComboBox::currentIndexChanged, this, [this, iconCombo] {
                const QString id = iconCombo->currentData().toString();
                if (id != m_appearance->currentIconTheme())
                    m_appearance->applyIconTheme(id);
            });
            connect(m_appearance, &AppearanceControl::changed, box, syncIcon);
            form->addRow(tr("· Apply icon theme:"), iconCombo);

            auto *schemeCombo = new QComboBox(box);
            const QVariantList schemes = m_appearance->colorSchemes();
            for (const QVariant &v : schemes) {
                const QVariantMap m = v.toMap();
                schemeCombo->addItem(m.value(QStringLiteral("name")).toString(),
                                     m.value(QStringLiteral("id")).toString());
            }
            schemeCombo->setToolTip(tr("Applies a KDE color scheme system-wide right now "
                                       "(plasma-apply-colorscheme)."));
            const auto syncScheme = [this, schemeCombo] {
                const int i = schemeCombo->findData(m_appearance->currentColorScheme());
                if (i >= 0 && i != schemeCombo->currentIndex())
                    schemeCombo->setCurrentIndex(i);
            };
            syncScheme();
            connect(schemeCombo, &QComboBox::currentIndexChanged, this, [this, schemeCombo] {
                const QString id = schemeCombo->currentData().toString();
                if (id != m_appearance->currentColorScheme())
                    m_appearance->applyColorScheme(id);
            });
            connect(m_appearance, &AppearanceControl::changed, box, syncScheme);
            form->addRow(tr("· Apply color scheme:"), schemeCombo);
        }

        layout->addWidget(box);
    }

    // --- Iconset del dock: el override global de kdock (General → "Icon theme") ---
    {
        auto *box = new QGroupBox(tr("Iconset del dock"), tab);
        auto *form = new QFormLayout(box);

        if (m_theme) {
            auto *iconTheme = new QComboBox(box);
            iconTheme->addItem(tr("(System default)"), QString());
            for (const auto &t : Theme::availableIconThemes())
                iconTheme->addItem(t.first, t.second);
            // Sync re-reads the override (empty = follow KDE) on Theme reloads.
            const auto sync = [this, iconTheme] { selectComboData(iconTheme, m_theme->iconTheme()); };
            sync();
            connect(iconTheme, &QComboBox::currentIndexChanged, this, [this, iconTheme](int i) {
                m_theme->setIconTheme(iconTheme->itemData(i).toString());
            });
            connect(m_theme, &Theme::changed, box, sync);
            form->addRow(tr("Iconset del dock:"), iconTheme);
        }

        layout->addWidget(box);
    }

    // --- Íconos de widgets: sets adaptados al color del panel ---
    {
        auto *box = new QGroupBox(tr("Íconos de widgets"), tab);
        auto *form = new QFormLayout(box);

        auto *widgetIcons = new QComboBox(box);
        widgetIcons->addItem(tr("Follow icon theme"), int(DockConfig::FollowIconTheme));
        widgetIcons->addItem(tr("Match dock color"), int(DockConfig::MatchDockColor));
        widgetIcons->addItem(tr("Always dark icons"), int(DockConfig::AlwaysLightBg));
        widgetIcons->addItem(tr("Always light icons"), int(DockConfig::AlwaysDarkBg));
        const int mIdx = widgetIcons->findData(m_config->widgetIconThemeMode());
        widgetIcons->setCurrentIndex(mIdx < 0 ? 1 : mIdx);
        widgetIcons->setToolTip(tr("Icon set used for the widget icons (volume, network, "
                                   "session…). \"Match dock color\" picks the light- or "
                                   "dark-background set from the panel's brightness."));
        form->addRow(tr("Widget icons:"), widgetIcons);

        auto *lightBg = new QComboBox(box);
        auto *darkBg = new QComboBox(box);
        for (const auto &t : Theme::availableIconThemes()) {
            lightBg->addItem(t.first, t.second);
            darkBg->addItem(t.first, t.second);
        }
        // A configured set that isn't installed still has to show up, or the
        // combo would claim a set the dock is not actually using.
        selectComboData(lightBg, m_config->widgetIconThemeLightBg());
        selectComboData(darkBg, m_config->widgetIconThemeDarkBg());
        lightBg->setToolTip(tr("Icon set for a light dock background (dark icons)."));
        darkBg->setToolTip(tr("Icon set for a dark dock background (light icons)."));
        connect(lightBg, &QComboBox::currentIndexChanged, this, [this, lightBg](int i) {
            m_config->setWidgetIconThemeLightBg(lightBg->itemData(i).toString());
        });
        connect(darkBg, &QComboBox::currentIndexChanged, this, [this, darkBg](int i) {
            m_config->setWidgetIconThemeDarkBg(darkBg->itemData(i).toString());
        });
        form->addRow(tr("· On light dock:"), lightBg);
        form->addRow(tr("· On dark dock:"), darkBg);

        // The two set pickers are only meaningful while an override is active.
        auto syncEnabled = [this, lightBg, darkBg] {
            const bool on = m_config->widgetIconThemeMode() != DockConfig::FollowIconTheme;
            lightBg->setEnabled(on);
            darkBg->setEnabled(on);
        };
        connect(widgetIcons, &QComboBox::currentIndexChanged, this,
                [this, widgetIcons, syncEnabled](int i) {
                    m_config->setWidgetIconThemeMode(widgetIcons->itemData(i).toInt());
                    syncEnabled();
                });
        // The whole group is mirrored in the General tab: re-read all three on
        // any change (the signal covers the three setters).
        const auto resync = [this, widgetIcons, lightBg, darkBg, syncEnabled] {
            widgetIcons->setCurrentIndex(qMax(0, widgetIcons->findData(
                                                m_config->widgetIconThemeMode())));
            selectComboData(lightBg, m_config->widgetIconThemeLightBg());
            selectComboData(darkBg, m_config->widgetIconThemeDarkBg());
            syncEnabled();
        };
        connect(m_config, &DockConfig::widgetIconThemeChanged, box, resync);
        syncEnabled();

        layout->addWidget(box);
    }

    // --- Color del panel + colores rápidos ---
    {
        auto *box = new QGroupBox(tr("Color del panel"), tab);
        auto *form = new QFormLayout(box);

        auto *colorRow = new QHBoxLayout;
        auto *colorBtn = new QPushButton(box);
        auto *colorReset = new QPushButton(tr("Reset to theme"), box);
        const auto refreshColorBtn = [this, colorBtn] {
            if (m_config->panelColorSet()) {
                const QColor c = m_config->panelColor();
                colorBtn->setText(c.name(QColor::HexRgb).toUpper());
                colorBtn->setStyleSheet(QStringLiteral(
                    "background-color:%1; color:%2; padding:4px 12px; border:1px solid gray;")
                    .arg(c.name(), c.lightnessF() > 0.5 ? QStringLiteral("black")
                                                        : QStringLiteral("white")));
            } else {
                colorBtn->setText(tr("Theme default"));
                colorBtn->setStyleSheet(QString());
            }
        };
        refreshColorBtn();
        connect(colorBtn, &QPushButton::clicked, this, [this, refreshColorBtn] {
            const QColor initial = m_config->panelColorSet() ? m_config->panelColor()
                                                             : palette().window().color();
            const QColor c = QColorDialog::getColor(initial, this, tr("Panel color"));
            if (c.isValid()) {
                m_config->setPanelColor(c);
                refreshColorBtn();
            }
        });
        connect(colorReset, &QPushButton::clicked, this, [this, refreshColorBtn] {
            m_config->setPanelColor(QColor()); // invalid = inherit theme
            refreshColorBtn();
        });
        // Mirrored in the General tab.
        connect(m_config, &DockConfig::panelColorChanged, colorBtn, refreshColorBtn);
        colorRow->addWidget(colorBtn, 1);
        colorRow->addWidget(colorReset);
        form->addRow(tr("Panel color:"), colorRow);

        // Quick-color presets, shared by every dock (also offered in the
        // right-click "background color" submenu).
        auto *presetsRow = new QHBoxLayout;
        presetsRow->setSpacing(4);
        for (int i = 0; i < DockConfig::kPresetColorCount; ++i) {
            auto *sw = new QPushButton(box);
            sw->setFixedSize(28, 28);
            const auto refreshSwatch = [this, sw, i] {
                const QColor c(m_config->panelPresetColors().value(i));
                sw->setToolTip(c.name(QColor::HexRgb).toUpper());
                sw->setStyleSheet(QStringLiteral(
                    "background-color:%1; border:1px solid gray; border-radius:4px;").arg(c.name()));
            };
            refreshSwatch();
            connect(m_config, &DockConfig::panelPresetColorsChanged, sw, refreshSwatch);
            connect(sw, &QPushButton::clicked, this, [this, i] {
                QStringList presets = m_config->panelPresetColors();
                const QColor c = QColorDialog::getColor(QColor(presets.value(i)), this,
                                                        tr("Quick color %1").arg(i + 1));
                if (c.isValid()) {
                    presets[i] = c.name(QColor::HexRgb);
                    m_config->setPanelPresetColors(presets);
                }
            });
            presetsRow->addWidget(sw);
        }
        presetsRow->addStretch();
        form->addRow(tr("Quick colors:"), presetsRow);

        layout->addWidget(box);
    }

    // --- Apps en ejecución: los tres indicadores ---
    {
        auto *box = new QGroupBox(tr("Apps en ejecución"), tab);
        auto *form = new QFormLayout(box);

        auto *runningBg = new QCheckBox(tr("Colored background for running apps"), box);
        runningBg->setChecked(m_config->iconRunningBackground());
        connect(runningBg, &QCheckBox::toggled, m_config, &DockConfig::setIconRunningBackground);
        connect(m_config, &DockConfig::iconRunningBackgroundChanged, runningBg,
                [this, runningBg] { runningBg->setChecked(m_config->iconRunningBackground()); });
        form->addRow(tr("Running highlight:"), runningBg);

        auto *runningDots = new QCheckBox(tr("Window dots for running apps"), box);
        runningDots->setChecked(m_config->iconRunningDots());
        connect(runningDots, &QCheckBox::toggled, m_config, &DockConfig::setIconRunningDots);
        connect(m_config, &DockConfig::iconRunningDotsChanged, runningDots,
                [this, runningDots] { runningDots->setChecked(m_config->iconRunningDots()); });
        form->addRow(tr("Running dots:"), runningDots);

        auto *runningLine = new QCheckBox(tr("Edge line for running apps"), box);
        runningLine->setChecked(m_config->iconRunningLine());
        connect(runningLine, &QCheckBox::toggled, m_config, &DockConfig::setIconRunningLine);
        connect(m_config, &DockConfig::iconRunningLineChanged, runningLine,
                [this, runningLine] { runningLine->setChecked(m_config->iconRunningLine()); });
        form->addRow(tr("Running edge line:"), runningLine);

        layout->addWidget(box);
    }

    // --- Modo oscuro: los dos colores + los efectos opcionales del escritorio ---
    {
        auto *box = new QGroupBox(tr("Modo oscuro"), tab);
        auto *boxLayout = new QVBoxLayout(box);
        auto *form = new QFormLayout;

        auto *accentBtn = new QPushButton(box);
        const auto refreshAccent = makeColorButton(accentBtn, &DockConfig::darkAccentColor,
                                                   &DockConfig::setDarkAccentColor,
                                                   tr("Color de resaltado"));
        accentBtn->setToolTip(tr("Color de los nombres de apps y widgets, y del resaltado de "
                                 "las apps que están corriendo (que en modo oscuro deja de "
                                 "usar el color de cada ícono y pasa a este único color)."));
        auto *accentRow = new QHBoxLayout;
        auto *accentReset = new QPushButton(tr("Breeze Dark"), box);
        connect(accentReset, &QPushButton::clicked, this, [refreshAccent] {
            DockConfig::setDarkAccentColor(QColor(QString::fromLatin1(DockConfig::kDarkAccentDefault)));
            refreshAccent();
        });
        accentRow->addWidget(accentBtn, 1);
        accentRow->addWidget(accentReset);
        form->addRow(tr("Color de resaltado:"), accentRow);

        auto *bgBtn = new QPushButton(box);
        const auto refreshBg = makeColorButton(bgBtn, &DockConfig::darkBackgroundColor,
                                               &DockConfig::setDarkBackgroundColor,
                                               tr("Fondo del dock"));
        bgBtn->setToolTip(tr("Fondo del dock en modo oscuro. La transparencia configurada en "
                             "la solapa General se sigue aplicando igual."));
        auto *bgRow = new QHBoxLayout;
        auto *bgReset = new QPushButton(tr("Breeze Dark"), box);
        connect(bgReset, &QPushButton::clicked, this, [refreshBg] {
            DockConfig::setDarkBackgroundColor(
                QColor(QString::fromLatin1(DockConfig::kDarkBackgroundDefault)));
            refreshBg();
        });
        bgRow->addWidget(bgBtn, 1);
        bgRow->addWidget(bgReset);
        form->addRow(tr("Fondo del dock:"), bgRow);
        // Both colors are mirrored in the DarkMode tab (app-wide statics).
        connect(m_config, &DockConfig::darkModeChanged, box, refreshAccent);
        connect(m_config, &DockConfig::darkModeChanged, box, refreshBg);

        boxLayout->addLayout(form);

        // The optional system-wide side effects, same as the DarkMode tab.
        auto *extrasBox = new QGroupBox(tr("Al cambiar de modo, cambiar también:"), box);
        auto *extrasForm = new QFormLayout(extrasBox);
        extrasForm->addRow(new QLabel(tr("<i>Esto sí toca la configuración del escritorio, "
                                         "no solo el dibujo del dock — por eso cada opción "
                                         "lleva el valor de los dos modos.</i>"), extrasBox));
        addDarkAppearanceExtras(extrasForm, extrasBox);
        boxLayout->addWidget(extrasBox);
        boxLayout->addStretch();

        layout->addWidget(box);
    }

    layout->addStretch();
    return tab;
}

QWidget *SettingsDialog::createFuentesTab()
{
    // Typography hub: every font size of the dock, as a synced copy of the
    // controls in the Widgets tab (the label width/font/bold are shared by the
    // app and widget names; the clock font by both clocks).
    auto *tab = new QWidget;
    auto *form = new QFormLayout(tab);

    auto *clockFont = new QSpinBox(tab);
    clockFont->setRange(0, 96);
    clockFont->setSpecialValueText(tr("Automatic"));
    clockFont->setSuffix(tr(" px"));
    clockFont->setValue(m_config->clockFontSize());
    clockFont->setToolTip(tr("Font size of the clock time text (both clocks). "
                             "Automatic derives it from the widget icon size."));
    connect(clockFont, &QSpinBox::valueChanged, m_config, &DockConfig::setClockFontSize);
    connect(m_config, &DockConfig::clockFontSizeChanged, clockFont,
            [this, clockFont] { clockFont->setValue(m_config->clockFontSize()); });
    form->addRow(tr("Clock font size:"), clockFont);

    // A separator before the name block, which only applies while some name is
    // shown (see the enable logic below).
    auto *nameHeader = new QLabel(tr("<b>Nombres de apps y widgets</b>"), tab);
    form->addRow(nameHeader);

    auto *labelWidth = new QSpinBox(tab);
    labelWidth->setRange(60, 400);
    labelWidth->setSingleStep(10);
    labelWidth->setSuffix(tr(" px"));
    labelWidth->setValue(m_config->iconLabelWidth());
    labelWidth->setToolTip(tr("Width of the name: a maximum (the name is elided past "
                              "it) on a horizontal dock, and the fixed column width on "
                              "a vertical one. Shared by the app and widget names."));
    connect(labelWidth, &QSpinBox::valueChanged, m_config, &DockConfig::setIconLabelWidth);
    connect(m_config, &DockConfig::iconLabelWidthChanged, labelWidth,
            [this, labelWidth] { labelWidth->setValue(m_config->iconLabelWidth()); });
    form->addRow(tr("Name width:"), labelWidth);

    auto *labelFont = new QSpinBox(tab);
    labelFont->setRange(0, 48);
    labelFont->setSpecialValueText(tr("Automatic"));
    labelFont->setSuffix(tr(" px"));
    labelFont->setValue(m_config->iconLabelFontSize());
    labelFont->setToolTip(tr("Font size of the app and widget names. Automatic derives "
                             "it from the icon size."));
    connect(labelFont, &QSpinBox::valueChanged, m_config, &DockConfig::setIconLabelFontSize);
    connect(m_config, &DockConfig::iconLabelFontSizeChanged, labelFont,
            [this, labelFont] { labelFont->setValue(m_config->iconLabelFontSize()); });
    form->addRow(tr("Name font size:"), labelFont);

    auto *labelBold = new QCheckBox(tr("Negritas"), tab);
    labelBold->setChecked(m_config->labelBold());
    labelBold->setToolTip(tr("Draw every name the dock shows — applications, widgets and "
                             "the clock date — in bold."));
    connect(labelBold, &QCheckBox::toggled, m_config, &DockConfig::setLabelBold);
    connect(m_config, &DockConfig::labelBoldChanged, labelBold,
            [this, labelBold] { labelBold->setChecked(m_config->labelBold()); });
    form->addRow(tr("· Bold text:"), labelBold);

    // The width and font size only mean something while some name is shown
    // (mirrors the gating of the Widgets tab). The bold toggle stays enabled
    // even with apps/widgets names off: it also drives the clock date, which
    // is always text.
    auto syncLabelEnabled = [this, labelWidth, labelFont] {
        const bool on = m_config->iconLabelMode() != DockConfig::IconOnly
                        || m_config->widgetLabelMode() != DockConfig::IconOnly;
        labelWidth->setEnabled(on);
        labelFont->setEnabled(on);
    };
    connect(m_config, &DockConfig::iconLabelModeChanged, tab, syncLabelEnabled);
    connect(m_config, &DockConfig::widgetLabelModeChanged, tab, syncLabelEnabled);
    syncLabelEnabled();

    form->addRow(new QLabel(tr("<i>El modo de etiqueta (ícono solo / nombre abajo, etc.) "
                               "se elige en la solapa Widgets.</i>"), tab));
    return tab;
}

QWidget *SettingsDialog::createWidgetsTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);
    auto *form = new QFormLayout;

    auto *showVolume = new QCheckBox(tr("Show volume control"), tab);
    showVolume->setChecked(m_config->showVolume());
    connect(showVolume, &QCheckBox::toggled, m_config, &DockConfig::setShowVolume);
    form->addRow(tr("Volume:"), showVolume);

    auto *showBrightness = new QCheckBox(tr("Show brightness control"), tab);
    showBrightness->setChecked(m_config->showBrightness());
    connect(showBrightness, &QCheckBox::toggled, m_config, &DockConfig::setShowBrightness);
    form->addRow(tr("Brightness:"), showBrightness);

    auto *showBattery = new QCheckBox(tr("Show battery / power profile"), tab);
    showBattery->setChecked(m_config->showBattery());
    connect(showBattery, &QCheckBox::toggled, m_config, &DockConfig::setShowBattery);
    form->addRow(tr("Battery:"), showBattery);

    // NOTE: the "Show clock" checkbox was removed from Settings to save space.
    // config.showClock / the `clock` widget are left intact but dormant — see
    // AGENTS.md "Dormant / UI-unreachable code". (The clock2 widget below still
    // has its own toggle, and the format checkboxes apply to both clocks.)

    auto *clock24h = new QCheckBox(tr("24-hour format"), tab);
    clock24h->setChecked(m_config->clockFormat24h());
    connect(clock24h, &QCheckBox::toggled, m_config, &DockConfig::setClockFormat24h);
    form->addRow(tr("Clock format:"), clock24h);

    auto *clockDate = new QCheckBox(tr("Show date"), tab);
    clockDate->setChecked(m_config->clockShowDate());
    connect(clockDate, &QCheckBox::toggled, m_config, &DockConfig::setClockShowDate);
    form->addRow(tr("Clock date:"), clockDate);

    auto *clockSeconds = new QCheckBox(tr("Show seconds"), tab);
    clockSeconds->setChecked(m_config->clockShowSeconds());
    connect(clockSeconds, &QCheckBox::toggled, m_config, &DockConfig::setClockShowSeconds);
    form->addRow(tr("Clock seconds:"), clockSeconds);

    auto *clockFont = new QSpinBox(tab);
    clockFont->setRange(0, 96);
    clockFont->setSpecialValueText(tr("Automatic"));
    clockFont->setSuffix(tr(" px"));
    clockFont->setValue(m_config->clockFontSize());
    clockFont->setToolTip(tr("Font size of the clock time text. Automatic derives it "
                             "from the widget icon size."));
    connect(clockFont, &QSpinBox::valueChanged, m_config, &DockConfig::setClockFontSize);
    // Mirrored in the Fuentes tab.
    connect(m_config, &DockConfig::clockFontSizeChanged, clockFont,
            [this, clockFont] { clockFont->setValue(m_config->clockFontSize()); });
    form->addRow(tr("Clock font size:"), clockFont);

    // Clock 2's click action: a .desktop id ("kdock-calendar"), a command name
    // on PATH, a full path to a binary, or a path to a .desktop file —
    // ClockWidget2::launch() resolves all four. The "Apps…" button fills the
    // box from the installed applications (same picker as the menu editor) with
    // the chosen app's desktop-file id; "File…" browses the disk for a binary
    // or a .desktop file and fills the box with its full path.
    auto *clock2Row = new QHBoxLayout;
    auto *clock2Command = new QLineEdit(tab);
    clock2Command->setText(m_config->clock2Command());
    clock2Command->setPlaceholderText(QStringLiteral("orage"));
    clock2Command->setToolTip(tr("Command run when left-clicking the Clock 2 widget. "
                                 "Four forms are accepted: a desktop-file id "
                                 "(\"kdock-calendar\"), a command name on PATH, a full "
                                 "path to a binary, or a full path to a .desktop file."));
    connect(clock2Command, &QLineEdit::textEdited, m_config, &DockConfig::setClock2Command);
    auto *clock2Browse = new QPushButton(tr("Apps…"), tab);
    clock2Browse->setToolTip(tr("Choose an installed application; the box is filled "
                                "with its desktop-file id."));
    connect(clock2Browse, &QPushButton::clicked, this, [this, clock2Command] {
        const QList<DesktopEntry> entries = m_apps->all();
        QStringList names;
        for (const DesktopEntry &e : entries)
            names.append(e.name);
        const DesktopEntry current = m_apps->byId(m_config->clock2Command());
        const int currentIdx = qMax(0, names.indexOf(current.name));
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, tr("Clock click app"),
                                                     tr("Application:"), names, currentIdx,
                                                     false, &ok);
        if (!ok)
            return;
        const int idx = names.indexOf(chosen);
        if (idx < 0)
            return;
        clock2Command->setText(entries[idx].id);
        m_config->setClock2Command(entries[idx].id);
    });
    auto *clock2File = new QPushButton(tr("File…"), tab);
    clock2File->setToolTip(tr("Browse the disk for a binary or a .desktop file; the box "
                              "is filled with its full path."));
    connect(clock2File, &QPushButton::clicked, this, [this, clock2Command] {
        const QString chosen = QFileDialog::getOpenFileName(
            this, tr("Choose a binary or .desktop file"),
            QFileInfo(m_config->clock2Command()).isAbsolute()
                ? m_config->clock2Command()
                : QStringLiteral("/usr/bin"),
            tr("Desktop entries (*.desktop);;Executables (*)"));
        if (chosen.isEmpty())
            return;
        clock2Command->setText(chosen);
        m_config->setClock2Command(chosen);
    });
    clock2Row->addWidget(clock2Command, 1);
    clock2Row->addWidget(clock2Browse);
    clock2Row->addWidget(clock2File);
    form->addRow(tr("Clock click app:"), clock2Row);

    auto *showAutohide = new QCheckBox(tr("Show autohide toggle button"), tab);
    showAutohide->setChecked(m_config->showAutohideToggle());
    connect(showAutohide, &QCheckBox::toggled, m_config, &DockConfig::setShowAutohideToggle);
    form->addRow(tr("Autohide button:"), showAutohide);

    auto *showDesktop = new QCheckBox(tr("Show 'show desktop' button"), tab);
    showDesktop->setChecked(m_config->showDesktopButton());
    connect(showDesktop, &QCheckBox::toggled, m_config, &DockConfig::setShowDesktopButton);
    form->addRow(tr("Show desktop:"), showDesktop);

    auto *runningBg = new QCheckBox(tr("Colored background for running apps"), tab);
    runningBg->setChecked(m_config->iconRunningBackground());
    connect(runningBg, &QCheckBox::toggled, m_config, &DockConfig::setIconRunningBackground);
    // Mirrored in the Colores tab.
    connect(m_config, &DockConfig::iconRunningBackgroundChanged, runningBg,
            [this, runningBg] { runningBg->setChecked(m_config->iconRunningBackground()); });
    form->addRow(tr("Running highlight:"), runningBg);

    auto *runningDots = new QCheckBox(tr("Window dots for running apps"), tab);
    runningDots->setChecked(m_config->iconRunningDots());
    connect(runningDots, &QCheckBox::toggled, m_config, &DockConfig::setIconRunningDots);
    connect(m_config, &DockConfig::iconRunningDotsChanged, runningDots,
            [this, runningDots] { runningDots->setChecked(m_config->iconRunningDots()); });
    form->addRow(tr("Running dots:"), runningDots);

    auto *runningLine = new QCheckBox(tr("Edge line for running apps"), tab);
    runningLine->setChecked(m_config->iconRunningLine());
    connect(runningLine, &QCheckBox::toggled, m_config, &DockConfig::setIconRunningLine);
    connect(m_config, &DockConfig::iconRunningLineChanged, runningLine,
            [this, runningLine] { runningLine->setChecked(m_config->iconRunningLine()); });
    form->addRow(tr("Running edge line:"), runningLine);

    // App name next to the icon. The icon keeps its size in every mode; only
    // the cell grows. Also reachable from the icon's right-click menu
    // ("Nombre de la app"), hence the two-way sync below.
    {
        auto *labelMode = new QComboBox(tab);
        labelMode->addItem(tr("Icon only"), int(DockConfig::IconOnly));
        labelMode->addItem(tr("Name over the icon"), int(DockConfig::LabelAbove));
        labelMode->addItem(tr("Name under the icon"), int(DockConfig::LabelBelow));
        labelMode->addItem(tr("Name at the left of the icon"), int(DockConfig::LabelLeft));
        labelMode->addItem(tr("Name at the right of the icon"), int(DockConfig::LabelRight));
        labelMode->addItem(tr("Name only"), int(DockConfig::LabelOnly));
        const int lIdx = labelMode->findData(m_config->iconLabelMode());
        labelMode->setCurrentIndex(lIdx < 0 ? 0 : lIdx);
        labelMode->setToolTip(tr("Show the application name next to its dock icon. "
                                 "Only affects application icons, not widgets."));
        form->addRow(tr("App name:"), labelMode);

        auto *labelWidth = new QSpinBox(tab);
        labelWidth->setRange(60, 400);
        labelWidth->setSingleStep(10);
        labelWidth->setSuffix(tr(" px"));
        labelWidth->setValue(m_config->iconLabelWidth());
        labelWidth->setToolTip(tr("Width of the name: a maximum (the name is elided past "
                                  "it) on a horizontal dock, and the fixed column width on "
                                  "a vertical one."));
        connect(labelWidth, &QSpinBox::valueChanged, m_config, &DockConfig::setIconLabelWidth);
        // Width and font size are mirrored in the Fuentes tab.
        connect(m_config, &DockConfig::iconLabelWidthChanged, labelWidth,
                [this, labelWidth] { labelWidth->setValue(m_config->iconLabelWidth()); });
        form->addRow(tr("· Name width:"), labelWidth);

        auto *labelFont = new QSpinBox(tab);
        labelFont->setRange(0, 48);
        labelFont->setSpecialValueText(tr("Automatic"));
        labelFont->setSuffix(tr(" px"));
        labelFont->setValue(m_config->iconLabelFontSize());
        labelFont->setToolTip(tr("Font size of the application name. Automatic derives it "
                                 "from the icon size."));
        connect(labelFont, &QSpinBox::valueChanged, m_config, &DockConfig::setIconLabelFontSize);
        connect(m_config, &DockConfig::iconLabelFontSizeChanged, labelFont,
                [this, labelFont] { labelFont->setValue(m_config->iconLabelFontSize()); });
        form->addRow(tr("· Name font size:"), labelFont);

        auto *labelBold = new QCheckBox(tr("Negritas"), tab);
        labelBold->setChecked(m_config->labelBold());
        labelBold->setToolTip(tr("Draw every name the dock shows — applications, widgets "
                                 "and the clock date — in bold."));
        connect(labelBold, &QCheckBox::toggled, m_config, &DockConfig::setLabelBold);
        connect(m_config, &DockConfig::labelBoldChanged, labelBold,
                [this, labelBold] { labelBold->setChecked(m_config->labelBold()); });
        form->addRow(tr("· Bold text:"), labelBold);

        // Same for every other section (widgets and blocks): an independent
        // setting, so a dock can name its widgets and not its apps, or the
        // other way round. "Name only" is missing on purpose (a widget without
        // its icon would be unrecognizable). Renaming a section is done in the
        // Layout tab. Also reachable from the right-click submenu "Nombre de
        // los widgets".
        auto *widgetLabelMode = new QComboBox(tab);
        widgetLabelMode->addItem(tr("No name"), int(DockConfig::IconOnly));
        widgetLabelMode->addItem(tr("Name over the icon"), int(DockConfig::LabelAbove));
        widgetLabelMode->addItem(tr("Name under the icon"), int(DockConfig::LabelBelow));
        widgetLabelMode->addItem(tr("Name at the left of the icon"), int(DockConfig::LabelLeft));
        widgetLabelMode->addItem(tr("Name at the right of the icon"), int(DockConfig::LabelRight));
        const int wIdx = widgetLabelMode->findData(m_config->widgetLabelMode());
        widgetLabelMode->setCurrentIndex(wIdx < 0 ? 0 : wIdx);
        widgetLabelMode->setToolTip(tr("Show the name of the dock sections that are not "
                                       "applications (volume, clock, system tray…). Rename "
                                       "them in the Layout tab. Width and font size are "
                                       "shared with the application names."));
        form->addRow(tr("Widget name:"), widgetLabelMode);

        // The width and font size only mean something while some name is shown;
        // the bold toggle stays enabled because it also drives the clock date,
        // which is always text.
        auto syncLabelEnabled = [this, labelWidth, labelFont] {
            const bool on = m_config->iconLabelMode() != DockConfig::IconOnly
                            || m_config->widgetLabelMode() != DockConfig::IconOnly;
            labelWidth->setEnabled(on);
            labelFont->setEnabled(on);
        };
        connect(labelMode, &QComboBox::currentIndexChanged, this, [this, labelMode](int i) {
            m_config->setIconLabelMode(labelMode->itemData(i).toInt());
        });
        connect(m_config, &DockConfig::iconLabelModeChanged, labelMode,
                [this, labelMode, syncLabelEnabled] {
                    const QSignalBlocker block(labelMode);
                    const int i = labelMode->findData(m_config->iconLabelMode());
                    labelMode->setCurrentIndex(i < 0 ? 0 : i);
                    syncLabelEnabled();
                });
        connect(widgetLabelMode, &QComboBox::currentIndexChanged, this,
                [this, widgetLabelMode](int i) {
                    m_config->setWidgetLabelMode(widgetLabelMode->itemData(i).toInt());
                });
        connect(m_config, &DockConfig::widgetLabelModeChanged, widgetLabelMode,
                [this, widgetLabelMode, syncLabelEnabled] {
                    const QSignalBlocker block(widgetLabelMode);
                    const int i = widgetLabelMode->findData(m_config->widgetLabelMode());
                    widgetLabelMode->setCurrentIndex(i < 0 ? 0 : i);
                    syncLabelEnabled();
                });
        syncLabelEnabled();
    }

    // NOTE: application-menu settings moved to their own "Menu" tab
    // (createMenuTab): menu button, icon, size, columns, power row, favorites
    // editor, and the shared-config / shared-favorites toggles.

    auto *showClipboard = new QCheckBox(tr("Show clipboard history button"), tab);
    showClipboard->setChecked(m_config->showClipboard());
    connect(showClipboard, &QCheckBox::toggled, m_config, &DockConfig::setShowClipboard);
    form->addRow(tr("Clipboard:"), showClipboard);

    auto *clipW = new QSpinBox(tab);
    clipW->setRange(240, 1600);
    clipW->setSingleStep(20);
    clipW->setSuffix(tr(" px"));
    clipW->setValue(m_config->clipboardPopupWidth());
    connect(clipW, QOverload<int>::of(&QSpinBox::valueChanged), m_config, &DockConfig::setClipboardPopupWidth);
    form->addRow(tr("Clipboard width:"), clipW);

    auto *clipH = new QSpinBox(tab);
    clipH->setRange(200, 1600);
    clipH->setSingleStep(20);
    clipH->setSuffix(tr(" px"));
    clipH->setValue(m_config->clipboardPopupHeight());
    connect(clipH, QOverload<int>::of(&QSpinBox::valueChanged), m_config, &DockConfig::setClipboardPopupHeight);
    form->addRow(tr("Clipboard height:"), clipH);

    auto *showDisks = new QCheckBox(tr("Show removable disks button (UDisks2)"), tab);
    showDisks->setChecked(m_config->showDisks());
    connect(showDisks, &QCheckBox::toggled, m_config, &DockConfig::setShowDisks);
    form->addRow(tr("Disks:"), showDisks);

    auto *showNetwork = new QCheckBox(tr("Show network button (NetworkManager)"), tab);
    showNetwork->setChecked(m_config->showNetwork());
    connect(showNetwork, &QCheckBox::toggled, m_config, &DockConfig::setShowNetwork);
    form->addRow(tr("Network:"), showNetwork);

    auto *showIconThemes = new QCheckBox(tr("Show icon-theme picker"), tab);
    showIconThemes->setChecked(m_config->showIconThemes());
    showIconThemes->setToolTip(tr("Applies the icon theme to the whole desktop. kdock's own "
                                  "icon theme (above) is left alone: while it is set, this "
                                  "dock keeps its icons and only the rest of KDE follows."));
    connect(showIconThemes, &QCheckBox::toggled, m_config, &DockConfig::setShowIconThemes);
    // Mirrored in the Colores tab.
    connect(m_config, &DockConfig::showIconThemesChanged, showIconThemes,
            [this, showIconThemes] { showIconThemes->setChecked(m_config->showIconThemes()); });
    form->addRow(tr("Icon theme picker:"), showIconThemes);

    auto *showColorSchemes = new QCheckBox(tr("Show color-scheme picker"), tab);
    showColorSchemes->setChecked(m_config->showColorSchemes());
    showColorSchemes->setToolTip(tr("Applies a KDE color scheme system-wide "
                                    "(plasma-apply-colorscheme)."));
    connect(showColorSchemes, &QCheckBox::toggled, m_config, &DockConfig::setShowColorSchemes);
    // Mirrored in the Colores tab.
    connect(m_config, &DockConfig::showColorSchemesChanged, showColorSchemes,
            [this, showColorSchemes] { showColorSchemes->setChecked(m_config->showColorSchemes()); });
    form->addRow(tr("Color scheme picker:"), showColorSchemes);

    auto *showSession = new QCheckBox(tr("Show session/power button (KDE)"), tab);
    showSession->setChecked(m_config->showSessionButton());
    connect(showSession, &QCheckBox::toggled, m_config, &DockConfig::setShowSessionButton);
    form->addRow(tr("Session button:"), showSession);

    auto *showSettings = new QCheckBox(tr("Show kdock settings button"), tab);
    showSettings->setChecked(m_config->showSettingsButton());
    connect(showSettings, &QCheckBox::toggled, m_config, &DockConfig::setShowSettingsButton);
    form->addRow(tr("Settings button:"), showSettings);

    auto *showOverview = new QCheckBox(tr("Show Overview button (KDE)"), tab);
    showOverview->setChecked(m_config->showOverview());
    connect(showOverview, &QCheckBox::toggled, m_config, &DockConfig::setShowOverview);
    form->addRow(tr("Overview:"), showOverview);

    auto *showClock2 = new QCheckBox(tr("Show clock (enhanced tooltip)"), tab);
    showClock2->setChecked(m_config->showClock2());
    connect(showClock2, &QCheckBox::toggled, m_config, &DockConfig::setShowClock2);
    form->addRow(tr("Clock 2:"), showClock2);

    auto *showMoveToDesktop = new QCheckBox(tr("Show move-to-next-desktop button (KDE)"), tab);
    showMoveToDesktop->setChecked(m_config->showMoveToDesktop());
    connect(showMoveToDesktop, &QCheckBox::toggled, m_config, &DockConfig::setShowMoveToDesktop);
    form->addRow(tr("Move to desktop:"), showMoveToDesktop);

    auto *showMoveToScreen = new QCheckBox(tr("Show move-to-next-monitor button (KDE)"), tab);
    showMoveToScreen->setChecked(m_config->showMoveToScreen());
    showMoveToScreen->setToolTip(tr("Right-click moves the window to the previous monitor. "
                                    "Shift+right-click opens the widget menu."));
    connect(showMoveToScreen, &QCheckBox::toggled, m_config, &DockConfig::setShowMoveToScreen);
    form->addRow(tr("Move to monitor:"), showMoveToScreen);

    auto *showMaxMin = new QCheckBox(tr("Show maximize/minimize button (KDE)"), tab);
    showMaxMin->setChecked(m_config->showMaxMin());
    showMaxMin->setToolTip(tr("Acts on the active window: left-click maximizes (KWin toggles, "
                              "so it also restores), right-click minimizes. "
                              "Shift+right-click opens the widget menu."));
    connect(showMaxMin, &QCheckBox::toggled, m_config, &DockConfig::setShowMaxMin);
    form->addRow(tr("MaxMin:"), showMaxMin);

    auto *showCloseWindow = new QCheckBox(tr("Show close-window button"), tab);
    showCloseWindow->setChecked(m_config->showCloseWindow());
    showCloseWindow->setToolTip(tr("Acts on the active window: left-click closes it, "
                                   "right-click sends it to the next virtual desktop while "
                                   "you stay on the current one (KWin only). "
                                   "Shift+right-click opens the widget menu."));
    connect(showCloseWindow, &QCheckBox::toggled, m_config, &DockConfig::setShowCloseWindow);
    form->addRow(tr("Close window:"), showCloseWindow);

    // NOTE: the "Next wallpaper" checkbox was removed from Settings to save
    // space. The widget itself is left intact but is no longer UI-reachable
    // (dormant) — see AGENTS.md "Dormant / UI-unreachable code".

    auto *showDarkMode = new QCheckBox(tr("Show dark-mode button"), tab);
    showDarkMode->setChecked(m_config->showDarkMode());
    showDarkMode->setToolTip(tr("Left-click switches this dock to the normal color "
                                "scheme, right-click to dark mode. Configure the dark "
                                "colors in the DarkMode tab. "
                                "Shift+right-click opens the widget menu."));
    connect(showDarkMode, &QCheckBox::toggled, m_config, &DockConfig::setShowDarkMode);
    form->addRow(tr("Modo oscuro:"), showDarkMode);

    // The tray can live in any dock, but in only one at a time: several docks
    // drawing the same StatusNotifierItems would duplicate every icon and open
    // two menus for one click. While another dock holds it, the checkbox is
    // disabled and says where to go turn it off (the state is recomputed by
    // buildTabs() whenever the edited dock changes).
    auto *showSystray = new QCheckBox(tr("Mostrar la bandeja del sistema"), tab);
    showSystray->setChecked(m_config->showSystray());
    const QString systrayOwner = m_manager ? m_manager->systrayDockId() : QString();
    const bool takenElsewhere = !systrayOwner.isEmpty() && systrayOwner != m_dockId;
    showSystray->setEnabled(!takenElsewhere);
    connect(showSystray, &QCheckBox::toggled, m_config, &DockConfig::setShowSystray);
    form->addRow(tr("System tray:"), showSystray);

    if (takenElsewhere) {
        auto *note = new QLabel(tr("La bandeja ya está activa en \"%1\". Desmarcala ahí "
                                   "para poder usarla acá.").arg(dockLabel(systrayOwner)),
                                tab);
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
        form->addRow(QString(), note);
    }

    auto *systrayScale = new QSpinBox(tab);
    systrayScale->setRange(20, 100);
    systrayScale->setSingleStep(5);
    systrayScale->setSuffix(QStringLiteral("%"));
    systrayScale->setValue(m_config->systrayIconScale());
    systrayScale->setToolTip(tr("Size of system tray icons as a percentage of the base icon size."));
    connect(systrayScale, &QSpinBox::valueChanged, m_config, &DockConfig::setSystrayIconScale);
    form->addRow(tr("Systray icon scale:"), systrayScale);

    auto *group = new QCheckBox(tr("Group windows of the same application"), tab);
    group->setChecked(m_config->groupWindows());
    connect(group, &QCheckBox::toggled, m_config, &DockConfig::setGroupWindows);
    form->addRow(tr("Window grouping:"), group);

    layout->addLayout(form);

    // Pinned applications section
    layout->addWidget(new QLabel(tr("Pinned applications:"), tab));
    m_pinnedList = new QListWidget(tab);
    layout->addWidget(m_pinnedList);

    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Add..."), tab);
    auto *remove = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), tr("Remove"), tab);
    auto *up = new QPushButton(QIcon::fromTheme(QStringLiteral("go-up")), tr("Up"), tab);
    auto *down = new QPushButton(QIcon::fromTheme(QStringLiteral("go-down")), tr("Down"), tab);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    buttons->addWidget(up);
    buttons->addWidget(down);
    layout->addLayout(buttons);

    connect(add, &QPushButton::clicked, this, &SettingsDialog::addPinnedApp);
    connect(remove, &QPushButton::clicked, this, [this] {
        delete m_pinnedList->takeItem(m_pinnedList->currentRow());
        savePinnedList();
    });
    connect(up, &QPushButton::clicked, this, [this] {
        const int row = m_pinnedList->currentRow();
        if (row > 0) {
            m_pinnedList->insertItem(row - 1, m_pinnedList->takeItem(row));
            m_pinnedList->setCurrentRow(row - 1);
            savePinnedList();
        }
    });
    connect(down, &QPushButton::clicked, this, [this] {
        const int row = m_pinnedList->currentRow();
        if (row >= 0 && row < m_pinnedList->count() - 1) {
            m_pinnedList->insertItem(row + 1, m_pinnedList->takeItem(row));
            m_pinnedList->setCurrentRow(row + 1);
            savePinnedList();
        }
    });

    connect(m_config, &DockConfig::pinnedChanged, this, &SettingsDialog::reloadPinnedList);
    reloadPinnedList();

    return tab;
}

QWidget *SettingsDialog::createMenuTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);
    auto *form = new QFormLayout;

    auto *showMenu = new QCheckBox(tr("Show application menu button"), tab);
    showMenu->setChecked(m_config->showMenuButton());
    connect(showMenu, &QCheckBox::toggled, m_config, &DockConfig::setShowMenuButton);
    form->addRow(tr("Application menu:"), showMenu);

    auto *menuIconBtn = new QPushButton(tab);
    menuIconBtn->setStyleSheet(QStringLiteral("text-align:left; padding:4px 8px;"));
    const auto refreshMenuIconBtn = [this, menuIconBtn] {
        const QString n = m_config->menuIcon();
        menuIconBtn->setIcon(QIcon::fromTheme(n));
        menuIconBtn->setText(QStringLiteral(" ") + n);
    };
    refreshMenuIconBtn();
    connect(menuIconBtn, &QPushButton::clicked, this, [this, refreshMenuIconBtn] {
        IconPickerDialog dlg(m_config->menuIcon(), this);
        if (dlg.exec() == QDialog::Accepted && !dlg.selectedIcon().isEmpty()) {
            m_config->setMenuIcon(dlg.selectedIcon());
            refreshMenuIconBtn();
        }
    });
    form->addRow(tr("Menu icon:"), menuIconBtn);
    // Keep the icon button in sync when the value changes elsewhere (e.g. via
    // shared-config propagation from another dock).
    connect(m_config, &DockConfig::menuIconChanged, menuIconBtn, [refreshMenuIconBtn] {
        refreshMenuIconBtn();
    });

    auto *menuW = new QSpinBox(tab);
    menuW->setRange(360, 2400);
    menuW->setSingleStep(20);
    menuW->setSuffix(tr(" px"));
    menuW->setValue(m_config->menuPopupWidth());
    connect(menuW, QOverload<int>::of(&QSpinBox::valueChanged), m_config, &DockConfig::setMenuPopupWidth);
    connect(m_config, &DockConfig::menuPopupWidthChanged, menuW,
            [this, menuW] { menuW->setValue(m_config->menuPopupWidth()); });
    form->addRow(tr("Menu width:"), menuW);

    auto *menuH = new QSpinBox(tab);
    menuH->setRange(300, 1600);
    menuH->setSingleStep(20);
    menuH->setSuffix(tr(" px"));
    menuH->setValue(m_config->menuPopupHeight());
    connect(menuH, QOverload<int>::of(&QSpinBox::valueChanged), m_config, &DockConfig::setMenuPopupHeight);
    connect(m_config, &DockConfig::menuPopupHeightChanged, menuH,
            [this, menuH] { menuH->setValue(m_config->menuPopupHeight()); });
    form->addRow(tr("Menu height:"), menuH);

    auto *menuColumns = new QSpinBox(tab);
    menuColumns->setRange(1, 8);
    menuColumns->setValue(m_config->menuColumns());
    menuColumns->setToolTip(tr("Number of columns for the app and favorites grid. The column "
                               "count is honoured exactly, so raise \"Menu width\" as well when "
                               "using many columns or the cells get too narrow for the names."));
    connect(menuColumns, QOverload<int>::of(&QSpinBox::valueChanged), m_config, &DockConfig::setMenuColumns);
    connect(m_config, &DockConfig::menuColumnsChanged, menuColumns,
            [this, menuColumns] { menuColumns->setValue(m_config->menuColumns()); });
    form->addRow(tr("Menu columns:"), menuColumns);

    auto *menuIconSize = new QSpinBox(tab);
    menuIconSize->setRange(16, 96);
    menuIconSize->setSingleStep(4);
    menuIconSize->setSuffix(tr(" px"));
    menuIconSize->setValue(m_config->menuAppIconSize());
    menuIconSize->setToolTip(tr("Size of the application icons inside the menu, in both the "
                                "single-column list and the multi-column grid."));
    connect(menuIconSize, QOverload<int>::of(&QSpinBox::valueChanged),
            m_config, &DockConfig::setMenuAppIconSize);
    connect(m_config, &DockConfig::menuAppIconSizeChanged, menuIconSize,
            [this, menuIconSize] { menuIconSize->setValue(m_config->menuAppIconSize()); });
    form->addRow(tr("Menu icon size:"), menuIconSize);

    auto *menuSpacing = new QSpinBox(tab);
    menuSpacing->setRange(0, 40);
    menuSpacing->setSuffix(tr(" px"));
    menuSpacing->setValue(m_config->menuGridSpacing());
    menuSpacing->setToolTip(tr("Space around each icon in the multi-column grid. Lower it to "
                               "pack the icons together instead of spreading a few of them "
                               "across the whole menu."));
    connect(menuSpacing, QOverload<int>::of(&QSpinBox::valueChanged),
            m_config, &DockConfig::setMenuGridSpacing);
    connect(m_config, &DockConfig::menuGridSpacingChanged, menuSpacing,
            [this, menuSpacing] { menuSpacing->setValue(m_config->menuGridSpacing()); });
    form->addRow(tr("Menu icon spacing:"), menuSpacing);

    // Application opened by the menu widget's right-click → "Edit menu…". Stored
    // as a .desktop id; AppMenu::launchMenuEditor() falls back to running the
    // value as a plain command.
    auto *menuEditorBtn = new QPushButton(tab);
    menuEditorBtn->setStyleSheet(QStringLiteral("text-align:left; padding:4px 8px;"));
    const auto refreshMenuEditorBtn = [this, menuEditorBtn] {
        const QString id = m_config->menuEditorApp();
        const DesktopEntry e = m_apps->byId(id);
        menuEditorBtn->setIcon(QIcon::fromTheme(e.isValid() && !e.icon.isEmpty()
                                                    ? e.icon : QStringLiteral("kmenuedit")));
        menuEditorBtn->setText(QStringLiteral(" ") + (e.isValid() ? e.name : id));
    };
    refreshMenuEditorBtn();
    connect(menuEditorBtn, &QPushButton::clicked, this, [this, refreshMenuEditorBtn] {
        const QList<DesktopEntry> entries = m_apps->all();
        QStringList names;
        for (const DesktopEntry &e : entries)
            names.append(e.name);
        const DesktopEntry current = m_apps->byId(m_config->menuEditorApp());
        const int currentIdx = qMax(0, names.indexOf(current.name));
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, tr("Menu editor"), tr("Application:"),
                                                     names, currentIdx, false, &ok);
        if (!ok)
            return;
        const int idx = names.indexOf(chosen);
        if (idx < 0)
            return;
        m_config->setMenuEditorApp(entries[idx].id);
        refreshMenuEditorBtn();
    });
    menuEditorBtn->setToolTip(tr("Application opened by the menu widget's right-click → "
                                 "\"Edit menu…\". KDE's menu editor by default."));
    form->addRow(tr("Menu editor:"), menuEditorBtn);
    connect(m_config, &DockConfig::menuEditorAppChanged, menuEditorBtn,
            [refreshMenuEditorBtn] { refreshMenuEditorBtn(); });

    auto *menuPower = new QCheckBox(tr("Power buttons in the application menu (KDE)"), tab);
    menuPower->setChecked(m_config->showMenuPower());
    connect(menuPower, &QCheckBox::toggled, m_config, &DockConfig::setShowMenuPower);
    connect(m_config, &DockConfig::showMenuPowerChanged, menuPower,
            [this, menuPower] { menuPower->setChecked(m_config->showMenuPower()); });
    form->addRow(tr("Menu power row:"), menuPower);

    auto *shareConfig = new QCheckBox(tr("Share menu configuration across all docks/monitors"), tab);
    shareConfig->setChecked(DockConfig::menuConfigShared());
    connect(shareConfig, &QCheckBox::toggled, this, [](bool on) {
        DockConfig::setMenuConfigShared(on);
    });
    form->addRow(tr("Shared menu config:"), shareConfig);

    auto *shareFav = new QCheckBox(tr("Share favorites across all docks/monitors"), tab);
    shareFav->setChecked(DockConfig::favoritesShared());
    connect(shareFav, &QCheckBox::toggled, this, [](bool on) {
        DockConfig::setFavoritesShared(on);
    });
    form->addRow(tr("Shared favorites:"), shareFav);

    layout->addLayout(form);

    layout->addWidget(createTileMenuGroup(tab));

    // Favorites editor (same pattern as the pinned-apps editor).
    layout->addWidget(new QLabel(tr("Menu favorites:"), tab));
    m_favoritesList = new QListWidget(tab);
    layout->addWidget(m_favoritesList);

    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Add..."), tab);
    auto *remove = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), tr("Remove"), tab);
    auto *sortAz = new QPushButton(QIcon::fromTheme(QStringLiteral("view-sort-ascending")),
                                   tr("Sort A-Z"), tab);
    auto *up = new QPushButton(QIcon::fromTheme(QStringLiteral("go-up")), tr("Up"), tab);
    auto *down = new QPushButton(QIcon::fromTheme(QStringLiteral("go-down")), tr("Down"), tab);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addWidget(sortAz);
    buttons->addStretch();
    buttons->addWidget(up);
    buttons->addWidget(down);
    layout->addLayout(buttons);

    connect(add, &QPushButton::clicked, this, [this] {
        const QList<DesktopEntry> entries = m_apps->all();
        QStringList names;
        for (const DesktopEntry &e : entries)
            names.append(e.name);
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, tr("Add favorite"),
                                                     tr("Application:"), names, 0, false, &ok);
        if (!ok)
            return;
        const int idx = names.indexOf(chosen);
        if (idx < 0)
            return;
        // Skip if already present.
        for (int i = 0; i < m_favoritesList->count(); ++i) {
            if (m_favoritesList->item(i)->data(Qt::UserRole).toString() == entries[idx].id)
                return;
        }
        auto *item = new QListWidgetItem(QIcon::fromTheme(entries[idx].icon), entries[idx].name,
                                          m_favoritesList);
        item->setData(Qt::UserRole, entries[idx].id);
        saveFavoritesList();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        delete m_favoritesList->takeItem(m_favoritesList->currentRow());
        saveFavoritesList();
    });
    connect(up, &QPushButton::clicked, this, [this] {
        const int row = m_favoritesList->currentRow();
        if (row > 0) {
            m_favoritesList->insertItem(row - 1, m_favoritesList->takeItem(row));
            m_favoritesList->setCurrentRow(row - 1);
            saveFavoritesList();
        }
    });
    connect(down, &QPushButton::clicked, this, [this] {
        const int row = m_favoritesList->currentRow();
        if (row >= 0 && row < m_favoritesList->count() - 1) {
            m_favoritesList->insertItem(row + 1, m_favoritesList->takeItem(row));
            m_favoritesList->setCurrentRow(row + 1);
            saveFavoritesList();
        }
    });
    connect(sortAz, &QPushButton::clicked, this, [this] {
        m_favoritesList->sortItems(Qt::AscendingOrder); // by display name
        saveFavoritesList();
    });

    connect(m_config, &DockConfig::menuFavoritesChanged, this, &SettingsDialog::reloadFavoritesList);
    reloadFavoritesList();

    return tab;
}

// Full audio mixer, backed by AudioControl (pactl). Output/input devices offer a
// default-device radio, a mute button and a 0–100/150% volume slider; the
// Applications section lists per-stream volume. Rows are rebuilt live from
// AudioControl::changed(). Invoked directly by the dock's volume-widget
// right-click (DockWindow::openAudioSettings -> showAudioTab).
QWidget *SettingsDialog::createTileMenuGroup(QWidget *parent)
{
    // A group inside the Menu tab rather than a twelfth tab: eleven titles
    // already ask for 1086 px of tab bar and one more drops it into scroll-arrow
    // mode without warning (see AGENTS.md). Everything about how the tile menu
    // looks lives in its own panel anyway — kdock only owns the widget.
    auto *box = new QGroupBox(tr("Menú de mosaicos (pantalla completa)"), parent);
    auto *layout = new QVBoxLayout(box);

    auto *info = new QLabel(
        tr("Un menú de aplicaciones que ocupa todo el escritorio libre (todo menos los "
           "docks y paneles visibles), con los íconos en una grilla que podés reacomodar "
           "arrastrándolos. Es un binario aparte, kdock-tilemenu, con su propia "
           "configuración: el botón de abajo abre su panel."),
        box);
    info->setWordWrap(true);
    layout->addWidget(info);

    if (!TileMenuLauncher::installed()) {
        auto *missing = new QLabel(tr("kdock-tilemenu no está instalado."), box);
        missing->setStyleSheet(QStringLiteral("color: gray;"));
        layout->addWidget(missing);
        return box;
    }

    auto *form = new QFormLayout;

    auto *showTile = new QCheckBox(tr("Mostrar el botón en este dock"), box);
    showTile->setChecked(m_config->showTileMenu());
    connect(showTile, &QCheckBox::toggled, m_config, &DockConfig::setShowTileMenu);
    connect(m_config, &DockConfig::showTileMenuChanged, showTile,
            [this, showTile] { showTile->setChecked(m_config->showTileMenu()); });
    form->addRow(tr("Widget:"), showTile);

    auto *iconBtn = new QPushButton(box);
    iconBtn->setStyleSheet(QStringLiteral("text-align:left; padding:4px 8px;"));
    const auto refreshIcon = [this, iconBtn] {
        const QString n = m_config->tileMenuIcon();
        iconBtn->setIcon(QIcon::fromTheme(n));
        iconBtn->setText(QStringLiteral(" ") + n);
    };
    refreshIcon();
    connect(iconBtn, &QPushButton::clicked, this, [this, refreshIcon] {
        IconPickerDialog dlg(m_config->tileMenuIcon(), this);
        if (dlg.exec() == QDialog::Accepted && !dlg.selectedIcon().isEmpty()) {
            m_config->setTileMenuIcon(dlg.selectedIcon());
            refreshIcon();
        }
    });
    form->addRow(tr("Ícono del widget:"), iconBtn);

    auto *preload = new QCheckBox(tr("Dejarlo cargado al iniciar kdock"), box);
    preload->setChecked(TileMenuLauncher::preload());
    preload->setToolTip(tr("Sin esto, el proceso arranca en el primer clic (medio segundo) y "
                           "queda residente: las aperturas siguientes son instantáneas."));
    connect(preload, &QCheckBox::toggled, this, [](bool on) {
        TileMenuLauncher::setPreload(on);
    });
    form->addRow(tr("Precargar:"), preload);

    layout->addLayout(form);

    auto *row = new QHBoxLayout;
    auto *configureBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")),
                                         tr("Configurar…"), box);
    row->addWidget(configureBtn);
    row->addStretch();
    layout->addLayout(row);

    auto *status = new QLabel(box);
    status->setWordWrap(true);
    layout->addWidget(status);

    const auto refreshStatus = [status] {
        status->setText(TileMenuLauncher::running()
                            ? tr("Estado: en ejecución (%1)").arg(TileMenuLauncher::binaryPath())
                            : tr("Estado: detenido (%1)").arg(TileMenuLauncher::binaryPath()));
    };
    refreshStatus();
    connect(configureBtn, &QPushButton::clicked, this, [this, refreshStatus] {
        if (!m_tileLauncher)
            m_tileLauncher = new TileMenuLauncher(this);
        m_tileLauncher->openSettings();
        QTimer::singleShot(600, this, refreshStatus);
    });

    // Bound to `box`, so the timer dies when buildTabs() deletes the tab.
    auto *poll = new QTimer(box);
    poll->setInterval(2000);
    connect(poll, &QTimer::timeout, box, refreshStatus);
    poll->start();

    return box;
}

QWidget *SettingsDialog::createAudioTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    // >100% ceiling toggle, shared with the dock volume widget via QSettings.
    auto *maxVol = new QCheckBox(tr("Raise maximum volume (up to 150%)"), tab);
    maxVol->setChecked(m_audio && m_audio->maxVolume());
    connect(maxVol, &QCheckBox::toggled, this, [this](bool on) {
        if (m_audio)
            m_audio->setMaxVolume(on);
    });
    layout->addWidget(maxVol);

    const auto makeSection = [tab, layout](const QString &title, QGroupBox *&groupOut,
                                           QVBoxLayout *&innerOut) {
        auto *group = new QGroupBox(title, tab);
        auto *v = new QVBoxLayout(group);
        v->setSpacing(4);
        groupOut = group;
        innerOut = v;
        layout->addWidget(group);
    };

    makeSection(tr("Output devices"), m_audioOutGroup, m_audioOutLayout);
    makeSection(tr("Input devices"), m_audioInGroup, m_audioInLayout);
    makeSection(tr("Applications"), m_audioAppGroup, m_audioAppLayout);
    layout->addStretch(1);

    rebuildAudioTab();

    if (m_audio) {
        // Bound to `tab`, so the connection dies when buildTabs() deletes it.
        // Queued (never immediate): a live change often arrives while we're
        // inside a row widget's signal, and rebuilding deletes that widget.
        connect(m_audio, &AudioControl::changed, tab, [this] { scheduleAudioRebuild(); });
    }
    return tab;
}

void SettingsDialog::scheduleAudioRebuild()
{
    if (m_audioRebuildQueued)
        return;
    m_audioRebuildQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_audioRebuildQueued = false;
        rebuildAudioTab();
    });
}

void SettingsDialog::rebuildAudioTab()
{
    if (!m_audio || !m_audioOutLayout)
        return;
    if (m_audioSliderDown)
        return; // don't yank a slider handle out from under the user mid-drag

    const int ceiling = m_audio->maxVolume() ? 150 : 100;

    const auto clearLayout = [](QVBoxLayout *lay) {
        while (QLayoutItem *item = lay->takeAt(0)) {
            delete item->widget();
            delete item;
        }
    };

    const auto populate = [&](QVBoxLayout *lay, QGroupBox *group,
                              const QVector<AudioControl::Device> &devices, bool withDefault) {
        clearLayout(lay);
        QButtonGroup *bg = withDefault ? new QButtonGroup(group) : nullptr;
        for (const AudioControl::Device &d : devices) {
            const AudioControl::DeviceType type = d.type;
            const int index = d.index;
            const QString name = d.name;

            auto *row = new QWidget(group);
            auto *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(6);

            if (withDefault) {
                auto *def = new QRadioButton(row);
                def->setChecked(d.isDefault);
                def->setToolTip(tr("Use as default device"));
                bg->addButton(def);
                connect(def, &QRadioButton::clicked, this, [this, type, name] {
                    if (m_audio)
                        m_audio->setDefault(type, name);
                });
                h->addWidget(def);
            } else {
                h->addSpacing(20);
            }

            auto *mute = new QToolButton(row);
            mute->setCheckable(true);
            mute->setAutoRaise(true);
            mute->setChecked(d.muted);
            mute->setIcon(QIcon::fromTheme(d.muted ? QStringLiteral("audio-volume-muted")
                                                   : d.iconName));
            mute->setToolTip(tr("Toggle mute"));
            connect(mute, &QToolButton::clicked, this, [this, type, index](bool checked) {
                if (m_audio)
                    m_audio->setMuted(type, index, checked);
            });
            h->addWidget(mute);

            auto *label = new QLabel(d.description, row);
            label->setToolTip(d.description);
            label->setMinimumWidth(150);
            h->addWidget(label, 1);

            auto *slider = new QSlider(Qt::Horizontal, row);
            slider->setRange(0, ceiling);
            {
                QSignalBlocker blk(slider);
                slider->setValue(qRound(d.volume * 100.0));
            }
            slider->setMinimumWidth(150);

            auto *pct = new QLabel(row);
            pct->setMinimumWidth(46);
            pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pct->setText(QString::number(qRound(d.volume * 100.0)) + QStringLiteral(" %"));

            connect(slider, &QSlider::sliderPressed, this, [this] { m_audioSliderDown = true; });
            connect(slider, &QSlider::sliderReleased, this, [this] {
                m_audioSliderDown = false;
                // Deferred: we're inside this slider's own signal; a direct
                // rebuild would delete it under our feet.
                scheduleAudioRebuild();
            });
            connect(slider, &QSlider::valueChanged, this, [this, type, index, pct](int v) {
                pct->setText(QString::number(v) + QStringLiteral(" %"));
                if (m_audio)
                    m_audio->setVolume(type, index, v / 100.0);
            });
            h->addWidget(slider);
            h->addWidget(pct);

            lay->addWidget(row);
        }
        group->setVisible(!devices.isEmpty());
    };

    populate(m_audioOutLayout, m_audioOutGroup, m_audio->outputs(), true);
    populate(m_audioInLayout, m_audioInGroup, m_audio->inputs(), true);
    populate(m_audioAppLayout, m_audioAppGroup, m_audio->apps(), false);
}

void SettingsDialog::showAudioTab()
{
    if (m_audioTabIndex >= 0 && m_audioTabIndex < m_tabWidget->count())
        m_tabWidget->setCurrentIndex(m_audioTabIndex);
}

void SettingsDialog::showNetworkTab()
{
    if (m_networkTabIndex >= 0 && m_networkTabIndex < m_tabWidget->count())
        m_tabWidget->setCurrentIndex(m_networkTabIndex);
}

void SettingsDialog::showMonitorsTab(const QString &dockId)
{
    // Switch to the Monitores tab and select the requested dock row, so the
    // user lands on the exact line they asked for (right-click → Dock → Nombre).
    if (m_monitorsTabIndex >= 0 && m_monitorsTabIndex < m_tabWidget->count())
        m_tabWidget->setCurrentIndex(m_monitorsTabIndex);
    if (!m_docksList || !m_manager)
        return;
    for (int i = 0; i < m_docksList->count(); ++i) {
        if (m_docksList->item(i)->data(Qt::UserRole).toString() == dockId) {
            m_docksList->setCurrentRow(i);
            break;
        }
    }
}

// Devices and saved connections (NetworkManager). All the work is in
// NetworkSettingsWidget; this only hands it a tab.
QWidget *SettingsDialog::createNetworkTab()
{
    return new NetworkSettingsWidget;
}

QString SettingsDialog::sectionLabel(const QString &token)
{
    // Single source of truth for the section names: DockConfig also draws them
    // in the dock, and the Layout tab can rename them (see widgetName()).
    return DockConfig::defaultWidgetLabel(token);
}

QString SettingsDialog::dockLabel(const QString &dockId)
{
    // The alias, when set, replaces the default "<screen> — Dock <n>" name.
    QString name;
    {
        QSettings s(DockConfig::instanceSettingsFilePath(dockId), QSettings::IniFormat);
        const QString alias = s.value(QStringLiteral("alias")).toString().trimmed();
        if (!alias.isEmpty())
            name = alias;
    }
    if (name.isEmpty())
        name = tr("%1 — Dock %2")
            .arg(DockConfig::screenOfDockId(dockId))
            .arg(DockConfig::slotOfDockId(dockId) + 1);
    return name;
}

std::function<void()> SettingsDialog::makeColorButton(QPushButton *btn, QColor (*get)(),
                                                      void (*set)(const QColor &),
                                                      const QString &title)
{
    // Swatch button that opens QColorDialog and paints itself with the result.
    // Returns the refresh lambda so the caller can re-run it on config signals
    // (the dark colors are app-wide statics edited from several tabs).
    const auto refresh = [btn, get] {
        const QColor c = get();
        btn->setText(c.name(QColor::HexRgb).toUpper());
        btn->setStyleSheet(QStringLiteral(
            "background-color:%1; color:%2; padding:4px 12px; border:1px solid gray;")
            .arg(c.name(), c.lightnessF() > 0.5 ? QStringLiteral("black")
                                                : QStringLiteral("white")));
    };
    refresh();
    connect(btn, &QPushButton::clicked, this, [this, get, set, title, refresh] {
        const QColor c = QColorDialog::getColor(get(), this, title);
        if (c.isValid()) {
            set(c);
            refresh();
        }
    });
    return refresh;
}

void SettingsDialog::addDarkAppearanceExtras(QFormLayout *form, QWidget *parent)
{
    // The two system-wide rows lead with a do-nothing entry (empty id); the
    // dock's own row does not, because there an empty id already means
    // something else ("no override, follow KDE").
    const QPair<QString, QString> noChange{QString(), tr("(no cambiar)")};

    if (m_appearance) {
        QList<QPair<QString, QString>> schemes{noChange};
        const QVariantList list = m_appearance->colorSchemes();
        for (const QVariant &v : list) {
            const QVariantMap m = v.toMap();
            schemes.append({m.value(QStringLiteral("id")).toString(),
                            m.value(QStringLiteral("name")).toString()});
        }
        addDarkAppearanceExtrasRow(form, parent, DockConfig::SystemColorScheme,
                                   tr("El esquema de color del sistema"),
                                   tr("Aplica el esquema de color de KDE, igual que el widget "
                                      "«Esquema de color» (plasma-apply-colorscheme)."),
                                   schemes, m_appearance->currentColorScheme());
    }

    QList<QPair<QString, QString>> icons;
    for (const auto &[name, id] : Theme::availableIconThemes()) {
        // Some shipped index.theme files keep the packaging placeholder
        // ("@ThemeName@") in Name=; the directory name is the honest fallback.
        // Same rule as AppearanceControl::iconThemes().
        icons.append({id, (name.isEmpty() || name.contains(QLatin1Char('@'))) ? id : name});
    }
    if (m_appearance) {
        QList<QPair<QString, QString>> systemIcons{noChange};
        systemIcons.append(icons);
        addDarkAppearanceExtrasRow(form, parent, DockConfig::SystemIconTheme,
                                   tr("El iconset del sistema"),
                                   tr("Aplica el iconset de KDE, igual que el widget «Iconset» "
                                      "(plasma-changeicons). Afecta a todo el escritorio."),
                                   systemIcons, m_appearance->currentIconTheme());
    }
    {
        // The dock's own override, where "no override" is a real choice.
        QList<QPair<QString, QString>> dockIcons = icons;
        dockIcons.prepend({QString(), tr("(seguir el del sistema)")});
        addDarkAppearanceExtrasRow(form, parent, DockConfig::DockIconTheme,
                                   tr("El iconset del dock"),
                                   tr("Solo el iconset que usa kdock, sin tocar el del escritorio "
                                      "(Configuración → General → «Iconset del dock»)."),
                                   dockIcons, m_theme ? m_theme->iconTheme() : QString());
    }
}

void SettingsDialog::addDarkAppearanceExtrasRow(QFormLayout *form, QWidget *parent, int item,
                                                const QString &title, const QString &tip,
                                                const QList<QPair<QString, QString>> &choices,
                                                const QString &liveValue)
{
    // With no match the combo would sit on whatever sorts first and quietly
    // apply *that* — this desktop has no General/ColorScheme key, so the
    // "normal" side started out pointing at "Arc". An explicit do-nothing
    // entry is the safe landing spot; both apply*() calls no-op on "".
    auto *check = new QCheckBox(title, parent);
    check->setChecked(DockConfig::darkAppearanceEnabled(item));
    check->setToolTip(tip);

    auto *darkCombo = new QComboBox(parent);
    auto *normalCombo = new QComboBox(parent);
    for (const auto &[id, name] : choices) {
        darkCombo->addItem(name, id);
        normalCombo->addItem(name, id);
    }
    const auto select = [](QComboBox *combo, const QString &id) {
        const int i = combo->findData(id);
        combo->setCurrentIndex(i < 0 ? 0 : i);
    };
    // Seed "normal" from what the system is using right now, but only while
    // the mode is off — reading it with dark applied would save the dark
    // value as the thing to restore.
    const auto reselect = [check, darkCombo, normalCombo, item, select] {
        const QSignalBlocker b1(check);
        const QSignalBlocker b2(darkCombo);
        const QSignalBlocker b3(normalCombo);
        check->setChecked(DockConfig::darkAppearanceEnabled(item));
        select(darkCombo, DockConfig::darkAppearanceValue(item, true));
        select(normalCombo, DockConfig::darkAppearanceValue(item, false));
        darkCombo->setEnabled(check->isChecked());
        normalCombo->setEnabled(check->isChecked());
    };
    select(darkCombo, DockConfig::darkAppearanceValue(item, true));
    QString normal = DockConfig::darkAppearanceValue(item, false);
    if (normal.isEmpty() && !DockConfig::darkAppearanceApplied())
        normal = liveValue;
    select(normalCombo, normal);

    connect(check, &QCheckBox::toggled, this, [this, item, darkCombo, normalCombo](bool on) {
        // Persist both combos on enable: the seeded "normal" is only in the
        // widget until something writes it, and it is what the restore uses.
        if (on) {
            DockConfig::setDarkAppearanceValue(item, true, darkCombo->currentData().toString());
            DockConfig::setDarkAppearanceValue(item, false,
                                               normalCombo->currentData().toString());
        }
        DockConfig::setDarkAppearanceEnabled(item, on);
        darkCombo->setEnabled(on);
        normalCombo->setEnabled(on);
    });
    connect(darkCombo, &QComboBox::currentIndexChanged, this, [item, darkCombo] {
        DockConfig::setDarkAppearanceValue(item, true, darkCombo->currentData().toString());
    });
    connect(normalCombo, &QComboBox::currentIndexChanged, this, [item, normalCombo] {
        DockConfig::setDarkAppearanceValue(item, false, normalCombo->currentData().toString());
    });
    // The values are app-wide statics reachable from the DarkMode and Colores
    // tabs (and another dock's dialog); re-read them all whenever the dark-mode
    // group changes anywhere.
    connect(m_config, &DockConfig::darkModeChanged, check, reselect);
    darkCombo->setEnabled(check->isChecked());
    normalCombo->setEnabled(check->isChecked());

    form->addRow(check);
    form->addRow(tr("· En modo oscuro:"), darkCombo);
    form->addRow(tr("· En modo normal:"), normalCombo);
}

QWidget *SettingsDialog::createDarkModeTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);
    auto *form = new QFormLayout;

    auto *intro = new QLabel(
        tr("El modo oscuro <b>reemplaza</b> los colores del dock mientras está activo: "
           "no toca la configuración que ya tenés. Al volver a Normal reaparece tal cual "
           "estaba (color de fondo, resaltado de íconos, todo)."), tab);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // Reads and writes the *effective* mode through setDarkModeActive(), the
    // same call the "Modo" submenu and the darkmode widget use — the three
    // controls are one switch with three faces. Showing the dock's own flag
    // here instead made the box read "on" while the dock was drawing normal
    // (bug 2026-08-02). Its label follows the scope for the same reason.
    auto *thisDock = new QCheckBox(tab);
    thisDock->setChecked(m_config->darkModeActive());
    connect(thisDock, &QCheckBox::toggled, m_config, &DockConfig::setDarkModeActive);
    form->addRow(tr("Modo oscuro:"), thisDock);

    auto *allDocks = new QCheckBox(tr("Aplicar a todos los docks"), tab);
    allDocks->setChecked(DockConfig::darkModeAllDocks());
    allDocks->setToolTip(tr("Mientras esté tildado, prender o apagar el modo oscuro "
                            "—desde acá, desde el submenú «Modo» o desde el widget— vale "
                            "para todos los docks. Las excepciones se editan abajo."));
    form->addRow(tr("Alcance:"), allDocks);

    // --- The two colors of the dark scheme (app-wide, not per dock) ---
    // Same button+swatch idiom as the panel color in the General tab (and the
    // copies in the Colores tab), via the shared makeColorButton helper.
    auto *accentBtn = new QPushButton(tab);
    const auto refreshAccent = makeColorButton(accentBtn, &DockConfig::darkAccentColor,
                                               &DockConfig::setDarkAccentColor,
                                               tr("Color de resaltado"));
    accentBtn->setToolTip(tr("Color de los nombres de apps y widgets, y del resaltado de "
                             "las apps que están corriendo (que en modo oscuro deja de "
                             "usar el color de cada ícono y pasa a este único color)."));
    auto *accentRow = new QHBoxLayout;
    auto *accentReset = new QPushButton(tr("Breeze Dark"), tab);
    connect(accentReset, &QPushButton::clicked, this, [refreshAccent] {
        DockConfig::setDarkAccentColor(QColor(QString::fromLatin1(DockConfig::kDarkAccentDefault)));
        refreshAccent();
    });
    accentRow->addWidget(accentBtn, 1);
    accentRow->addWidget(accentReset);
    form->addRow(tr("Color de resaltado:"), accentRow);

    auto *bgBtn = new QPushButton(tab);
    const auto refreshBg = makeColorButton(bgBtn, &DockConfig::darkBackgroundColor,
                                           &DockConfig::setDarkBackgroundColor,
                                           tr("Fondo del dock"));
    bgBtn->setToolTip(tr("Fondo del dock en modo oscuro. La transparencia configurada en "
                         "la solapa General se sigue aplicando igual."));
    auto *bgRow = new QHBoxLayout;
    auto *bgReset = new QPushButton(tr("Breeze Dark"), tab);
    connect(bgReset, &QPushButton::clicked, this, [refreshBg] {
        DockConfig::setDarkBackgroundColor(
            QColor(QString::fromLatin1(DockConfig::kDarkBackgroundDefault)));
        refreshBg();
    });
    bgRow->addWidget(bgBtn, 1);
    bgRow->addWidget(bgReset);
    form->addRow(tr("Fondo del dock:"), bgRow);
    // Both colors are mirrored in the Colores tab (and are app-wide statics),
    // so re-read them whenever the dark-mode group changes anywhere.
    connect(m_config, &DockConfig::darkModeChanged, tab, refreshAccent);
    connect(m_config, &DockConfig::darkModeChanged, tab, refreshBg);

    layout->addLayout(form);

    // --- Optional system-wide side effects ---
    // Each row is a checkbox plus the value for each mode. Two combos and not
    // one because these are global state, not a read-time override like the
    // dock's colors: there is no "previous value" to fall back to (this desktop
    // has no General/ColorScheme key at all), so what to restore is a setting.
    auto *extrasBox = new QGroupBox(tr("Al cambiar de modo, cambiar también:"), tab);
    auto *extrasForm = new QFormLayout(extrasBox);
    extrasForm->addRow(new QLabel(tr("<i>Esto sí toca la configuración del escritorio, "
                                     "no solo el dibujo del dock — por eso cada opción "
                                     "lleva el valor de los dos modos.</i>"), extrasBox));
    addDarkAppearanceExtras(extrasForm, extrasBox);
    layout->addWidget(extrasBox);

    // --- Exceptions ---
    auto *exceptionsLabel = new QLabel(tr("Docks exceptuados (quedan en Normal):"), tab);
    layout->addWidget(exceptionsLabel);
    auto *exceptions = new QListWidget(tab);
    exceptions->setMinimumHeight(140);
    const QStringList excepted = DockConfig::darkModeExceptions();
    for (const QString &id : DockConfig::knownDocks()) {
        const QString screen = DockConfig::screenOfDockId(id);
        const int slot = DockConfig::slotOfDockId(id);
        auto *item = new QListWidgetItem(tr("%1 — Dock %2").arg(screen).arg(slot + 1), exceptions);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(excepted.contains(id) ? Qt::Checked : Qt::Unchecked);
    }
    connect(exceptions, &QListWidget::itemChanged, this, [exceptions] {
        QStringList ids;
        for (int i = 0; i < exceptions->count(); ++i) {
            QListWidgetItem *item = exceptions->item(i);
            if (item->checkState() == Qt::Checked)
                ids << item->data(Qt::UserRole).toString();
        }
        DockConfig::setDarkModeExceptions(ids);
    });
    layout->addWidget(exceptions);

    // One resync for the whole tab. Everything here is reachable from somewhere
    // else too (the submenu, the widget, another dock's copy of this dialog),
    // and the three controls overlap, so they are all redrawn from the config
    // rather than from each other. Blocked while writing: the exception list
    // itself is what setDarkModeExceptions() changes.
    const auto syncAll = [this, thisDock, allDocks, exceptions, exceptionsLabel] {
        const bool all = DockConfig::darkModeAllDocks();
        const QSignalBlocker blockThis(thisDock);
        const QSignalBlocker blockAll(allDocks);
        const QSignalBlocker blockList(exceptions);
        thisDock->setChecked(m_config->darkModeActive());
        thisDock->setText(all ? tr("Activado en todos los docks")
                              : tr("Activado en este dock"));
        allDocks->setChecked(all);
        const QStringList excepted = DockConfig::darkModeExceptions();
        for (int i = 0; i < exceptions->count(); ++i) {
            QListWidgetItem *item = exceptions->item(i);
            item->setCheckState(excepted.contains(item->data(Qt::UserRole).toString())
                                    ? Qt::Checked : Qt::Unchecked);
        }
        exceptions->setEnabled(all);
        exceptionsLabel->setEnabled(all);
    };
    connect(allDocks, &QCheckBox::toggled, this, [syncAll](bool on) {
        DockConfig::setDarkModeAllDocks(on);
        syncAll();
    });
    connect(m_config, &DockConfig::darkModeChanged, tab, syncAll);
    syncAll();

    layout->addStretch();
    return tab;
}

QWidget *SettingsDialog::createLayoutTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    layout->addWidget(new QLabel(
        tr("Order of dock sections. Use the buttons to reorder, add or remove\n"
           "separators. A static separator is a fixed gap; a dynamic one (spring)\n"
           "pushes the following sections toward the far end in panel mode."), tab));

    m_layoutList = new QListWidget(tab);
    // Two lists share the tab; the section one has many more rows, so it gets
    // the larger share of whatever height is going.
    layout->addWidget(m_layoutList, 2);

    auto *buttons = new QHBoxLayout;
    auto *addSep = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")),
                                   tr("Add separator"), tab);
    addSep->setToolTip(tr("Fixed gap of \"Separator size\" px between two sections."));
    auto *addSpring = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")),
                                      tr("Add dynamic separator"), tab);
    auto *remove = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")),
                                   tr("Remove separator"), tab);
    auto *rename = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-rename")),
                                   tr("Rename..."), tab);
    rename->setToolTip(tr("Name shown for this section in the dock (see the "
                          "\"Widget name\" setting in Appearance). Leave the field "
                          "empty to restore the default name."));
    auto *up = new QPushButton(QIcon::fromTheme(QStringLiteral("go-up")), tr("Up"), tab);
    auto *down = new QPushButton(QIcon::fromTheme(QStringLiteral("go-down")), tr("Down"), tab);
    buttons->addWidget(addSep);
    buttons->addWidget(addSpring);
    buttons->addWidget(remove);
    buttons->addWidget(rename);
    buttons->addStretch();
    buttons->addWidget(up);
    buttons->addWidget(down);
    layout->addLayout(buttons);

    // The list can hide some tokens (see reloadLayoutList), so a list row is not
    // necessarily the same index as in config.widgetOrder. Each item carries its
    // own index in Qt::UserRole + 1. Looking it up by token instead would break
    // on "spring": that token repeats, so indexOf() always found the *first*
    // separator and Up/Down/Remove acted on the wrong row.
    auto orderIndexOfRow = [this](int row) -> int {
        QListWidgetItem *it = (row >= 0) ? m_layoutList->item(row) : nullptr;
        return it ? it->data(Qt::UserRole + 1).toInt() : -1;
    };

    // Remove is only enabled when a separator (of either kind) is selected;
    // renaming is the other way round (a separator draws no name).
    auto updateRemove = [this, remove, rename] {
        QListWidgetItem *it = m_layoutList->currentItem();
        const bool sep = it && DockConfig::isRepeatableToken(it->data(Qt::UserRole).toString());
        remove->setEnabled(sep);
        rename->setEnabled(it && !sep);
    };
    connect(m_layoutList, &QListWidget::currentRowChanged, this, [updateRemove](int) { updateRemove(); });

    connect(rename, &QPushButton::clicked, this, [this] {
        QListWidgetItem *it = m_layoutList->currentItem();
        if (!it)
            return;
        const QString token = it->data(Qt::UserRole).toString();
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Rename section"),
            tr("Name for \"%1\" (empty = default):").arg(sectionLabel(token)),
            QLineEdit::Normal, m_config->widgetName(token), &ok);
        if (ok)
            m_config->setWidgetName(token, name);
    });

    connect(addSpring, &QPushButton::clicked, this, [this, orderIndexOfRow] {
        const int row = m_layoutList->currentRow();
        const int oi = orderIndexOfRow(row);
        const int at = oi >= 0 ? oi + 1 : m_config->widgetOrder().size();
        m_config->insertSpring(at);
        m_layoutList->setCurrentRow(row + 1);
    });
    connect(addSep, &QPushButton::clicked, this, [this, orderIndexOfRow] {
        const int row = m_layoutList->currentRow();
        const int oi = orderIndexOfRow(row);
        const int at = oi >= 0 ? oi + 1 : m_config->widgetOrder().size();
        m_config->insertSeparator(at);
        m_layoutList->setCurrentRow(row + 1);
    });
    connect(remove, &QPushButton::clicked, this, [this, orderIndexOfRow] {
        const int oi = orderIndexOfRow(m_layoutList->currentRow());
        if (oi >= 0)
            m_config->removeSectionAt(oi);
    });
    // Up/Down move the item to the neighbouring *visible* row's index, so a
    // hidden token in between is jumped over. QList::move() keeps the relative
    // order of everything else, so nothing but the selected row changes place.
    // The setCurrentRow() calls run after moveSection(), which reloads the list
    // synchronously through widgetOrderChanged: keep that order or the
    // selection stops following the moved row.
    connect(up, &QPushButton::clicked, this, [this, orderIndexOfRow] {
        const int row = m_layoutList->currentRow();
        if (row > 0) {
            const int from = orderIndexOfRow(row), to = orderIndexOfRow(row - 1);
            if (from >= 0 && to >= 0) {
                m_config->moveSection(from, to);
                m_layoutList->setCurrentRow(row - 1);
            }
        }
    });
    connect(down, &QPushButton::clicked, this, [this, orderIndexOfRow] {
        const int row = m_layoutList->currentRow();
        if (row >= 0 && row < m_layoutList->count() - 1) {
            const int from = orderIndexOfRow(row), to = orderIndexOfRow(row + 1);
            if (from >= 0 && to >= 0) {
                m_config->moveSection(from, to);
                m_layoutList->setCurrentRow(row + 1);
            }
        }
    });

    connect(m_config, &DockConfig::widgetOrderChanged, this, [this, updateRemove] {
        reloadLayoutList();
        updateRemove();
    });
    connect(m_config, &DockConfig::widgetNamesChanged, this, [this] { reloadLayoutList(); });
    reloadLayoutList();
    updateRemove();

    // ---- Separators inside the apps block -------------------------------
    // These are not sections: DockModel draws them *between app icons*, so they
    // are placed by index (DockConfig::separator1/separator2). The list below
    // shows the pinned launchers with those two separators where they land, so
    // the index never has to be typed (it used to be a spinbox in General).
    auto *line = new QLabel(tab);
    line->setFrameStyle(QFrame::HLine | QFrame::Sunken);
    layout->addWidget(line);

    layout->addWidget(new QLabel(
        tr("Separators inside the applications block (up to two). They split the\n"
           "pinned launchers; a separator at the end also splits them from the\n"
           "windows that are merely running."), tab));

    m_appSepList = new QListWidget(tab);
    layout->addWidget(m_appSepList, 1);
    m_appSepSelected = 0; // buildTabs() rebuilds this list for another dock

    auto *sepButtons = new QHBoxLayout;
    auto *addAppSep = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")),
                                      tr("Add separator"), tab);
    auto *removeAppSep = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")),
                                         tr("Remove separator"), tab);
    auto *sepUp = new QPushButton(QIcon::fromTheme(QStringLiteral("go-up")), tr("Up"), tab);
    auto *sepDown = new QPushButton(QIcon::fromTheme(QStringLiteral("go-down")), tr("Down"), tab);
    sepButtons->addWidget(addAppSep);
    sepButtons->addWidget(removeAppSep);
    sepButtons->addStretch();
    sepButtons->addWidget(sepUp);
    sepButtons->addWidget(sepDown);
    layout->addLayout(sepButtons);

    auto updateAppSepButtons = [this, addAppSep, removeAppSep, sepUp, sepDown] {
        const bool room = appSeparatorPos(1) < 0 || appSeparatorPos(2) < 0;
        const bool onSep = m_appSepSelected != 0;
        addAppSep->setEnabled(room);
        removeAppSep->setEnabled(onSep);
        sepUp->setEnabled(onSep);
        sepDown->setEnabled(onSep);
    };
    connect(m_appSepList, &QListWidget::currentRowChanged, this,
            [this, updateAppSepButtons](int row) {
                QListWidgetItem *it = row >= 0 ? m_appSepList->item(row) : nullptr;
                m_appSepSelected = it ? it->data(Qt::UserRole).toInt() : 0;
                updateAppSepButtons();
            });

    connect(addAppSep, &QPushButton::clicked, this, [this] {
        if (appSeparatorPos(1) >= 0 && appSeparatorPos(2) >= 0)
            return;
        const int which = appSeparatorPos(1) < 0 ? 1 : 2;
        // Insert before the selected launcher, or at the end of the pinned ones.
        QListWidgetItem *it = m_appSepList->currentItem();
        int pos = it ? it->data(Qt::UserRole + 1).toInt() : m_config->pinned().size();
        if (pos == appSeparatorPos(which == 1 ? 2 : 1))
            ++pos; // never stack the two on the same index
        m_appSepSelected = which; // the new separator is what the buttons act on
        setAppSeparatorPos(which, pos);
    });
    connect(removeAppSep, &QPushButton::clicked, this, [this] {
        if (const int which = m_appSepSelected) {
            m_appSepSelected = 0;
            setAppSeparatorPos(which, -1);
        }
    });
    // Up/Down shift the separator one launcher at a time, jumping over the
    // other separator so the two never share an index (they would then be
    // indistinguishable, and DockModel would draw them back to back).
    auto shift = [this](int delta) {
        const int which = m_appSepSelected;
        if (!which)
            return;
        const int other = appSeparatorPos(which == 1 ? 2 : 1);
        int pos = appSeparatorPos(which) + delta;
        if (pos == other)
            pos += delta;
        setAppSeparatorPos(which, qBound(0, pos, m_config->pinned().size()));
    };
    connect(sepUp, &QPushButton::clicked, this, [shift] { shift(-1); });
    connect(sepDown, &QPushButton::clicked, this, [shift] { shift(1); });

    auto *sizeForm = new QFormLayout;
    auto *sepSize = new QSpinBox(tab);
    sepSize->setRange(4, 64);
    sepSize->setSuffix(tr(" px"));
    sepSize->setValue(m_config->separatorSize());
    sepSize->setToolTip(tr("Size of every static separator: both the ones inside the "
                           "applications block and the \"Static separator\" sections."));
    connect(sepSize, &QSpinBox::valueChanged, m_config, &DockConfig::setSeparatorSize);
    connect(m_config, &DockConfig::separatorSizeChanged, sepSize,
            [this, sepSize] { sepSize->setValue(m_config->separatorSize()); });
    sizeForm->addRow(tr("Separator size:"), sepSize);
    layout->addLayout(sizeForm);

    for (auto signal : {&DockConfig::separator1Changed, &DockConfig::separator2Changed,
                        &DockConfig::pinnedChanged}) {
        connect(m_config, signal, this, [this, updateAppSepButtons] {
            reloadAppSeparatorList();
            updateAppSepButtons();
        });
    }
    reloadAppSeparatorList();
    updateAppSepButtons();

    return tab;
}

int SettingsDialog::appSeparatorPos(int which) const
{
    return which == 1 ? m_config->separator1() : m_config->separator2();
}

void SettingsDialog::setAppSeparatorPos(int which, int pos)
{
    if (which == 1)
        m_config->setSeparator1(pos);
    else
        m_config->setSeparator2(pos);
}

void SettingsDialog::reloadAppSeparatorList()
{
    // Keep the selection on the same separator across the rebuild. Snapshot it
    // first: clear() and setCurrentItem() both emit currentRowChanged, which
    // writes m_appSepSelected, so it is restored at the end.
    const int selected = m_appSepSelected;
    m_appSepList->clear();

    const QStringList pinned = m_config->pinned();
    const auto addSeparatorsAt = [this, selected](int pos) {
        for (int which = 1; which <= 2; ++which) {
            if (appSeparatorPos(which) != pos)
                continue;
            auto *item = new QListWidgetItem(
                QIcon::fromTheme(QStringLiteral("distribute-vertical-margin")),
                tr("── Separator %1 ──").arg(which), m_appSepList);
            item->setData(Qt::UserRole, which);
            item->setData(Qt::UserRole + 1, pos);
            if (which == selected)
                m_appSepList->setCurrentItem(item);
        }
    };

    for (int i = 0; i < pinned.size(); ++i) {
        addSeparatorsAt(i);
        const DesktopEntry entry = m_apps->byId(pinned.at(i));
        auto *item = new QListWidgetItem(
            QIcon::fromTheme(entry.isValid() ? entry.icon
                                             : QStringLiteral("application-x-executable")),
            entry.isValid() ? entry.name : pinned.at(i), m_appSepList);
        item->setData(Qt::UserRole, 0);
        item->setData(Qt::UserRole + 1, i);
    }
    // A separator can also sit past the last launcher: that is where it splits
    // the pinned icons from the merely running windows.
    addSeparatorsAt(pinned.size());
    // Indexes further out land among windows this dialog cannot enumerate;
    // list them at the end instead of dropping them (they would look "off").
    QList<int> beyond;
    for (int which = 1; which <= 2; ++which) {
        const int pos = appSeparatorPos(which);
        if (pos > pinned.size() && !beyond.contains(pos))
            beyond.append(pos);
    }
    std::sort(beyond.begin(), beyond.end());
    for (int pos : std::as_const(beyond))
        addSeparatorsAt(pos);

    m_appSepSelected = selected;
}

void SettingsDialog::reloadLayoutList()
{
    const int prev = m_layoutList->currentRow();
    m_layoutList->clear();
    const QStringList order = m_config->widgetOrder();
    // Hidden from the Layout tab (their Settings checkboxes were removed; kept in
    // widgetOrder for compatibility). See AGENTS.md "Dormant / UI-unreachable".
    static const QStringList kHiddenFromLayout = {
        QStringLiteral("clock"), QStringLiteral("nextwallpaper")};
    int springNumber = 0;
    int sepNumber = 0;
    for (int i = 0; i < order.size(); ++i) {
        const QString token = order.at(i);
        if (kHiddenFromLayout.contains(token))
            continue;
        const bool spring = token == QLatin1String("spring");
        const bool separator = DockConfig::isRepeatableToken(token);
        // Separators are numbered (each kind on its own count): they are
        // otherwise indistinguishable, so there is no way to tell that Up/Down
        // moved the one that was selected. Renamed sections keep the default
        // name in parentheses, or the list stops saying which widget a row is.
        const QString name = m_config->widgetName(token);
        QString shown;
        if (separator)
            shown = tr("%1 %2").arg(sectionLabel(token))
                        .arg(spring ? ++springNumber : ++sepNumber);
        else if (name == sectionLabel(token))
            shown = sectionLabel(token);
        else
            shown = tr("%1 (%2)").arg(name, sectionLabel(token));
        // The apps block keeps its place in the order while switched off, so
        // the row has to say it is not being drawn.
        if (token == QLatin1String("apps") && !m_config->showAppIcons())
            shown += tr("  (oculto)");
        auto *item = new QListWidgetItem(
            QIcon::fromTheme(!separator ? QStringLiteral("view-list-symbolic")
                             : spring   ? QStringLiteral("distribute-horizontal-margin")
                                        : QStringLiteral("distribute-vertical-margin")),
            shown, m_layoutList);
        item->setData(Qt::UserRole, token);
        item->setData(Qt::UserRole + 1, i); // index in widgetOrder, springs included
    }
    if (prev >= 0 && prev < m_layoutList->count())
        m_layoutList->setCurrentRow(prev);
}

QWidget *SettingsDialog::createRelanzadoresTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    // Top: relanzadores list + add/remove
    layout->addWidget(new QLabel(tr("Relanzadores (nested dock groups):"), tab));
    m_relanzadoresList = new QListWidget(tab);
    layout->addWidget(m_relanzadoresList);

    auto *relButtons = new QHBoxLayout;
    auto *addRel = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Add..."), tab);
    auto *removeRel = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), tr("Remove"), tab);
    auto *iconRel = new QPushButton(QIcon::fromTheme(QStringLiteral("preferences-desktop-icons")), tr("Icon..."), tab);
    iconRel->setEnabled(false);
    relButtons->addWidget(addRel);
    relButtons->addWidget(removeRel);
    relButtons->addWidget(iconRel);
    relButtons->addStretch();
    layout->addLayout(relButtons);

    // Separator
    auto *sep = new QLabel(tab);
    sep->setFrameStyle(QFrame::HLine | QFrame::Sunken);
    layout->addWidget(sep);

    // Bottom: apps in selected relanzador
    layout->addWidget(new QLabel(tr("Apps in selected relanzador:"), tab));
    m_relanzadorAppsList = new QListWidget(tab);
    m_relanzadorAppsList->setEnabled(false);
    layout->addWidget(m_relanzadorAppsList);

    auto *appButtons = new QHBoxLayout;
    auto *addApp = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Add app..."), tab);
    auto *removeApp = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), tr("Remove app"), tab);
    auto *upApp = new QPushButton(QIcon::fromTheme(QStringLiteral("go-up")), tr("Up"), tab);
    auto *downApp = new QPushButton(QIcon::fromTheme(QStringLiteral("go-down")), tr("Down"), tab);
    appButtons->addWidget(addApp);
    appButtons->addWidget(removeApp);
    appButtons->addStretch();
    appButtons->addWidget(upApp);
    appButtons->addWidget(downApp);
    layout->addLayout(appButtons);

    // --- Connections ---
    connect(addRel, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString title = QInputDialog::getText(this, tr("New Relanzador"),
                                                     tr("Title:"), QLineEdit::Normal,
                                                     QString(), &ok);
        if (!ok || title.isEmpty())
            return;
        m_relanzadores->createRelanzador(title);
        reloadRelanzadoresList();
    });

    connect(removeRel, &QPushButton::clicked, this, [this]() {
        int row = m_relanzadoresList->currentRow();
        if (row < 0)
            return;
        const QString id = m_relanzadoresList->item(row)->data(Qt::UserRole).toString();
        m_relanzadores->removeRelanzador(id);
        if (m_selectedRelanzadorId == id) {
            m_selectedRelanzadorId.clear();
            m_relanzadorAppsList->clear();
            m_relanzadorAppsList->setEnabled(false);
        }
        reloadRelanzadoresList();
    });

    connect(m_relanzadoresList, &QListWidget::currentRowChanged, this, [this, iconRel](int row) {
        if (row < 0) {
            m_selectedRelanzadorId.clear();
            m_relanzadorAppsList->clear();
            m_relanzadorAppsList->setEnabled(false);
            iconRel->setEnabled(false);
            return;
        }
        m_selectedRelanzadorId = m_relanzadoresList->item(row)->data(Qt::UserRole).toString();
        m_relanzadorAppsList->setEnabled(true);
        iconRel->setEnabled(true);
        reloadRelanzadorApps();
    });

    connect(iconRel, &QPushButton::clicked, this, [this]() {
        if (m_selectedRelanzadorId.isEmpty()) return;
        RelanzadorConfig *cfg = m_relanzadores->get(m_selectedRelanzadorId);
        if (!cfg) return;
        IconPickerDialog dlg(cfg->iconName(), this);
        if (dlg.exec() == QDialog::Accepted && !dlg.selectedIcon().isEmpty()) {
            cfg->setIconName(dlg.selectedIcon());
            reloadRelanzadoresList();
        }
    });

    connect(addApp, &QPushButton::clicked, this, [this]() {
        if (m_selectedRelanzadorId.isEmpty()) return;
        RelanzadorConfig *cfg = m_relanzadores->get(m_selectedRelanzadorId);
        if (!cfg) return;
        const QList<DesktopEntry> entries = m_apps->all();
        QStringList names;
        for (const DesktopEntry &e : entries) names.append(e.name);
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, tr("Add app"),
            tr("Application:"), names, 0, false, &ok);
        if (!ok) return;
        const int idx = names.indexOf(chosen);
        if (idx < 0) return;
        QStringList pinned = cfg->pinned();
        if (!pinned.contains(entries[idx].id)) {
            pinned.append(entries[idx].id);
            cfg->setPinned(pinned);
        }
        reloadRelanzadorApps();
    });

    connect(removeApp, &QPushButton::clicked, this, [this]() {
        if (m_selectedRelanzadorId.isEmpty()) return;
        RelanzadorConfig *cfg = m_relanzadores->get(m_selectedRelanzadorId);
        if (!cfg) return;
        int row = m_relanzadorAppsList->currentRow();
        if (row < 0) return;
        const QString appId = m_relanzadorAppsList->item(row)->data(Qt::UserRole).toString();
        QStringList pinned = cfg->pinned();
        pinned.removeAll(appId);
        cfg->setPinned(pinned);
        reloadRelanzadorApps();
    });

    connect(upApp, &QPushButton::clicked, this, [this]() {
        if (m_selectedRelanzadorId.isEmpty()) return;
        RelanzadorConfig *cfg = m_relanzadores->get(m_selectedRelanzadorId);
        if (!cfg) return;
        const int row = m_relanzadorAppsList->currentRow();
        if (row <= 0) return;
        QStringList pinned = cfg->pinned();
        pinned.move(row, row - 1);
        cfg->setPinned(pinned);
        reloadRelanzadorApps();
        m_relanzadorAppsList->setCurrentRow(row - 1);
    });

    connect(downApp, &QPushButton::clicked, this, [this]() {
        if (m_selectedRelanzadorId.isEmpty()) return;
        RelanzadorConfig *cfg = m_relanzadores->get(m_selectedRelanzadorId);
        if (!cfg) return;
        const int row = m_relanzadorAppsList->currentRow();
        QStringList pinned = cfg->pinned();
        if (row < 0 || row >= pinned.size() - 1) return;
        pinned.move(row, row + 1);
        cfg->setPinned(pinned);
        reloadRelanzadorApps();
        m_relanzadorAppsList->setCurrentRow(row + 1);
    });

    connect(m_relanzadores, &RelanzadoresManager::itemsChanged,
            this, &SettingsDialog::reloadRelanzadoresList);

    // Per-dock visibility: the checkbox on each row toggles whether the
    // relanzador shows on the currently-edited dock (m_config). The primary
    // dock stores a hidden list (default all shown); others a shown list.
    connect(m_relanzadoresList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (!m_config)
            return;
        const QString id = item->data(Qt::UserRole).toString();
        const bool checked = item->checkState() == Qt::Checked;
        const bool primary = !m_manager || m_manager->primaryDockId() == m_dockId;
        if (primary) {
            QStringList hidden = m_config->relanzadoresHidden();
            if (checked)
                hidden.removeAll(id);
            else if (!hidden.contains(id))
                hidden.append(id);
            m_config->setRelanzadoresHidden(hidden);
        } else {
            QStringList shown = m_config->relanzadoresShown();
            if (checked) {
                if (!shown.contains(id))
                    shown.append(id);
            } else {
                shown.removeAll(id);
            }
            m_config->setRelanzadoresShown(shown);
        }
    });

    reloadRelanzadoresList();

    return tab;
}

void SettingsDialog::reloadPinnedList()
{
    m_pinnedList->clear();
    for (const QString &id : m_config->pinned()) {
        const DesktopEntry entry = m_apps->byId(id);
        auto *item = new QListWidgetItem(
            QIcon::fromTheme(entry.isValid() ? entry.icon : QStringLiteral("application-x-executable")),
            entry.isValid() ? entry.name : id, m_pinnedList);
        item->setData(Qt::UserRole, id);
    }
}

void SettingsDialog::savePinnedList()
{
    QStringList pinned;
    for (int i = 0; i < m_pinnedList->count(); ++i)
        pinned.append(m_pinnedList->item(i)->data(Qt::UserRole).toString());

    // Avoid rebuilding the list widget from our own change
    disconnect(m_config, &DockConfig::pinnedChanged, this, &SettingsDialog::reloadPinnedList);
    m_config->setPinned(pinned);
    connect(m_config, &DockConfig::pinnedChanged, this, &SettingsDialog::reloadPinnedList);
}

void SettingsDialog::addPinnedApp()
{
    const QList<DesktopEntry> entries = m_apps->all();
    QStringList names;
    for (const DesktopEntry &e : entries)
        names.append(e.name);

    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, tr("Pin application"),
                                                  tr("Application:"), names, 0, false, &ok);
    if (!ok)
        return;
    const int idx = names.indexOf(chosen);
    if (idx < 0)
        return;

    auto *item = new QListWidgetItem(QIcon::fromTheme(entries[idx].icon), entries[idx].name,
                                      m_pinnedList);
    item->setData(Qt::UserRole, entries[idx].id);
    savePinnedList();
}

void SettingsDialog::reloadFavoritesList()
{
    if (!m_favoritesList)
        return;
    m_favoritesList->clear();
    for (const QString &id : m_config->menuFavorites()) {
        const DesktopEntry entry = m_apps->byId(id);
        auto *item = new QListWidgetItem(
            QIcon::fromTheme(entry.isValid() ? entry.icon : QStringLiteral("application-x-executable")),
            entry.isValid() ? entry.name : id, m_favoritesList);
        item->setData(Qt::UserRole, id);
    }
}

void SettingsDialog::saveFavoritesList()
{
    QStringList favs;
    for (int i = 0; i < m_favoritesList->count(); ++i)
        favs.append(m_favoritesList->item(i)->data(Qt::UserRole).toString());

    // Avoid rebuilding the list widget from our own change.
    disconnect(m_config, &DockConfig::menuFavoritesChanged, this, &SettingsDialog::reloadFavoritesList);
    m_config->setMenuFavorites(favs);
    connect(m_config, &DockConfig::menuFavoritesChanged, this, &SettingsDialog::reloadFavoritesList);
}

void SettingsDialog::reloadRelanzadoresList()
{
    // Block itemChanged so setting the check states below doesn't write config.
    const QSignalBlocker blocker(m_relanzadoresList);
    m_relanzadoresList->clear();
    if (!m_relanzadores)
        return;

    // Check state reflects the currently-edited dock: primary shows all but its
    // hidden list; others show only their shown list.
    const bool primary = !m_manager || m_manager->primaryDockId() == m_dockId;
    const QStringList hidden = m_config ? m_config->relanzadoresHidden() : QStringList();
    const QStringList shown = m_config ? m_config->relanzadoresShown() : QStringList();

    const QStringList allIds = m_relanzadores->ids();
    int selectedRow = -1;
    for (const QString &id : allIds) {
        RelanzadorConfig *cfg = m_relanzadores->get(id);
        if (!cfg)
            continue;
        auto *item = new QListWidgetItem(
            QIcon::fromTheme(cfg->iconName()),
            cfg->title(), m_relanzadoresList);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool visible = primary ? !hidden.contains(id) : shown.contains(id);
        item->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
        if (id == m_selectedRelanzadorId)
            selectedRow = m_relanzadoresList->count() - 1;
    }
    // Keep the previously-selected relanzador highlighted across reloads (the
    // signal blocker above prevents this from re-triggering reloadRelanzadorApps).
    if (selectedRow >= 0)
        m_relanzadoresList->setCurrentRow(selectedRow);
}

void SettingsDialog::reloadRelanzadorApps()
{
    m_relanzadorAppsList->clear();
    if (m_selectedRelanzadorId.isEmpty())
        return;
    RelanzadorConfig *cfg = m_relanzadores->get(m_selectedRelanzadorId);
    if (!cfg)
        return;
    for (const QString &id : cfg->pinned()) {
        const DesktopEntry entry = m_apps->byId(id);
        auto *item = new QListWidgetItem(
            QIcon::fromTheme(entry.isValid() ? entry.icon : QStringLiteral("application-x-executable")),
            entry.isValid() ? entry.name : id, m_relanzadorAppsList);
        item->setData(Qt::UserRole, id);
    }
}

QWidget *SettingsDialog::createScriptRunnersTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    // Top: script runners list (checkbox = shown on the current dock) + add/remove.
    layout->addWidget(new QLabel(tr("Script Runners (check to show on this dock):"), tab));
    m_scriptRunnersList = new QListWidget(tab);
    layout->addWidget(m_scriptRunnersList);

    auto *buttons = new QHBoxLayout;
    auto *addBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Add..."), tab);
    auto *removeBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), tr("Remove"), tab);
    buttons->addWidget(addBtn);
    buttons->addWidget(removeBtn);
    buttons->addStretch();
    layout->addLayout(buttons);

    auto *sep = new QLabel(tab);
    sep->setFrameStyle(QFrame::HLine | QFrame::Sunken);
    layout->addWidget(sep);

    // Bottom: editor for the selected runner.
    auto *form = new QFormLayout;
    m_scriptRunnerTitle = new QLineEdit(tab);
    m_scriptRunnerIconButton = new QPushButton(tab);
    m_scriptRunnerIconButton->setStyleSheet(QStringLiteral("text-align:left; padding:4px 8px;"));
    m_scriptRunnerPath = new QLineEdit(tab);
    m_scriptRunnerPath->setPlaceholderText(tr("Ruta al archivo del script (se ejecuta con: sh <archivo>)"));
    m_scriptRunnerBrowse = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")),
                                           tr("Examinar…"), tab);
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(m_scriptRunnerPath, 1);
    pathRow->addWidget(m_scriptRunnerBrowse);
    form->addRow(tr("Title:"), m_scriptRunnerTitle);
    form->addRow(tr("Icon:"), m_scriptRunnerIconButton);
    form->addRow(tr("Script:"), pathRow);
    layout->addLayout(form);

    // --- Connections ---
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString title = QInputDialog::getText(this, tr("New Script Runner"),
                                                     tr("Title:"), QLineEdit::Normal,
                                                     QString(), &ok);
        if (!ok || title.isEmpty())
            return;
        m_scriptRunners->createScriptRunner(title);
        reloadScriptRunnersList();
    });

    connect(removeBtn, &QPushButton::clicked, this, [this]() {
        int row = m_scriptRunnersList->currentRow();
        if (row < 0)
            return;
        const QString id = m_scriptRunnersList->item(row)->data(Qt::UserRole).toString();
        m_scriptRunners->removeScriptRunner(id);
        if (m_selectedScriptRunnerId == id)
            m_selectedScriptRunnerId.clear();
        reloadScriptRunnersList();
        reloadScriptRunnerEditor();
    });

    connect(m_scriptRunnersList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_selectedScriptRunnerId = row < 0
            ? QString()
            : m_scriptRunnersList->item(row)->data(Qt::UserRole).toString();
        reloadScriptRunnerEditor();
    });

    // Live edits to the selected runner.
    connect(m_scriptRunnerTitle, &QLineEdit::textEdited, this, [this](const QString &t) {
        if (auto *cfg = m_scriptRunners->get(m_selectedScriptRunnerId)) {
            cfg->setTitle(t);
            if (auto *item = m_scriptRunnersList->currentItem())
                item->setText(t);
        }
    });
    connect(m_scriptRunnerIconButton, &QPushButton::clicked, this, [this]() {
        auto *cfg = m_scriptRunners->get(m_selectedScriptRunnerId);
        if (!cfg)
            return;
        IconPickerDialog dlg(cfg->iconName(), this);
        if (dlg.exec() != QDialog::Accepted || dlg.selectedIcon().isEmpty())
            return;
        const QString name = dlg.selectedIcon();
        cfg->setIconName(name);
        m_scriptRunnerIconButton->setIcon(QIcon::fromTheme(name));
        m_scriptRunnerIconButton->setText(QStringLiteral(" ") + name);
        if (auto *item = m_scriptRunnersList->currentItem())
            item->setIcon(QIcon::fromTheme(name));
    });
    connect(m_scriptRunnerPath, &QLineEdit::textEdited, this, [this](const QString &t) {
        if (auto *cfg = m_scriptRunners->get(m_selectedScriptRunnerId))
            cfg->setScriptPath(t);
    });
    connect(m_scriptRunnerBrowse, &QPushButton::clicked, this, [this]() {
        auto *cfg = m_scriptRunners->get(m_selectedScriptRunnerId);
        if (!cfg)
            return;
        const QString current = cfg->scriptPath();
        const QString startDir = current.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
            : QFileInfo(current).absolutePath();
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Elegir script"), startDir,
            tr("Scripts (*.sh *.bash *.py *.pl);;Todos los archivos (*)"));
        if (path.isEmpty())
            return;
        cfg->setScriptPath(path);
        m_scriptRunnerPath->setText(path);
    });

    connect(m_scriptRunners, &ScriptRunnersManager::itemsChanged,
            this, &SettingsDialog::reloadScriptRunnersList);

    // Per-dock visibility: same scheme as relanzadores (primary → hidden list,
    // others → shown list).
    connect(m_scriptRunnersList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (!m_config)
            return;
        const QString id = item->data(Qt::UserRole).toString();
        const bool checked = item->checkState() == Qt::Checked;
        const bool primary = !m_manager || m_manager->primaryDockId() == m_dockId;
        if (primary) {
            QStringList hidden = m_config->scriptRunnersHidden();
            if (checked)
                hidden.removeAll(id);
            else if (!hidden.contains(id))
                hidden.append(id);
            m_config->setScriptRunnersHidden(hidden);
        } else {
            QStringList shown = m_config->scriptRunnersShown();
            if (checked) {
                if (!shown.contains(id))
                    shown.append(id);
            } else {
                shown.removeAll(id);
            }
            m_config->setScriptRunnersShown(shown);
        }
    });

    reloadScriptRunnersList();
    reloadScriptRunnerEditor();

    return tab;
}

void SettingsDialog::reloadScriptRunnersList()
{
    const QSignalBlocker blocker(m_scriptRunnersList);
    m_scriptRunnersList->clear();
    if (!m_scriptRunners)
        return;

    const bool primary = !m_manager || m_manager->primaryDockId() == m_dockId;
    const QStringList hidden = m_config ? m_config->scriptRunnersHidden() : QStringList();
    const QStringList shown = m_config ? m_config->scriptRunnersShown() : QStringList();

    for (const QString &id : m_scriptRunners->ids()) {
        ScriptRunnerConfig *cfg = m_scriptRunners->get(id);
        if (!cfg)
            continue;
        auto *item = new QListWidgetItem(
            QIcon::fromTheme(cfg->iconName()), cfg->title(), m_scriptRunnersList);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool visible = primary ? !hidden.contains(id) : shown.contains(id);
        item->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
    }
}

void SettingsDialog::reloadScriptRunnerEditor()
{
    ScriptRunnerConfig *cfg = m_scriptRunners ? m_scriptRunners->get(m_selectedScriptRunnerId) : nullptr;
    const bool enabled = cfg != nullptr;
    m_scriptRunnerTitle->setEnabled(enabled);
    m_scriptRunnerIconButton->setEnabled(enabled);
    m_scriptRunnerPath->setEnabled(enabled);
    m_scriptRunnerBrowse->setEnabled(enabled);

    // Block signals so setting the fields doesn't write back / recurse.
    const QSignalBlocker b1(m_scriptRunnerTitle);
    const QSignalBlocker b3(m_scriptRunnerPath);
    m_scriptRunnerTitle->setText(cfg ? cfg->title() : QString());
    m_scriptRunnerPath->setText(cfg ? cfg->scriptPath() : QString());

    const QString iconName = cfg ? cfg->iconName() : QString();
    m_scriptRunnerIconButton->setIcon(iconName.isEmpty() ? QIcon()
                                                         : QIcon::fromTheme(iconName));
    m_scriptRunnerIconButton->setText(iconName.isEmpty() ? tr(" Choose icon…")
                                                        : QStringLiteral(" ") + iconName);
}

QWidget *SettingsDialog::createBackupTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    auto *info = new QLabel(
        tr("Export the complete kdock configuration (all docks, plus relanzadores "
           "and script runners including their scripts) to a .zip file, or import "
           "a previously exported one.\n\nImporting replaces the current "
           "configuration (a backup is kept) and restarts kdock."),
        tab);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *buttons = new QHBoxLayout;
    auto *exportBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-export")),
                                      tr("Export…"), tab);
    auto *importBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-import")),
                                      tr("Import…"), tab);
    buttons->addWidget(exportBtn);
    buttons->addWidget(importBtn);
    buttons->addStretch();
    layout->addLayout(buttons);

    // Favorites-only export/import (a small JSON list of .desktop ids), separate
    // from the full config backup above.
    auto *favGroup = new QGroupBox(tr("Menu favorites"), tab);
    auto *favLayout = new QVBoxLayout(favGroup);
    auto *favInfo = new QLabel(
        tr("Export or import just the application-menu favorites list as a .json "
           "file. Importing replaces the current favorites."),
        favGroup);
    favInfo->setWordWrap(true);
    favLayout->addWidget(favInfo);

    auto *favButtons = new QHBoxLayout;
    auto *favExportBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-export")),
                                         tr("Export favorites…"), favGroup);
    auto *favImportBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-import")),
                                         tr("Import favorites…"), favGroup);
    favButtons->addWidget(favExportBtn);
    favButtons->addWidget(favImportBtn);
    favButtons->addStretch();
    favLayout->addLayout(favButtons);
    layout->addWidget(favGroup);

    layout->addStretch();

    connect(favExportBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        QString path = QFileDialog::getSaveFileName(
            this, tr("Export favorites"),
            dir + QStringLiteral("/kdock-favorites.json"), tr("JSON files (*.json)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
            path += QStringLiteral(".json");
        QString err;
        if (ConfigArchive::exportFavorites(path, m_config->menuFavorites(), &err))
            QMessageBox::information(this, tr("Export favorites"),
                                     tr("Favorites exported to:\n%1").arg(path));
        else
            QMessageBox::warning(this, tr("Export failed"), err);
    });

    connect(favImportBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import favorites"), dir, tr("JSON files (*.json)"));
        if (path.isEmpty())
            return;
        QStringList favs;
        QString err;
        if (!ConfigArchive::importFavorites(path, &favs, &err)) {
            QMessageBox::warning(this, tr("Import failed"), err);
            return;
        }
        if (QMessageBox::question(
                this, tr("Import favorites"),
                tr("Replace the current menu favorites with %n imported item(s)?",
                   nullptr, favs.size()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
            return;
        m_config->setMenuFavorites(favs);
    });

    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        QString path = QFileDialog::getSaveFileName(
            this, tr("Export configuration"),
            dir + QStringLiteral("/kdock-config.zip"), tr("Zip archives (*.zip)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
            path += QStringLiteral(".zip");
        QString err;
        if (ConfigArchive::exportTo(path, &err))
            QMessageBox::information(this, tr("Export"),
                                     tr("Configuration exported to:\n%1").arg(path));
        else
            QMessageBox::warning(this, tr("Export failed"), err);
    });

    connect(importBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import configuration"), dir, tr("Zip archives (*.zip)"));
        if (path.isEmpty())
            return;
        if (QMessageBox::warning(
                this, tr("Import configuration"),
                tr("This will replace your current kdock configuration and restart "
                   "kdock. A backup of the current configuration is kept.\n\nContinue?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QString err;
        if (!ConfigArchive::importFrom(path, &err)) {
            QMessageBox::warning(this, tr("Import failed"), err);
            return;
        }
        // Relaunch kdock so every dock/manager reloads the imported config.
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
        QCoreApplication::quit();
    });

    return tab;
}

QWidget *SettingsDialog::createPreviewsTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    if (!m_previewsLauncher)
        m_previewsLauncher = new PreviewsLauncher(this);

    auto *info = new QLabel(
        tr("El Dock Preview es un binario aparte (kdock-previews) que corre junto a "
           "kdock y muestra una tira con la vista previa de cada ventana abierta, "
           "clickeable para activarla. Tiene su propia configuración y su propio "
           "manejo multimonitor: el botón de abajo abre su panel."),
        tab);
    info->setWordWrap(true);
    layout->addWidget(info);

    m_previewsEnabled = new QCheckBox(tr("Activar Dock Preview"), tab);
    m_previewsEnabled->setChecked(PreviewsLauncher::enabled());
    layout->addWidget(m_previewsEnabled);

    auto *row = new QHBoxLayout;
    auto *configureBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")),
                                         tr("Configurar…"), tab);
    row->addWidget(configureBtn);
    row->addStretch();
    layout->addLayout(row);

    m_previewsStatus = new QLabel(tab);
    m_previewsStatus->setWordWrap(true);
    layout->addWidget(m_previewsStatus);

    auto *note = new QLabel(
        tr("Si las tarjetas muestran solo el ícono de la aplicación en vez de la "
           "ventana, falta el privilegio de captura: kdock-previews.desktop tiene que "
           "estar instalado (con X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2 "
           "y el Exec= apuntando al binario real)."),
        tab);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(note);

    layout->addStretch();

    const auto refreshStatus = [this] {
        if (!m_previewsStatus)
            return;
        m_previewsStatus->setText(PreviewsLauncher::running()
                                      ? tr("Estado: en ejecución (%1)")
                                            .arg(PreviewsLauncher::binaryPath())
                                      : tr("Estado: detenido (%1)")
                                            .arg(PreviewsLauncher::binaryPath()));
    };
    refreshStatus();

    connect(m_previewsEnabled, &QCheckBox::toggled, this, [this, refreshStatus](bool on) {
        m_previewsLauncher->setEnabled(on);
        // The process needs a moment to claim (or release) the bus name.
        QTimer::singleShot(600, this, refreshStatus);
    });
    connect(configureBtn, &QPushButton::clicked, this, [this, refreshStatus] {
        m_previewsLauncher->openSettings();
        QTimer::singleShot(600, this, refreshStatus);
    });

    // Bound to `tab`, so the timer dies when buildTabs() deletes it.
    auto *poll = new QTimer(tab);
    poll->setInterval(2000);
    connect(poll, &QTimer::timeout, tab, refreshStatus);
    poll->start();

    return tab;
}

QWidget *SettingsDialog::createMonitorsTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    // Top: every configured dock, plus a "remove from list" button.
    layout->addWidget(new QLabel(tr("Docks (Doble-click renombra):"), tab));
    m_docksList = new QListWidget(tab);
    layout->addWidget(m_docksList);

    auto *dockButtons = new QHBoxLayout;
    m_deleteDockButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")),
                                         tr("Borrar de la lista"), tab);
    dockButtons->addWidget(m_deleteDockButton);
    dockButtons->addStretch();
    layout->addLayout(dockButtons);

    // Separator
    auto *sep = new QLabel(tab);
    sep->setFrameStyle(QFrame::HLine | QFrame::Sunken);
    layout->addWidget(sep);

    // Bottom: monitors the selected dock can appear on. Checking another monitor
    // shows a live *preview* dock there; nothing is persisted until "Aplicar".
    layout->addWidget(new QLabel(
        tr("Monitores (marcá otro monitor para ver una vista previa del dock; "
           "\"Aplicar\" crea la copia definitiva):"),
        tab));
    m_monitorsList = new QListWidget(tab);
    m_monitorsList->setEnabled(false);
    layout->addWidget(m_monitorsList);

    auto *monButtons = new QHBoxLayout;
    monButtons->addStretch();
    m_applyPreviewButton = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")),
                                           tr("Aplicar"), tab);
    monButtons->addWidget(m_applyPreviewButton);
    layout->addLayout(monButtons);

    connect(m_docksList, &QListWidget::currentRowChanged, this, [this](int row) {
        // Switching the selected dock discards any pending (unapplied) previews.
        if (m_manager)
            m_manager->clearPreviews();
        m_selectedTabDockId = (row < 0)
            ? QString()
            : m_docksList->item(row)->data(Qt::UserRole).toString();
        reloadMonitorsForSelectedDock();
    });

    // Double-click a dock row to give it (or remove its) alias. The dock keeps
    // its auto-name Dock 1/2/3 underneath; the alias is only a display name.
    connect(m_docksList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item || !m_manager)
            return;
        const QString dockId = item->data(Qt::UserRole).toString();
        if (dockId.isEmpty())
            return;
        DockConfig *cfg = m_manager->configFor(dockId);
        const QString current = cfg->alias();
        bool ok = false;
        const QString text = QInputDialog::getText(
            this, tr("Nombre del dock"),
            tr("Alias para \"%1\" (vacío = volver a \"Dock %2\"):")
                .arg(dockLabel(dockId))
                .arg(DockConfig::slotOfDockId(dockId) + 1),
            QLineEdit::Normal, current, &ok);
        if (ok)
            cfg->setAlias(text);
        reloadDocksList();
    });

    connect(m_monitorsList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (m_selectedTabDockId.isEmpty() || !m_manager)
            return;
        const QString screen = item->data(Qt::UserRole).toString();
        const bool checked = item->checkState() == Qt::Checked;
        const QString ownScreen = DockConfig::screenOfDockId(m_selectedTabDockId);

        if (screen == ownScreen) {
            // The dock's own monitor: the checkbox is its enabled state.
            m_manager->setDockEnabled(m_selectedTabDockId, checked);
            reloadDocksList();
            return;
        }
        // Another monitor: toggle a live, non-persisted preview.
        if (checked) {
            QString err;
            if (!m_manager->previewDockOnScreen(m_selectedTabDockId, screen, &err)) {
                QMessageBox::warning(this, tr("Vista previa"), err);
                reloadMonitorsForSelectedDock(); // revert the check
                return;
            }
        } else {
            m_manager->unpreviewScreen(m_selectedTabDockId, screen);
        }
        updateMonitorsTabButtons();
    });

    connect(m_deleteDockButton, &QPushButton::clicked, this, [this]() {
        if (m_selectedTabDockId.isEmpty() || !m_manager)
            return;
        if (QMessageBox::question(
                this, tr("Borrar dock"),
                tr("¿Eliminar el dock \"%1\"?\n\nSe dejará de mostrar y su "
                   "archivo de configuración se borrará "
                   "(%2).").arg(m_selectedTabDockId,
                                QFileInfo(DockConfig::instanceSettingsFilePath(
                                              m_selectedTabDockId))
                                    .fileName()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        m_manager->clearPreviews();
        m_manager->removeDock(m_selectedTabDockId);
        m_selectedTabDockId.clear();
        reloadDocksList();
    });

    connect(m_applyPreviewButton, &QPushButton::clicked, this, [this]() {
        if (m_selectedTabDockId.isEmpty() || !m_manager)
            return;
        m_manager->applyPreviews(m_selectedTabDockId);
        reloadDocksList(); // the committed copies show up as new dock rows
    });

    reloadDocksList();
    return tab;
}

void SettingsDialog::reloadDocksList()
{
    if (!m_docksList || !m_manager)
        return;
    const QSignalBlocker block(m_docksList);
    const QString previous = m_selectedTabDockId;
    m_docksList->clear();
    int selectRow = -1;
    const QString thisScreen = DockConfig::screenOfDockId(m_dockId);
    const QStringList docks = m_manager->configuredDocks();
    for (int i = 0; i < docks.size(); ++i) {
        const QString &id = docks.at(i);
        QString label = dockLabel(id);

        // Edge position, so the list tells where each dock lives without
        // needing the preview geometry.
        const DockConfig *cfg = m_manager->configFor(id);
        switch (cfg->edge()) {
        case DockConfig::Top:    label += tr(" [Arriba]"); break;
        case DockConfig::Left:   label += tr(" [Izquierda]"); break;
        case DockConfig::Right:  label += tr(" [Derecha]"); break;
        case DockConfig::Bottom:
        default:                 label += tr(" [Abajo]"); break;
        }

        // Mark the docks living on the monitor the dialog was opened from, so
        // the row the user was on is easy to find again.
        if (DockConfig::screenOfDockId(id) == thisScreen)
            label += tr(" (ESTE MONITOR)");

        if (!m_manager->isDockEnabled(id))
            label += tr("  (oculto)");
        auto *item = new QListWidgetItem(label, m_docksList);
        item->setData(Qt::UserRole, id);
        if (id == previous)
            selectRow = i;
    }
    if (selectRow >= 0)
        m_docksList->setCurrentRow(selectRow);
    else
        m_selectedTabDockId.clear();
    reloadMonitorsForSelectedDock();
}

void SettingsDialog::reloadMonitorsForSelectedDock()
{
    if (!m_monitorsList || !m_manager)
        return;
    const QSignalBlocker block(m_monitorsList);
    m_monitorsList->clear();
    if (m_selectedTabDockId.isEmpty()) {
        m_monitorsList->setEnabled(false);
        updateMonitorsTabButtons();
        return;
    }
    m_monitorsList->setEnabled(true);
    const QString ownScreen = DockConfig::screenOfDockId(m_selectedTabDockId);
    const QStringList connected = m_manager->connectedScreens();
    const bool enabled = m_manager->isDockEnabled(m_selectedTabDockId);
    for (const QString &screen : m_manager->knownScreensForUi()) {
        QString label = screen;
        if (!connected.contains(screen))
            label += tr("  (desconectado)");
        if (screen == ownScreen)
            label += tr("  — este dock");
        else if (m_manager->hasPreview(m_selectedTabDockId, screen))
            label += tr("  (vista previa — sin aplicar)");
        auto *item = new QListWidgetItem(label, m_monitorsList);
        item->setData(Qt::UserRole, screen);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        // Own monitor: checked = the dock's enabled state. Others: checked =
        // there is a pending preview on that monitor.
        const bool checked = (screen == ownScreen)
            ? enabled
            : m_manager->hasPreview(m_selectedTabDockId, screen);
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
    updateMonitorsTabButtons();
}

void SettingsDialog::updateMonitorsTabButtons()
{
    if (m_deleteDockButton)
        m_deleteDockButton->setEnabled(!m_selectedTabDockId.isEmpty());
    if (m_applyPreviewButton)
        m_applyPreviewButton->setEnabled(
            m_manager && !m_selectedTabDockId.isEmpty()
            && m_manager->hasPreviewsFor(m_selectedTabDockId));
}

void SettingsDialog::closeEvent(QCloseEvent *event)
{
    // Discard any live previews that were never applied.
    if (m_manager)
        m_manager->clearPreviews();
    QDialog::closeEvent(event);
}
