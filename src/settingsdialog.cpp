#include "settingsdialog.h"

#include "audiocontrol.h"
#include "networksettingswidget.h"
#include "appearancecontrol.h"
#include "coloredtabbar.h"
#include "desktopentry.h"
#include "dockconfig.h"
#include "dockmanager.h"
#include "dockmodel.h"
#include "desktopwallpapers.h"
#include "iconcolorprovider.h"
#include "relanzadorconfig.h"
#include "relanzadoresmanager.h"
#include "configarchive.h"
#include "iconpickerdialog.h"
#include "themepicker.h"
#include "previewslauncher.h"
#include "controlmanagerlauncher.h"
#include "tilemenulauncher.h"
#include "scriptrunnerconfig.h"
#include "scriptrunnersmanager.h"
#include "systray.h"
#include "translations.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDir>
#include <QComboBox>
#include <QGroupBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFont>
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
#include <QImageReader>
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
#include <QUrl>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

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
        int idx = m_monitorSelector->findData(screen);
        if (idx < 0 && !screen.isEmpty()) {
            // Same as in selectDock(): a dock whose monitor is unplugged still
            // has to be named correctly by the bar.
            m_monitorSelector->addItem(tr("%1 (desconectado)").arg(screen), screen);
            idx = m_monitorSelector->count() - 1;
        }
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

        // Second row: which virtual desktop this dock belongs to, plus its name
        // from the Docks tab. Monitor + slot alone don't say which of several
        // per-desktop copies is being edited, which is the whole point of
        // opening the dialog from a right-click on one particular dock.
        auto *deskBar = new QHBoxLayout;
        deskBar->addWidget(new QLabel(tr("Escritorio:"), this));
        m_desktopSelector = new QComboBox(this);
        m_desktopSelector->setToolTip(
            tr("Escritorios virtuales a los que pertenece el dock que se está "
               "editando. Elegí otro para saltar al dock de ese escritorio en "
               "este monitor (se asigna en la solapa Docks)."));
        deskBar->addWidget(m_desktopSelector, 1);
        deskBar->addWidget(new QLabel(tr("Nombre:"), this));
        m_dockNameLabel = new QLabel(this);
        QFont nameFont = m_dockNameLabel->font();
        nameFont.setBold(true);
        m_dockNameLabel->setFont(nameFont);
        deskBar->addWidget(m_dockNameLabel);
        deskBar->addStretch();
        mainLayout->addLayout(deskBar);

        // "activated" and not currentIndexChanged: reloadDockHeader() rebuilds
        // the combo on every dock switch, and that must not switch docks again.
        connect(m_desktopSelector, &QComboBox::activated, this, [this](int) {
            const QString want = m_desktopSelector->currentData().toString();
            if (desktopBindingKey(m_manager->configFor(m_dockId)->dockDesktops()) == want)
                return; // the dock on screen already belongs to that desktop
            const QString screen = DockConfig::screenOfDockId(m_dockId);
            for (const QString &id : m_manager->configuredDocks()) {
                if (DockConfig::screenOfDockId(id) != screen)
                    continue;
                if (desktopBindingKey(m_manager->configFor(id)->dockDesktops()) != want)
                    continue;
                {
                    const QSignalBlocker block(m_slotSelector);
                    m_slotSelector->setCurrentIndex(DockConfig::slotOfDockId(id));
                }
                selectDock(id);
                return;
            }
            reloadDockHeader(); // nothing there: snap back to the current dock
        });

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
    // Qt labels its standard buttons from *its own* catalogs, i.e. in the system
    // locale, which would leave a Spanish "Cerrar" in a dialog the user asked to
    // see in another language. Setting the text puts it back on our layer.
    box->button(QDialogButtonBox::Close)->setText(tr("Close"));
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
    reloadDockHeader();

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
        addTab(createMonitorsTab(), tr("Docks"));
        m_monitorsTabIndex = m_tabWidget->count() - 1;
    }
    // Not a per-dock setting either: it drives the whole session's wallpapers.
    m_wallpaperSnapshotList = nullptr;
    if (m_manager)
        addTab(createWallpapersTab(), tr("Wallpapers"));
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
    // Dead last on purpose: it is the tab that decides in which language every
    // other tab is written.
    m_translationsTabIndex = -1;
    if (Translations::instance()) {
        addTab(createTranslationsTab(), tr("Traducciones"));
        m_translationsTabIndex = m_tabWidget->count() - 1;
    }
    applyTabColors();

    // A dock moved via the context menu (Dock → Mover Sig. Monitor) changes the
    // enabled/known dock sets out from under the dialog; refresh the Docks tab
    // so its lists match reality instead of going stale.
    if (m_manager) {
        connect(m_manager, &DockManager::dockListChanged, this, [this] {
            reloadDocksList();
            reloadMonitorsForSelectedDock();
        });
    }
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
    // The two selectors are the caller when this comes from selectFromCombos(),
    // but not when it comes from the desktop selector or showMonitorsTab().
    if (m_monitorSelector && m_slotSelector) {
        const QSignalBlocker blockMon(m_monitorSelector);
        const QSignalBlocker blockSlot(m_slotSelector);
        const QString screen = DockConfig::screenOfDockId(dockId);
        int monIdx = m_monitorSelector->findData(screen);
        if (monIdx < 0 && !screen.isEmpty()) {
            // The combo only lists connected monitors; a dock on an unplugged
            // one still has to be selectable, or the bar would name another.
            m_monitorSelector->addItem(tr("%1 (desconectado)").arg(screen), screen);
            monIdx = m_monitorSelector->count() - 1;
        }
        if (monIdx >= 0)
            m_monitorSelector->setCurrentIndex(monIdx);
        m_slotSelector->setCurrentIndex(DockConfig::slotOfDockId(dockId));
    }
    m_relanzadores = m_manager->relanzadores();
    m_scriptRunners = m_manager->scriptRunners();
    buildTabs();
    updateEnabledCheck();
    reloadDockHeader();
}

void SettingsDialog::updateEnabledCheck()
{
    if (!m_enabledCheck || !m_manager)
        return;
    const QSignalBlocker block(m_enabledCheck);
    m_enabledCheck->setChecked(m_manager->isDockEnabled(m_dockId));
}

QString SettingsDialog::desktopBindingKey(const QList<int> &desktops)
{
    QStringList parts;
    for (int position : desktops)
        parts << QString::number(position);
    return parts.join(QLatin1Char(','));
}

QString SettingsDialog::desktopBindingLabel(const QList<int> &desktops) const
{
    if (desktops.isEmpty())
        return tr("Todos (dock base)");
    const QStringList names = m_manager ? m_manager->desktopNamesForUi() : QStringList();
    QStringList parts;
    for (int position : desktops)
        parts << (position <= names.size() ? names.at(position - 1)
                                           : tr("Escritorio %1").arg(position));
    return parts.join(QStringLiteral(" / "));
}

void SettingsDialog::reloadDockHeader()
{
    if (!m_manager || !m_desktopSelector)
        return;
    const QSignalBlocker block(m_desktopSelector);
    m_desktopSelector->clear();

    // One entry per distinct desktop binding among the docks of this monitor,
    // so picking one always lands somewhere. "Base" is always offered, and so
    // is the current dock's own binding even when it is the only dock with it
    // (a slot that has no dock yet counts as base).
    const QString screen = DockConfig::screenOfDockId(m_dockId);
    const QList<int> mine = m_manager->configFor(m_dockId)->dockDesktops();
    QList<QList<int>> groups;
    groups << QList<int>();
    for (const QString &id : m_manager->configuredDocks()) {
        if (DockConfig::screenOfDockId(id) != screen)
            continue;
        const QList<int> desktops = m_manager->configFor(id)->dockDesktops();
        if (!groups.contains(desktops))
            groups << desktops;
    }
    if (!groups.contains(mine))
        groups << mine;
    for (const QList<int> &group : std::as_const(groups))
        m_desktopSelector->addItem(desktopBindingLabel(group), desktopBindingKey(group));
    m_desktopSelector->setCurrentIndex(
        qMax(0, m_desktopSelector->findData(desktopBindingKey(mine))));

    if (m_dockNameLabel)
        m_dockNameLabel->setText(dockLabel(m_dockId));
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
        // panel mode, or a fixed length of 100%) AND a spring is present. In
        // any other fixed-length mode or floating mode the alignment applies.
        const bool fullEdge = (m_config->panelMode() && m_config->dockLength() == 0)
                              || m_config->dockLength() == 100;
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
    if (m_theme && m_appearance)
        form->addRow(tr("Icon theme:"), makeDockIconThemePicker(tab));

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

        auto *lightBg = makeWidgetIconSetPicker(tab, false);
        auto *darkBg = makeWidgetIconSetPicker(tab, true);
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
        // The mode combo is mirrored in the Colores tab, so it re-reads from the
        // config when any of the three widget-icon values changes. The two
        // pickers re-read themselves (see makeWidgetIconSetPicker). Setters only
        // emit on a real change, so the loop this creates terminates on its own.
        const auto resync = [this, widgetIcons, syncEnabled] {
            widgetIcons->setCurrentIndex(qMax(0, widgetIcons->findData(
                                                m_config->widgetIconThemeMode())));
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

    // Re-maximize windows after a virtual-desktop switch when the two docks
    // briefly coexist on the same output. Shared setting, not per dock.
    {
        auto *cb = new QCheckBox(tr("Restore maximized windows after desktop switch"), tab);
        cb->setChecked(DockConfig::maximizeWindowsOnDesktop());
        cb->setToolTip(tr("When a virtual-desktop switch briefly shows two docks on the same "
                          "output, KWin can shrink the work area and leave maximized windows "
                          "at the wrong size. This re-maximizes the ones that were maximized "
                          "before the switch."));
        connect(cb, &QCheckBox::toggled, this,
                [](bool on) { DockConfig::setMaximizeWindowsOnDesktop(on); });
        form->addRow(tr("Maximize windows:"), cb);
    }

    // Master switch for the dock's tooltips. Shared, not per dock: turning it
    // off hides every ToolTip on every dock at once.
    {
        auto *cb = new QCheckBox(tr("Mostrar Tooltips"), tab);
        cb->setChecked(DockConfig::showTooltips());
        cb->setToolTip(tr("Muestra la descripción al pasar el mouse por los elementos "
                          "del dock. Desactivar oculta todos los tooltips de todos "
                          "los docks."));
        connect(cb, &QCheckBox::toggled, this,
                [](bool on) { DockConfig::setShowTooltips(on); });
        form->addRow(tr("Tooltips:"), cb);
    }

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
        // open apps reload). Both are ThemePickerButtons - the Qt Widgets twin
        // of the dock widget's popup (search field, previews, favourites), not
        // a plain combo: these two lists run to 183 and 456 entries.
        if (m_appearance) {
            m_appearance->refreshIfStale();

            auto *iconPicker = new ThemePickerButton(m_appearance, QStringLiteral("icons"),
                                                     ThemePickerPopup::ApplyToDesktop, box);
            iconPicker->setToolTip(tr("Applies the icon theme to the whole desktop right now "
                                      "(plasma-changeicons). kdock's own icon theme is left "
                                      "alone: while it is set, this dock keeps its icons and "
                                      "only the rest of KDE follows."));
            form->addRow(tr("· Apply icon theme:"), iconPicker);

            auto *schemePicker = new ThemePickerButton(m_appearance, QStringLiteral("colors"),
                                                       ThemePickerPopup::ApplyToDesktop, box);
            schemePicker->setToolTip(tr("Applies a KDE color scheme system-wide right now "
                                        "(plasma-apply-colorscheme)."));
            form->addRow(tr("· Apply color scheme:"), schemePicker);
        }

        layout->addWidget(box);
    }

    // --- Iconset del dock: el override global de kdock (General → "Icon theme") ---
    {
        auto *box = new QGroupBox(tr("Iconset del dock"), tab);
        auto *form = new QFormLayout(box);

        if (m_theme && m_appearance)
            form->addRow(tr("Iconset del dock:"), makeDockIconThemePicker(box));

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

        auto *lightBg = makeWidgetIconSetPicker(box, false);
        auto *darkBg = makeWidgetIconSetPicker(box, true);
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
        // The mode combo is mirrored in the General tab; the two pickers re-read
        // themselves (see makeWidgetIconSetPicker).
        const auto resync = [this, widgetIcons, syncEnabled] {
            widgetIcons->setCurrentIndex(qMax(0, widgetIcons->findData(
                                                m_config->widgetIconThemeMode())));
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

    // The Control Manager widget can draw text of its own (a fixed string, or a
    // clock) that is neither an app name nor a widget-section name — so it gets a
    // font of its own instead of reusing one gated on a label mode. Only
    // meaningful while the widget is visible and actually draws text. Mirrored
    // in the Widgets tab's Control Manager group, like the clock font above.
    auto *cmFont = new QSpinBox(tab);
    cmFont->setRange(0, 96);
    cmFont->setSpecialValueText(tr("Automatic"));
    cmFont->setSuffix(tr(" px"));
    cmFont->setValue(m_config->controlManagerFontSize());
    cmFont->setToolTip(tr("Font size of the text the Control Manager widget draws "
                          "on the dock. Automatic follows the clock font and, with "
                          "neither, a fraction of the icon size."));
    connect(cmFont, &QSpinBox::valueChanged, m_config, &DockConfig::setControlManagerFontSize);
    connect(m_config, &DockConfig::controlManagerFontSizeChanged, cmFont,
            [this, cmFont] { cmFont->setValue(m_config->controlManagerFontSize()); });
    const auto syncCmFontEnabled = [this, cmFont] {
        cmFont->setEnabled(m_config->showControlManager()
                           && m_config->controlManagerDisplay() != 0);
    };
    connect(m_config, &DockConfig::showControlManagerChanged, tab, syncCmFontEnabled);
    connect(m_config, &DockConfig::controlManagerDisplayChanged, tab, syncCmFontEnabled);
    syncCmFontEnabled();
    form->addRow(tr("Control Manager text size:"), cmFont);

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

    auto *labelWrap = new QCheckBox(tr("Dos renglones"), tab);
    labelWrap->setChecked(m_config->labelLines() > 1);
    labelWrap->setToolTip(tr("Deja que un nombre largo se dibuje en dos renglones en vez de "
                              "cortarse con puntos suspensivos. El nombre ocupa dos "
                              "líneas de alto, así que el dock engorda otro tanto."));
    connect(labelWrap, &QCheckBox::toggled, m_config,
            [this](bool on) { m_config->setLabelLines(on ? 2 : 1); });
    connect(m_config, &DockConfig::labelLinesChanged, labelWrap,
            [this, labelWrap] { labelWrap->setChecked(m_config->labelLines() > 1); });
    form->addRow(tr("Name lines:"), labelWrap);

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
    auto syncLabelEnabled = [this, labelWidth, labelFont, labelWrap] {
        const bool on = m_config->iconLabelMode() != DockConfig::IconOnly
                        || m_config->widgetLabelMode() != DockConfig::IconOnly;
        labelWidth->setEnabled(on);
        labelFont->setEnabled(on);
        labelWrap->setEnabled(on);
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

        // Mirrored in the Fuentes tab, like the width and the font size.
        auto *labelWrap = new QCheckBox(tr("Dos renglones"), tab);
        labelWrap->setChecked(m_config->labelLines() > 1);
        labelWrap->setToolTip(tr("Deja que un nombre largo se dibuje en dos renglones en vez de "
                                  "cortarse con puntos suspensivos. El nombre ocupa dos "
                                  "líneas de alto, así que el dock engorda otro tanto."));
        connect(labelWrap, &QCheckBox::toggled, m_config,
                [this](bool on) { m_config->setLabelLines(on ? 2 : 1); });
        connect(m_config, &DockConfig::labelLinesChanged, labelWrap,
                [this, labelWrap] { labelWrap->setChecked(m_config->labelLines() > 1); });
        form->addRow(tr("· Name lines:"), labelWrap);

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
        auto syncLabelEnabled = [this, labelWidth, labelFont, labelWrap] {
            const bool on = m_config->iconLabelMode() != DockConfig::IconOnly
                            || m_config->widgetLabelMode() != DockConfig::IconOnly;
            labelWidth->setEnabled(on);
            labelFont->setEnabled(on);
            labelWrap->setEnabled(on);
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

    auto *showPager = new QCheckBox(tr("Mostrar el paginador de escritorios"), tab);
    showPager->setChecked(m_config->showPager());
    showPager->setToolTip(tr("Un número por escritorio virtual de KWin; el clic cambia "
                             "a ese escritorio. El actual va resaltado."));
    connect(showPager, &QCheckBox::toggled, m_config, &DockConfig::setShowPager);
    form->addRow(tr("Escritorios:"), showPager);

    // The tray can live in any dock, but in only one at a time: several docks
    // drawing the same StatusNotifierItems would duplicate every icon and open
    // two menus for one click. While another dock holds it, the checkbox is
    // disabled and says where to go turn it off (the state is recomputed by
    // buildTabs() whenever the edited dock changes).
    auto *showSystray = new QCheckBox(tr("Mostrar la bandeja del sistema"), tab);
    showSystray->setChecked(m_config->showSystray());
    // Only docks that can be on screen at the same time collide: a dock bound
    // to another virtual desktop may host its own tray.
    const QString systrayOwner = m_manager ? m_manager->systrayDockIdFor(m_dockId) : QString();
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

    layout->addWidget(createControlManagerGroup(tab));

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

// The control panel's widget: shown or not, its icon, and what it draws beside
// (or instead of) that icon. Everything about the panel itself lives in its own
// settings dialog, which the button at the bottom opens — same split as the tile
// menu group above and the Previews tab.
QWidget *SettingsDialog::createControlManagerGroup(QWidget *parent)
{
    auto *box = new QGroupBox(tr("Control Manager (panel de control)"), parent);
    auto *layout = new QVBoxLayout(box);

    auto *info = new QLabel(
        tr("Un panel anclado a un borde de la pantalla con solapas: audio, brillo de cada "
           "monitor, perfil de energía, calendario, reproducción, red, fondo de escritorio y "
           "sistema. Es un binario aparte, kdock-controlmanager, con su propia configuración: "
           "el botón de abajo abre su panel."),
        box);
    info->setWordWrap(true);
    layout->addWidget(info);

    if (!ControlManagerLauncher::installed()) {
        auto *missing = new QLabel(tr("kdock-controlmanager no está instalado."), box);
        missing->setStyleSheet(QStringLiteral("color: gray;"));
        layout->addWidget(missing);
        return box;
    }

    auto *form = new QFormLayout;

    auto *showCm = new QCheckBox(tr("Mostrar el botón en este dock"), box);
    showCm->setChecked(m_config->showControlManager());
    connect(showCm, &QCheckBox::toggled, m_config, &DockConfig::setShowControlManager);
    connect(m_config, &DockConfig::showControlManagerChanged, showCm,
            [this, showCm] { showCm->setChecked(m_config->showControlManager()); });
    form->addRow(tr("Widget:"), showCm);

    auto *iconBtn = new QPushButton(box);
    iconBtn->setStyleSheet(QStringLiteral("text-align:left; padding:4px 8px;"));
    const auto refreshIcon = [this, iconBtn] {
        const QString n = m_config->controlManagerIcon();
        iconBtn->setIcon(QIcon::fromTheme(n));
        iconBtn->setText(QStringLiteral(" ") + n);
    };
    refreshIcon();
    connect(iconBtn, &QPushButton::clicked, this, [this, refreshIcon] {
        IconPickerDialog dlg(m_config->controlManagerIcon(), this);
        if (dlg.exec() == QDialog::Accepted && !dlg.selectedIcon().isEmpty()) {
            m_config->setControlManagerIcon(dlg.selectedIcon());
            refreshIcon();
        }
    });
    form->addRow(tr("Ícono del widget:"), iconBtn);

    auto *display = new QComboBox(box);
    display->addItem(tr("Solo el ícono"));
    display->addItem(tr("Ícono y texto"));
    display->addItem(tr("Solo el texto"));
    display->setCurrentIndex(m_config->controlManagerDisplay());
    connect(display, &QComboBox::currentIndexChanged, m_config,
            &DockConfig::setControlManagerDisplay);
    form->addRow(tr("Qué muestra:"), display);

    auto *text = new QLineEdit(m_config->controlManagerText(), box);
    // No " = " in the string: that is the separator of the translation
    // catalogue's line format, so a string containing one is split into the
    // wrong key/value pair (this one became key "vacío"). The dockLength
    // tooltip above has the same shape and the same problem.
    text->setPlaceholderText(tr("vacío: la fecha y la hora"));
    text->setToolTip(tr("Una cadena corta propia, por ejemplo «Máquina de Pruebas». "
                        "Dejalo vacío para que muestre el reloj."));
    connect(text, &QLineEdit::textEdited, m_config, &DockConfig::setControlManagerText);
    form->addRow(tr("Texto:"), text);

    auto *format = new QLineEdit(m_config->controlManagerFormat(), box);
    format->setToolTip(tr("Formato de fecha/hora de Qt: dddd, d MMMM yyyy, HH:mm:ss…"));
    connect(format, &QLineEdit::textEdited, m_config, &DockConfig::setControlManagerFormat);
    // Only meaningful while the text *is* the clock.
    const auto syncFormatEnabled = [this, format, text, display] {
        format->setEnabled(m_config->controlManagerDisplay() != 0
                           && m_config->controlManagerText().isEmpty());
        text->setEnabled(m_config->controlManagerDisplay() != 0);
        Q_UNUSED(display);
    };
    syncFormatEnabled();
    connect(m_config, &DockConfig::controlManagerTextChanged, format, syncFormatEnabled);
    connect(m_config, &DockConfig::controlManagerDisplayChanged, format, syncFormatEnabled);
    form->addRow(tr("Formato del reloj:"), format);

    // The text's own font. Mirrored in the Fuentes tab (there it lives next to
    // the clock font, which is what automatic follows); enabled only while the
    // widget draws text.
    auto *cmFont = new QSpinBox(box);
    cmFont->setRange(0, 96);
    cmFont->setSpecialValueText(tr("Automatic"));
    cmFont->setSuffix(tr(" px"));
    cmFont->setValue(m_config->controlManagerFontSize());
    cmFont->setToolTip(tr("Tamaño de fuente del texto que dibuja este widget en el "
                          "dock. Automático sigue la fuente del reloj y, si no, una "
                          "fracción del tamaño del ícono."));
    connect(cmFont, &QSpinBox::valueChanged, m_config, &DockConfig::setControlManagerFontSize);
    connect(m_config, &DockConfig::controlManagerFontSizeChanged, cmFont,
            [this, cmFont] { cmFont->setValue(m_config->controlManagerFontSize()); });
    const auto syncCmFontEnabled = [this, cmFont] {
        cmFont->setEnabled(m_config->showControlManager()
                           && m_config->controlManagerDisplay() != 0);
    };
    syncCmFontEnabled();
    connect(m_config, &DockConfig::showControlManagerChanged, box, syncCmFontEnabled);
    connect(m_config, &DockConfig::controlManagerDisplayChanged, box, syncCmFontEnabled);
    form->addRow(tr("Tamaño del texto:"), cmFont);

    auto *preload = new QCheckBox(tr("Dejarlo cargado al iniciar kdock"), box);
    preload->setChecked(ControlManagerLauncher::preload());
    preload->setToolTip(tr("Sin esto, el proceso arranca en el primer clic (medio segundo) y "
                           "queda residente: las aperturas siguientes son instantáneas."));
    connect(preload, &QCheckBox::toggled, this, [](bool on) {
        ControlManagerLauncher::setPreload(on);
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
        status->setText(ControlManagerLauncher::running()
                            ? tr("Estado: en ejecución (%1)")
                                  .arg(ControlManagerLauncher::binaryPath())
                            : tr("Estado: detenido (%1)")
                                  .arg(ControlManagerLauncher::binaryPath()));
    };
    refreshStatus();
    connect(configureBtn, &QPushButton::clicked, this, [this, refreshStatus] {
        if (!m_cmLauncher)
            m_cmLauncher = new ControlManagerLauncher(this);
        m_cmLauncher->openSettings();
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
    // Edit that dock, not whichever one the dialog was left on: the whole point
    // of arriving here is that the user pointed at one (right-click → Dock →
    // Nombre, or a freshly created empty dock). No-op when it is the current
    // one; otherwise it rebuilds the tabs, so the row is selected afterwards.
    selectDock(dockId);
    // Switch to the Docks tab and select the requested dock row, so the
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

bool SettingsDialog::hideOfflinePref(const char *key)
{
    QSettings s(DockConfig::settingsFilePath(), QSettings::IniFormat);
    return s.value(QLatin1String(key), true).toBool(); // hidden by default
}

void SettingsDialog::setHideOfflinePref(const char *key, bool on)
{
    QSettings s(DockConfig::settingsFilePath(), QSettings::IniFormat);
    s.setValue(QLatin1String(key), on);
}

QString SettingsDialog::dockLabel(const QString &dockId)
{
    // The alias, when set, replaces the default "<screen> — Dock <n>" name.
    const QString base = tr("%1 — Dock %2")
                             .arg(DockConfig::screenOfDockId(dockId))
                             .arg(DockConfig::slotOfDockId(dockId) + 1);
    QSettings s(DockConfig::instanceSettingsFilePath(dockId), QSettings::IniFormat);
    const QString alias = s.value(QStringLiteral("alias")).toString().trimmed();
    // The alias is added to the automatic name, not swapped for it: naming a
    // dock used to drop the monitor it lives on, which is the one thing the
    // list needs to tell docks apart. Renaming is not destructive, so every
    // already-renamed dock gets its monitor back with no migration.
    return alias.isEmpty() ? base : tr("%1: %2").arg(base, alias);
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

ThemePickerButton *SettingsDialog::makeDockIconThemePicker(QWidget *parent)
{
    auto *picker = new ThemePickerButton(m_appearance, QStringLiteral("icons"),
                                         ThemePickerPopup::PickValue, parent);
    // Empty id means "no override, follow KDE" — a real choice, so it leads the
    // list instead of being unreachable.
    picker->setSpecialEntry(tr("(System default)"));
    picker->setCurrentId(m_theme ? m_theme->iconTheme() : QString());
    picker->setToolTip(tr("Icon set kdock draws with. Overrides the KDE icon theme for every "
                          "dock, and leaves the rest of the desktop alone."));
    connect(picker, &ThemePickerButton::picked, this, [this](const QString &id) {
        if (m_theme)
            m_theme->setIconTheme(id);
    });
    // The same picker lives in the other tab (and the override is global, so
    // another dock's dialog can change it too): re-read instead of going stale.
    if (m_theme)
        connect(m_theme, &Theme::changed, picker,
                [this, picker] { picker->setCurrentId(m_theme->iconTheme()); });
    return picker;
}

ThemePickerButton *SettingsDialog::makeWidgetIconSetPicker(QWidget *parent, bool darkBg)
{
    auto *picker = new ThemePickerButton(m_appearance, QStringLiteral("icons"),
                                         ThemePickerPopup::PickValue, parent);
    picker->setCurrentId(darkBg ? m_config->widgetIconThemeDarkBg()
                                : m_config->widgetIconThemeLightBg());
    picker->setToolTip(darkBg ? tr("Icon set for a dark dock background (light icons).")
                              : tr("Icon set for a light dock background (dark icons)."));
    connect(picker, &ThemePickerButton::picked, this, [this, darkBg](const QString &id) {
        if (darkBg)
            m_config->setWidgetIconThemeDarkBg(id);
        else
            m_config->setWidgetIconThemeLightBg(id);
    });
    // Mirrored in the other tab; the signal covers all three widget-icon keys.
    connect(m_config, &DockConfig::widgetIconThemeChanged, picker, [this, picker, darkBg] {
        picker->setCurrentId(darkBg ? m_config->widgetIconThemeDarkBg()
                                    : m_config->widgetIconThemeLightBg());
    });
    return picker;
}

void SettingsDialog::addDarkAppearanceExtras(QFormLayout *form, QWidget *parent)
{
    // Every row is a ThemePickerButton in PickValue mode, so the lists (and the
    // favourites) come from AppearanceControl instead of being rebuilt here.
    //
    // The empty id means different things on each side, hence two labels: going
    // dark it is "leave the desktop alone", coming back it is "restore what was
    // there before" (the snapshot DarkModeAppearance takes on the way in). The
    // dock's own row is the exception — there an empty id is already a setting
    // of its own ("no override, follow KDE"), on both sides.
    if (!m_appearance)
        return;
    const QString noChange = tr("(no cambiar)");
    const QString restorePrevious = tr("(volver al anterior)");

    addDarkAppearanceExtrasRow(form, parent, DockConfig::SystemColorScheme,
                               tr("El esquema de color del sistema"),
                               tr("Aplica el esquema de color de KDE, igual que el widget "
                                  "«Esquema de color» (plasma-apply-colorscheme)."),
                               QStringLiteral("colors"), noChange, restorePrevious);

    addDarkAppearanceExtrasRow(form, parent, DockConfig::SystemIconTheme,
                               tr("El iconset del sistema"),
                               tr("Aplica el iconset de KDE, igual que el widget «Iconset» "
                                  "(plasma-changeicons). Afecta a todo el escritorio."),
                               QStringLiteral("icons"), noChange, restorePrevious);

    addDarkAppearanceExtrasRow(form, parent, DockConfig::DockIconTheme,
                               tr("El iconset del dock"),
                               tr("Solo el iconset que usa kdock, sin tocar el del escritorio "
                                  "(Configuración → General → «Iconset del dock»)."),
                               QStringLiteral("icons"), tr("(seguir el del sistema)"),
                               tr("(seguir el del sistema)"));
}

void SettingsDialog::addDarkAppearanceExtrasRow(QFormLayout *form, QWidget *parent, int item,
                                                const QString &title, const QString &tip,
                                                const QString &kind, const QString &specialDark,
                                                const QString &specialNormal)
{
    // Without an explicit empty-id entry the picker would sit on whatever sorts
    // first and quietly apply *that* when the mode flips.
    auto *check = new QCheckBox(title, parent);
    check->setChecked(DockConfig::darkAppearanceEnabled(item));
    check->setToolTip(tip);

    const auto makePicker = [this, parent, kind](const QString &special) {
        auto *p = new ThemePickerButton(m_appearance, kind, ThemePickerPopup::PickValue, parent);
        p->setSpecialEntry(special);
        return p;
    };
    auto *darkPick = makePicker(specialDark);
    auto *normalPick = makePicker(specialNormal);

    const auto reselect = [check, darkPick, normalPick, item] {
        // setCurrentId() is silent by contract, so no QSignalBlocker here; the
        // checkbox still needs one.
        const QSignalBlocker b(check);
        check->setChecked(DockConfig::darkAppearanceEnabled(item));
        darkPick->setCurrentId(DockConfig::darkAppearanceValue(item, true));
        normalPick->setCurrentId(DockConfig::darkAppearanceValue(item, false));
        darkPick->setEnabled(check->isChecked());
        normalPick->setEnabled(check->isChecked());
    };
    darkPick->setCurrentId(DockConfig::darkAppearanceValue(item, true));
    // No seeding from the live system: an empty "normal" is now a meaningful
    // choice ("put back whatever was there"), captured on the way into dark
    // mode by DarkModeAppearance instead of guessed here.
    normalPick->setCurrentId(DockConfig::darkAppearanceValue(item, false));

    connect(check, &QCheckBox::toggled, this, [this, item, darkPick, normalPick](bool on) {
        // Persist both pickers on enable, so what the row shows is what the
        // switch will use.
        if (on) {
            DockConfig::setDarkAppearanceValue(item, true, darkPick->currentId());
            DockConfig::setDarkAppearanceValue(item, false, normalPick->currentId());
        }
        DockConfig::setDarkAppearanceEnabled(item, on);
        darkPick->setEnabled(on);
        normalPick->setEnabled(on);
    });
    connect(darkPick, &ThemePickerButton::picked, this, [item](const QString &id) {
        DockConfig::setDarkAppearanceValue(item, true, id);
    });
    connect(normalPick, &ThemePickerButton::picked, this, [item](const QString &id) {
        DockConfig::setDarkAppearanceValue(item, false, id);
    });
    // The values are app-wide statics reachable from the DarkMode and Colores
    // tabs (and another dock's dialog); re-read them all whenever the dark-mode
    // group changes anywhere.
    connect(m_config, &DockConfig::darkModeChanged, check, reselect);
    darkPick->setEnabled(check->isChecked());
    normalPick->setEnabled(check->isChecked());

    form->addRow(check);
    form->addRow(tr("· En modo oscuro:"), darkPick);
    form->addRow(tr("· En modo normal:"), normalPick);
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
    auto *addGap = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")),
                                   tr("Add transparent separator"), tab);
    addGap->setToolTip(tr("Expands like a dynamic separator, and the dock's background is "
                          "not painted over it: the desktop shows through and the dock reads "
                          "as two. It only has room to open in panel mode or with a fixed "
                          "dock length."));
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
    buttons->addWidget(addGap);
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
    connect(addGap, &QPushButton::clicked, this, [this, orderIndexOfRow] {
        const int row = m_layoutList->currentRow();
        const int oi = orderIndexOfRow(row);
        const int at = oi >= 0 ? oi + 1 : m_config->widgetOrder().size();
        m_config->insertGap(at);
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

    // Acts on the selected separator, like Remove/Up/Down: the two slots are
    // independent, so one can be a line and the other just room.
    m_appSepTransparent = new QCheckBox(tr("Transparent separator (no line)"), tab);
    m_appSepTransparent->setToolTip(
        tr("The separator still takes its size in the applications block, but no "
           "line is drawn. Unlike a transparent separator section, the dock "
           "background stays painted behind it."));
    layout->addWidget(m_appSepTransparent);

    auto updateAppSepButtons = [this, addAppSep, removeAppSep, sepUp, sepDown] {
        const bool room = appSeparatorPos(1) < 0 || appSeparatorPos(2) < 0;
        const bool onSep = m_appSepSelected != 0;
        addAppSep->setEnabled(room);
        removeAppSep->setEnabled(onSep);
        sepUp->setEnabled(onSep);
        sepDown->setEnabled(onSep);
        m_appSepTransparent->setEnabled(onSep);
        // Reflects the selected separator, so the box never shows the state of
        // the one the buttons no longer act on. Blocked: setChecked() would
        // otherwise write that state onto the newly selected separator.
        QSignalBlocker blocker(m_appSepTransparent);
        m_appSepTransparent->setChecked(onSep && appSeparatorTransparent(m_appSepSelected));
    };
    connect(m_appSepTransparent, &QCheckBox::toggled, this, [this](bool on) {
        if (const int which = m_appSepSelected)
            setAppSeparatorTransparent(which, on);
    });
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
                        &DockConfig::separator1TransparentChanged,
                        &DockConfig::separator2TransparentChanged,
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

bool SettingsDialog::appSeparatorTransparent(int which) const
{
    return which == 1 ? m_config->separator1Transparent() : m_config->separator2Transparent();
}

void SettingsDialog::setAppSeparatorTransparent(int which, bool on)
{
    if (which == 1)
        m_config->setSeparator1Transparent(on);
    else
        m_config->setSeparator2Transparent(on);
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
            // Two icons, so the kind is readable without selecting the row.
            const bool clear = appSeparatorTransparent(which);
            auto *item = new QListWidgetItem(
                QIcon::fromTheme(clear ? QStringLiteral("distribute-vertical-page")
                                       : QStringLiteral("distribute-vertical-margin")),
                clear ? tr("── Separator %1 (transparent) ──").arg(which)
                      : tr("── Separator %1 ──").arg(which),
                m_appSepList);
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
    // One counter per kind: the numbering is what tells two otherwise identical
    // separator rows apart, so a transparent one must not borrow the static
    // one's number.
    int springNumber = 0;
    int sepNumber = 0;
    int gapNumber = 0;
    for (int i = 0; i < order.size(); ++i) {
        const QString token = order.at(i);
        if (kHiddenFromLayout.contains(token))
            continue;
        const bool spring = token == QLatin1String("spring");
        const bool gap = token == QLatin1String("gap");
        const bool separator = DockConfig::isRepeatableToken(token);
        // Separators are numbered (each kind on its own count): they are
        // otherwise indistinguishable, so there is no way to tell that Up/Down
        // moved the one that was selected. Renamed sections keep the default
        // name in parentheses, or the list stops saying which widget a row is.
        const QString name = m_config->widgetName(token);
        QString shown;
        if (separator)
            shown = tr("%1 %2").arg(sectionLabel(token))
                        .arg(spring ? ++springNumber : gap ? ++gapNumber : ++sepNumber);
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
                             : gap      ? QStringLiteral("edit-clear-all")
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

namespace {
// The translations directory with the home directory folded back to "~".
QString translationsDirLabel()
{
    QString path = Translations::dirPath();
    const QString home = QDir::homePath();
    if (!home.isEmpty() && path.startsWith(home))
        path.replace(0, home.size(), QStringLiteral("~"));
    return path;
}
} // namespace

void SettingsDialog::reloadTranslationsList()
{
    Translations *layer = Translations::instance();
    if (!m_translationsList || !layer)
        return;
    const QString previous = m_translationsList->currentItem()
                                 ? m_translationsList->currentItem()->data(Qt::UserRole).toString()
                                 : layer->activeName();
    m_translationsList->clear();
    const QString active = layer->activeName();
    for (const QString &name : layer->available()) {
        const bool isActive = (name == active);
        auto *item = new QListWidgetItem(
            isActive ? tr("%1  — en uso").arg(name) : name, m_translationsList);
        item->setData(Qt::UserRole, name);
        if (isActive) {
            QFont bold = item->font();
            bold.setBold(true);
            item->setFont(bold);
            item->setIcon(QIcon::fromTheme(QStringLiteral("dialog-ok")));
        }
        if (name == previous)
            m_translationsList->setCurrentItem(item);
    }
    if (!m_translationsList->currentItem() && m_translationsList->count() > 0)
        m_translationsList->setCurrentRow(0);
}

QWidget *SettingsDialog::createTranslationsTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);
    Translations *layer = Translations::instance();

    auto *info = new QLabel(
        tr("Idioma de la interfaz. Cada traducción es un archivo de texto en "
           "%1, con cuatro títulos: Configuracion (este diálogo), UIdock (menús "
           "y ventanas del dock), Widgets (nombres de los widgets) y Apps "
           "(nombres de aplicaciones, por id de .desktop).\n\n"
           "Lo que una traducción no incluya usa el texto nativo \"capabase\"; "
           "para las apps, el Name= de su .desktop. Un widget renombrado a mano "
           "(solapa Layout) conserva ese nombre en todos los idiomas.")
            // ~ instead of the real home: the label is documentation, and the
            // literal path is one more thing that differs per machine.
            .arg(translationsDirLabel()),
        tab);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *row = new QHBoxLayout;
    m_translationsList = new QListWidget(tab);
    row->addWidget(m_translationsList, 1);

    auto *buttons = new QVBoxLayout;
    auto *edit = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")),
                                 tr("Editar"), tab);
    edit->setToolTip(tr("Abre el archivo con el editor de texto predeterminado. "
                        "Al guardarlo, el dock toma los cambios solo."));
    auto *use = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")),
                                tr("Usar este idioma"), tab);
    auto *refresh = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                    tr("Actualizar apps"), tab);
    refresh->setToolTip(tr("Agrega al archivo los nombres de todas las "
                           "aplicaciones instaladas que todavía no estén."));
    auto *reload = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                   tr("Recargar"), tab);
    auto *create = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")),
                                   tr("Nueva…"), tab);
    create->setToolTip(tr("Copia la traducción seleccionada con otro nombre."));
    for (QPushButton *b : {edit, use, refresh, reload, create})
        buttons->addWidget(b);
    buttons->addStretch();
    row->addLayout(buttons);
    layout->addLayout(row, 1);

    // The selected name, not the row: the list is rebuilt on every change.
    auto selected = [this]() -> QString {
        QListWidgetItem *item = m_translationsList->currentItem();
        return item ? item->data(Qt::UserRole).toString() : QString();
    };

    connect(edit, &QPushButton::clicked, this, [this, selected] {
        const QString name = selected();
        if (name.isEmpty())
            return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(Translations::filePathFor(name)));
    });
    connect(use, &QPushButton::clicked, this, [this, layer, selected] {
        const QString name = selected();
        if (!name.isEmpty())
            layer->setActive(name);
    });
    connect(m_translationsList, &QListWidget::itemDoubleClicked, this,
            [layer](QListWidgetItem *item) {
                layer->setActive(item->data(Qt::UserRole).toString());
            });
    connect(refresh, &QPushButton::clicked, this, [this, layer, selected] {
        const QString name = selected();
        if (name.isEmpty() || !m_apps)
            return;
        const int added = layer->refreshApps(name, m_apps->all());
        QMessageBox::information(
            this, tr("Actualizar apps"),
            added > 0 ? tr("Se agregaron %1 aplicaciones a \"%2\".").arg(added).arg(name)
                      : tr("\"%1\" ya tenía todas las aplicaciones instaladas.").arg(name));
    });
    connect(reload, &QPushButton::clicked, this, [layer] { layer->reload(); });
    connect(create, &QPushButton::clicked, this, [this, layer, selected] {
        const QString base = selected();
        if (base.isEmpty())
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Nueva traducción"),
            tr("Nombre del idioma nuevo (copia de \"%1\"):").arg(base),
            QLineEdit::Normal, QString(), &ok);
        if (!ok)
            return;
        QString error;
        if (!layer->createFrom(base, name, &error))
            QMessageBox::warning(this, tr("Nueva traducción"), error);
        else
            reloadTranslationsList();
    });

    // Rebuilding the whole dialog on a language change would destroy this tab
    // mid-signal, so the list is refreshed through the event loop.
    connect(layer, &Translations::changed, this,
            [this] { QTimer::singleShot(0, this, [this] { reloadTranslationsList(); }); });

    reloadTranslationsList();
    return tab;
}

void SettingsDialog::showTranslationsTab()
{
    if (m_translationsTabIndex >= 0 && m_translationsTabIndex < m_tabWidget->count())
        m_tabWidget->setCurrentIndex(m_translationsTabIndex);
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

void SettingsDialog::reloadWallpaperSnapshot()
{
    if (!m_wallpaperSnapshotList)
        return;
    m_wallpaperSnapshotList->clear();
    const auto snapshot = DesktopWallpapers::snapshot();
    if (snapshot.isEmpty()) {
        m_wallpaperSnapshotList->addItem(
            tr("(todavía sin guardar — se guarda sola la primera vez que salgas "
               "del Escritorio 1)"));
        return;
    }
    // Sorted so the list doesn't reshuffle between refreshes (the snapshot is a
    // hash).
    QStringList screens = snapshot.keys();
    screens.sort();
    for (const QString &screen : std::as_const(screens)) {
        const WallpaperSnapshot &snap = snapshot[screen];
        // The slideshow's folders say more than its current image; a plain
        // image only has the image.
        QString detail = snap.keys.value(QStringLiteral("SlidePaths"));
        if (detail.isEmpty())
            detail = QUrl(snap.keys.value(QStringLiteral("Image"))).toLocalFile();
        m_wallpaperSnapshotList->addItem(
            QStringLiteral("%1 — %2 — %3").arg(screen, snap.plugin, detail));
    }
}

QWidget *SettingsDialog::createWallpapersTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);
    DesktopWallpapers *wallpapers = m_manager ? m_manager->desktopWallpapers() : nullptr;

    auto *info = new QLabel(
        tr("Plasma no tiene un fondo por escritorio virtual: el fondo es de la pantalla, "
           "no del escritorio. kdock lo consigue reescribiéndolo en el momento del cambio. "
           "El Escritorio 1 es de KDE y no se toca desde acá: su configuración se guarda "
           "sola y vuelve cada vez que regresás a él (y cuando kdock se cierra)."),
        tab);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *enable = new QCheckBox(
        tr("Cambiar el fondo al cambiar de escritorio virtual"), tab);
    enable->setChecked(DesktopWallpapers::enabled());
    layout->addWidget(enable);

    auto *fillRow = new QHBoxLayout;
    fillRow->addWidget(new QLabel(tr("Modo de ajuste (imágenes estáticas):"), tab));
    auto *fillMode = new QComboBox(tab);
    // Plasma's org.kde.image FillMode, in its own order.
    fillMode->addItem(tr("Estirada"), 0);
    fillMode->addItem(tr("Escalada, manteniendo proporciones"), 1);
    fillMode->addItem(tr("Escalada y recortada"), 2);
    fillMode->addItem(tr("Mosaico"), 3);
    fillMode->addItem(tr("Mosaico vertical"), 4);
    fillMode->addItem(tr("Mosaico horizontal"), 5);
    fillMode->addItem(tr("Centrada"), 6);
    fillMode->setCurrentIndex(fillMode->findData(DesktopWallpapers::fillMode()));
    fillRow->addWidget(fillMode);
    fillRow->addStretch();
    layout->addLayout(fillRow);

    // ---- Desktop 1: read-only view of what KDE has --------------------------
    auto *kdeGroup = new QGroupBox(tr("Escritorio 1 — configuración de KDE (se conserva siempre)"), tab);
    auto *kdeLayout = new QVBoxLayout(kdeGroup);
    m_wallpaperSnapshotList = new QListWidget(kdeGroup);
    m_wallpaperSnapshotList->setMaximumHeight(90);
    m_wallpaperSnapshotList->setSelectionMode(QAbstractItemView::NoSelection);
    kdeLayout->addWidget(m_wallpaperSnapshotList);

    auto *kdeButtons = new QHBoxLayout;
    auto *recapture = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                      tr("Volver a capturar ahora"), kdeGroup);
    // Capturing while another desktop's wallpaper is up would store *ours* as
    // if it were KDE's, so the button only works from desktop 1.
    const int currentDesktop = m_manager ? m_manager->currentDesktop() : 0;
    recapture->setEnabled(wallpapers && currentDesktop == 1);
    if (currentDesktop != 1) {
        recapture->setToolTip(
            tr("Solo desde el Escritorio 1: es el único momento en que lo que está "
               "en pantalla es la configuración de KDE."));
    }
    auto *restoreNow = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")),
                                       tr("Restaurar ahora"), kdeGroup);
    restoreNow->setEnabled(wallpapers);
    kdeButtons->addWidget(recapture);
    kdeButtons->addWidget(restoreNow);
    kdeButtons->addStretch();
    kdeLayout->addLayout(kdeButtons);
    layout->addWidget(kdeGroup);

    reloadWallpaperSnapshot();

    // ---- Desktops 2..kMaxDesktops: slideshow or static, per desktop ---------
    const QStringList screens = DesktopWallpapers::configuredScreens();
    const QStringList desktopNames = m_manager ? m_manager->desktopNamesForUi() : QStringList();

    // A thumbnail without decoding the whole file: wallpapers are megabytes and
    // there is one per monitor per desktop.
    const auto thumbnailOf = [](const QString &path) -> QPixmap {
        if (path.isEmpty())
            return {};
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QSize size = reader.size();
        if (size.isValid()) {
            size.scale(64, 36, Qt::KeepAspectRatio);
            reader.setScaledSize(size);
        }
        const QImage image = reader.read();
        return image.isNull() ? QPixmap() : QPixmap::fromImage(image);
    };

    // A compact monitor row: tiny thumbnail + read-only path + icon-only
    // choose/clear buttons. `folder` picks the slideshow flavour (folder icon,
    // directory picker) over the static one (image thumbnail, file picker).
    const auto makeMonitorRow = [this, &thumbnailOf](QWidget *parent, int desktop,
                                                     const QString &screen, bool folder) -> QWidget * {
        auto *row = new QWidget(parent);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        auto *thumb = new QLabel(row);
        thumb->setFixedSize(64, 36);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setFrameShape(QFrame::StyledPanel);
        auto *path = new QLineEdit(row);
        path->setReadOnly(true);
        path->setPlaceholderText(folder ? tr("(sin carpeta — este monitor no se toca)")
                                        : tr("(sin imagen — este monitor no se toca)"));
        auto *choose = new QToolButton(row);
        choose->setAutoRaise(true);
        choose->setIcon(QIcon::fromTheme(QStringLiteral("document-open")));
        choose->setToolTip(folder ? tr("Elegir carpeta de imágenes…")
                                  : tr("Elegir imagen de fondo…"));
        auto *clear = new QToolButton(row);
        clear->setAutoRaise(true);
        clear->setIcon(QIcon::fromTheme(QStringLiteral("edit-clear")));
        clear->setToolTip(folder ? tr("Quitar carpeta") : tr("Quitar imagen"));

        rowLayout->addWidget(thumb);
        rowLayout->addWidget(path, 1);
        rowLayout->addWidget(choose);
        rowLayout->addWidget(clear);

        const auto refresh = [thumb, path, clear, desktop, screen, folder, &thumbnailOf] {
            const QString value = folder
                ? DesktopWallpapers::slideshowFolder(desktop, screen)
                : DesktopWallpapers::imageFor(desktop, screen);
            path->setText(value);
            if (folder) {
                thumb->setPixmap(QIcon::fromTheme(QStringLiteral("folder-pictures")).pixmap(32));
            } else {
                const QPixmap pixmap = thumbnailOf(value);
                thumb->setPixmap(pixmap);
                thumb->setText(pixmap.isNull() && !value.isEmpty() ? tr("?") : QString());
            }
            clear->setEnabled(!value.isEmpty());
        };
        refresh();

        connect(choose, &QToolButton::clicked, this, [this, refresh, desktop, screen, folder] {
            if (folder) {
                const QString current = DesktopWallpapers::slideshowFolder(desktop, screen);
                const QString start =
                    current.isEmpty()
                        ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                        : current;
                const QString chosen = QFileDialog::getExistingDirectory(
                    this, tr("Elegir carpeta de imágenes del slideshow"), start);
                if (chosen.isEmpty())
                    return;
                DesktopWallpapers::setSlideshowFolder(desktop, screen, chosen);
            } else {
                const QString current = DesktopWallpapers::imageFor(desktop, screen);
                const QString start =
                    current.isEmpty()
                        ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                        : QFileInfo(current).absolutePath();
                // Native on purpose: in a Plasma session the platform theme
                // serves KDE's own file dialog, which previews images — without
                // kdock taking a dependency on KDE Frameworks.
                const QString chosen = QFileDialog::getOpenFileName(
                    this, tr("Elegir imagen de fondo"), start,
                    tr("Imágenes (*.jpg *.jpeg *.png *.webp *.bmp *.gif *.svg *.svgz)"));
                if (chosen.isEmpty())
                    return;
                DesktopWallpapers::setImageFor(desktop, screen, chosen);
            }
            refresh();
        });
        connect(clear, &QToolButton::clicked, this, [refresh, desktop, screen, folder] {
            if (folder)
                DesktopWallpapers::setSlideshowFolder(desktop, screen, QString());
            else
                DesktopWallpapers::setImageFor(desktop, screen, QString());
            refresh();
        });

        return row;
    };

    for (int desktop = 2; desktop <= DockConfig::kMaxDesktops; ++desktop) {
        const QString name = desktop <= desktopNames.size()
                                 ? desktopNames.at(desktop - 1)
                                 : tr("Escritorio %1").arg(desktop);
        auto *group = new QGroupBox(name, tab);
        auto *v = new QVBoxLayout(group);

        if (screens.isEmpty()) {
            v->addWidget(new QLabel(tr("No hay monitores detectados."), group));
            layout->addWidget(group);
            continue;
        }

        // ---- Mode toggles: Slideshow and Estático, mutually exclusive. ------
        auto *slideshowCheck = new QCheckBox(tr("Slideshow"), group);
        slideshowCheck->setChecked(DesktopWallpapers::slideshowEnabled(desktop));
        auto *intervalLabel = new QLabel(tr("Intervalo:"), group);
        auto *intervalSpin = new QSpinBox(group);
        intervalSpin->setRange(30, 86400);
        intervalSpin->setSingleStep(30);
        intervalSpin->setSuffix(tr(" s"));
        intervalSpin->setValue(DesktopWallpapers::slideshowInterval(desktop));
        intervalSpin->setToolTip(tr("Cada cuántos segundos cambia la imagen del slideshow."));

        auto *toggleRow = new QHBoxLayout;
        toggleRow->addWidget(slideshowCheck);
        toggleRow->addWidget(intervalLabel);
        toggleRow->addWidget(intervalSpin);
        toggleRow->addStretch();
        v->addLayout(toggleRow);

        // Slideshow body: one folder per monitor.
        auto *slideshowBody = new QWidget(group);
        auto *slideshowForm = new QFormLayout(slideshowBody);
        slideshowForm->setContentsMargins(0, 0, 0, 0);
        slideshowForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (const QString &screen : screens)
            slideshowForm->addRow(screen, makeMonitorRow(slideshowBody, desktop, screen, true));
        v->addWidget(slideshowBody);

        // Estático toggle and body: one image per monitor (the original look).
        auto *staticCheck = new QCheckBox(tr("Estático"), group);
        v->addWidget(staticCheck);

        auto *staticBody = new QWidget(group);
        auto *staticForm = new QFormLayout(staticBody);
        staticForm->setContentsMargins(0, 0, 0, 0);
        staticForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (const QString &screen : screens)
            staticForm->addRow(screen, makeMonitorRow(staticBody, desktop, screen, false));
        v->addWidget(staticBody);

        // Exactly one mode is always active: checking a mode turns the other
        // off, and the active mode's body is the only one laid out (space).
        const auto refreshModes = [slideshowCheck, staticCheck, slideshowBody, staticBody,
                                   intervalLabel, intervalSpin] {
            const bool slideshow = slideshowCheck->isChecked();
            staticCheck->setChecked(!slideshow);
            slideshowBody->setVisible(slideshow);
            staticBody->setVisible(!slideshow);
            intervalLabel->setVisible(slideshow);
            intervalSpin->setVisible(slideshow);
        };
        refreshModes();

        connect(slideshowCheck, &QCheckBox::toggled, this,
                [desktop, refreshModes](bool on) {
                    DesktopWallpapers::setSlideshowEnabled(desktop, on);
                    refreshModes();
                });
        connect(staticCheck, &QCheckBox::toggled, this,
                [slideshowCheck, refreshModes](bool on) {
                    // Static is the *absence* of slideshow: nothing to persist,
                    // the slideshow slot already removed the key. Checking it
                    // just turns the slideshow mode off.
                    if (on)
                        slideshowCheck->setChecked(false);
                    refreshModes();
                });
        connect(intervalSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [desktop](int seconds) {
                    DesktopWallpapers::setSlideshowInterval(desktop, seconds);
                });

        layout->addWidget(group);
    }

    auto *applyRow = new QHBoxLayout;
    auto *applyNow = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")),
                                     tr("Aplicar ahora"), tab);
    applyNow->setToolTip(tr("Pone el juego del escritorio actual sin tener que salir y volver."));
    applyNow->setEnabled(wallpapers && currentDesktop >= 2);
    applyRow->addWidget(applyNow);
    applyRow->addStretch();
    layout->addLayout(applyRow);

    layout->addStretch();

    // ---- Wiring -------------------------------------------------------------
    connect(enable, &QCheckBox::toggled, this, [wallpapers](bool on) {
        DesktopWallpapers::setEnabled(on);
        // Turning it on should not wait for the next desktop switch.
        if (on && wallpapers)
            wallpapers->start();
    });
    connect(fillMode, &QComboBox::currentIndexChanged, this, [fillMode](int) {
        DesktopWallpapers::setFillMode(fillMode->currentData().toInt());
    });
    connect(recapture, &QPushButton::clicked, this, [wallpapers] {
        if (wallpapers)
            wallpapers->capture(true);
    });
    connect(restoreNow, &QPushButton::clicked, this, [wallpapers] {
        if (wallpapers)
            wallpapers->restore();
    });
    connect(applyNow, &QPushButton::clicked, this, [this, wallpapers] {
        if (wallpapers && m_manager)
            wallpapers->apply(m_manager->currentDesktop());
    });
    if (wallpapers) {
        // Bound to `tab`, so the connection dies when buildTabs() deletes it.
        connect(wallpapers, &DesktopWallpapers::snapshotChanged, tab,
                [this] { reloadWallpaperSnapshot(); });
    }

    return tab;
}

QWidget *SettingsDialog::createMonitorsTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    // Top: every configured dock, plus a "remove from list" button.
    layout->addWidget(new QLabel(tr("Docks (Doble-click renombra):"), tab));
    // A laptop that has been docked to several setups accumulates docks on
    // outputs that are not plugged in right now; hiding them by default keeps
    // the list about the monitors the user actually has.
    m_hideOfflineDocks = new QCheckBox(tr("Ocultar los docks de monitores desconectados"), tab);
    m_hideOfflineDocks->setChecked(hideOfflinePref(kHideOfflineDocksKey));
    layout->addWidget(m_hideOfflineDocks);
    connect(m_hideOfflineDocks, &QCheckBox::toggled, this, [this](bool on) {
        setHideOfflinePref(kHideOfflineDocksKey, on);
        reloadDocksList();
    });
    m_docksList = new QListWidget(tab);
    // The tab stacks three lists (docks, monitors, desktops). Left to expand,
    // the first two fill the dialog and push the third below the fold, where
    // the user never finds it — so each gets a ceiling and scrolls internally.
    m_docksList->setMaximumHeight(150);
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
    // Same idea as the docks list above. The dock's own monitor is never
    // hidden: its row is the dock's "show here" checkbox.
    m_hideOfflineMonitors = new QCheckBox(tr("Ocultar monitores desconectados"), tab);
    m_hideOfflineMonitors->setChecked(hideOfflinePref(kHideOfflineMonitorsKey));
    layout->addWidget(m_hideOfflineMonitors);
    connect(m_hideOfflineMonitors, &QCheckBox::toggled, this, [this](bool on) {
        setHideOfflinePref(kHideOfflineMonitorsKey, on);
        reloadMonitorsForSelectedDock();
    });
    m_monitorsList = new QListWidget(tab);
    m_monitorsList->setEnabled(false);
    m_monitorsList->setMaximumHeight(120);
    layout->addWidget(m_monitorsList);

    auto *monButtons = new QHBoxLayout;
    monButtons->addStretch();
    m_applyPreviewButton = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")),
                                           tr("Aplicar"), tab);
    monButtons->addWidget(m_applyPreviewButton);
    layout->addLayout(monButtons);

    // Separator
    auto *sep2 = new QLabel(tab);
    sep2->setFrameStyle(QFrame::HLine | QFrame::Sunken);
    layout->addWidget(sep2);

    // Virtual desktops. Unlike the monitors list above there is no preview:
    // another desktop is by definition not on screen, so the change is applied
    // straight away and seen on the next switch.
    auto *desktopsLabel = new QLabel(
        tr("Escritorios virtuales (sin marcar nada, el dock se ve en todos los "
           "escritorios donde su monitor no tenga docks propios):"),
        tab);
    desktopsLabel->setWordWrap(true); // or it widens the tab and forces a scrollbar
    layout->addWidget(desktopsLabel);
    m_desktopsList = new QListWidget(tab);
    m_desktopsList->setEnabled(false);
    m_desktopsList->setMaximumHeight(120); // kMaxDesktops rows and no more
    layout->addWidget(m_desktopsList);

    m_desktopsNote = new QLabel(tab);
    m_desktopsNote->setWordWrap(true);
    layout->addWidget(m_desktopsNote);

    auto *deskButtons = new QHBoxLayout;
    deskButtons->addStretch();
    m_duplicateForDesktopButton =
        new QPushButton(QIcon::fromTheme(QStringLiteral("edit-copy")),
                        tr("Duplicar para el escritorio…"), tab);
    deskButtons->addWidget(m_duplicateForDesktopButton);
    layout->addLayout(deskButtons);
    // Soak up the leftover height, or the layout spreads it as gaps between the
    // three capped lists and the sections drift apart.
    layout->addStretch();

    connect(m_desktopsList, &QListWidget::itemChanged, this, [this](QListWidgetItem *) {
        if (m_selectedTabDockId.isEmpty() || !m_manager)
            return;
        QList<int> desktops;
        for (int row = 0; row < m_desktopsList->count(); ++row) {
            QListWidgetItem *item = m_desktopsList->item(row);
            if (item->checkState() == Qt::Checked)
                desktops << item->data(Qt::UserRole).toInt();
        }
        // The manager re-syncs on dockDesktopsChanged, so the docks appear and
        // disappear live without a restart.
        m_manager->configFor(m_selectedTabDockId)->setDockDesktops(desktops);
        reloadDocksList();
    });

    connect(m_duplicateForDesktopButton, &QPushButton::clicked, this, [this]() {
        if (m_selectedTabDockId.isEmpty() || !m_manager)
            return;
        const QStringList names = m_manager->desktopNamesForUi();
        if (names.isEmpty()) {
            QMessageBox::information(
                this, tr("Escritorios virtuales"),
                tr("KWin no informó ningún escritorio virtual (¿sesión X11 o "
                   "compositor sin escritorios?)."));
            return;
        }
        bool ok = false;
        const QString choice = QInputDialog::getItem(
            this, tr("Duplicar para un escritorio"),
            tr("Copiar \"%1\" a un dock propio del escritorio:")
                .arg(dockLabel(m_selectedTabDockId)),
            names, 0, false, &ok);
        if (!ok)
            return;
        QString err;
        const QString created = m_manager->duplicateDockForDesktop(
            m_selectedTabDockId, names.indexOf(choice) + 1, &err);
        if (created.isEmpty()) {
            QMessageBox::warning(this, tr("Duplicar para un escritorio"), err);
            return;
        }
        m_selectedTabDockId = created; // land on the copy, which is what to edit now
        reloadDocksList();
    });

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
            tr("Alias para \"%1\" (vacío = dejar solo el monitor y el número):")
                .arg(dockLabel(dockId)),
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
    const QStringList connected = m_manager->connectedScreens();
    const bool hideOffline = m_hideOfflineDocks && m_hideOfflineDocks->isChecked();
    QStringList docks;
    for (const QString &id : m_manager->configuredDocks()) {
        // The dock the dialog is editing always shows, even on an unplugged
        // monitor: it is the one the user came here for.
        if (hideOffline && !connected.contains(DockConfig::screenOfDockId(id))
            && id != m_dockId && id != previous)
            continue;
        docks << id;
    }
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

        // Which virtual desktops it belongs to; nothing shown for a base dock,
        // which is the common case (and what every dock was before this).
        const QList<int> desktops = m_manager->configFor(id)->dockDesktops();
        if (!desktops.isEmpty()) {
            QStringList numbers;
            for (int position : desktops)
                numbers << QString::number(position);
            label += tr(" [Escritorio %1]").arg(numbers.join(QStringLiteral("/")));
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
    // The alias and the desktop bindings are edited here, and both show in the
    // top bar.
    reloadDockHeader();
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
        reloadDesktopsForSelectedDock();
        updateMonitorsTabButtons();
        return;
    }
    m_monitorsList->setEnabled(true);
    const QString ownScreen = DockConfig::screenOfDockId(m_selectedTabDockId);
    const QStringList connected = m_manager->connectedScreens();
    const bool enabled = m_manager->isDockEnabled(m_selectedTabDockId);
    const bool hideOffline = m_hideOfflineMonitors && m_hideOfflineMonitors->isChecked();
    for (const QString &screen : m_manager->knownScreensForUi()) {
        if (hideOffline && !connected.contains(screen) && screen != ownScreen)
            continue;
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
    reloadDesktopsForSelectedDock();
    updateMonitorsTabButtons();
}

void SettingsDialog::reloadDesktopsForSelectedDock()
{
    if (!m_desktopsList || !m_manager)
        return;
    const QSignalBlocker block(m_desktopsList);
    m_desktopsList->clear();
    if (m_selectedTabDockId.isEmpty()) {
        m_desktopsList->setEnabled(false);
        if (m_desktopsNote)
            m_desktopsNote->clear();
        return;
    }

    const QStringList names = m_manager->desktopNamesForUi();
    const QList<int> bound = m_manager->configFor(m_selectedTabDockId)->dockDesktops();
    const int current = m_manager->currentDesktop();

    // Positions the dock is bound to that KWin no longer has: the rows still
    // show (so the user can uncheck them) but are marked, the same way the
    // monitors list marks an unplugged output.
    QList<int> positions;
    for (int i = 1; i <= names.size(); ++i)
        positions << i;
    for (int position : bound)
        if (!positions.contains(position))
            positions << position;
    std::sort(positions.begin(), positions.end());

    m_desktopsList->setEnabled(!positions.isEmpty());
    for (int position : std::as_const(positions)) {
        QString label = position <= names.size()
                            ? names.at(position - 1)
                            : tr("Escritorio %1").arg(position);
        if (position > names.size())
            label += tr("  (no existe)");
        else if (position == current)
            label += tr("  — actual");
        auto *item = new QListWidgetItem(label, m_desktopsList);
        item->setData(Qt::UserRole, position);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(bound.contains(position) ? Qt::Checked : Qt::Unchecked);
    }

    if (m_desktopsNote) {
        if (names.isEmpty())
            m_desktopsNote->setText(
                tr("KWin no informó escritorios virtuales; el dock se ve siempre."));
        else if (bound.isEmpty())
            m_desktopsNote->setText(tr("Dock base: se ve en todos los escritorios."));
        else
            m_desktopsNote->setText(
                tr("Mientras este dock esté asignado a un escritorio, reemplaza en él "
                   "a los docks base de %1.")
                    .arg(DockConfig::screenOfDockId(m_selectedTabDockId)));
    }
    if (m_duplicateForDesktopButton)
        m_duplicateForDesktopButton->setEnabled(!m_selectedTabDockId.isEmpty());
}

void SettingsDialog::updateMonitorsTabButtons()
{
    if (m_deleteDockButton)
        m_deleteDockButton->setEnabled(!m_selectedTabDockId.isEmpty());
    if (m_duplicateForDesktopButton)
        m_duplicateForDesktopButton->setEnabled(!m_selectedTabDockId.isEmpty());
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
