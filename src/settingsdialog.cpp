#include "settingsdialog.h"

#include "apprestart.h"
#include "audiocontrol.h"
#include "batterycontrol.h"
#include "brightnesscontrol.h"
#include "screenbrightness.h"
#include "networksettingswidget.h"
#include "appearancecontrol.h"
#include "autocolorscheme.h"
#include "coloredtabbar.h"
#include "desktopentry.h"
#include "dockconfig.h"
#include "dockmanager.h"
#include "dockmodel.h"
#include "desktopwallpapers.h"
#include "lxqtwallpapers.h"
#include "session.h"
#include "iconcolorprovider.h"
#include "relanzadorconfig.h"
#include "relanzadoresmanager.h"
#include "configarchive.h"
#include "iconpickerdialog.h"
#include "themepicker.h"
#include "keyboardcontrol.h"
#include "qtcompat.h"
#include "kwinscripts.h"
#include "previewslauncher.h"
#include "controlmanagerlauncher.h"
#include "desktoplauncher.h"
#include "weatherconfig.h"
#include "weatherlauncher.h"
#include "tilemenulauncher.h"
#include "scriptrunnerconfig.h"
#include "scriptrunnersmanager.h"
#include "systraylauncher.h"
#include "translations.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDialog>
#include <QFormLayout>
#include <QFont>
#include <QGroupBox>
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
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>

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

SettingsDialog::SettingsDialog(DockConfig *config, DesktopEntryIndex *apps,
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
    m_brightness = manager ? manager->brightness() : nullptr;
    m_screens = manager ? manager->screens() : nullptr;
    m_battery = manager ? manager->battery() : nullptr;
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
    m_tabWidget->setObjectName(QStringLiteral("settingsTabs"));

    // Search box above the tab column. QTabWidget's own corner widgets are not
    // sized by the style for the West/East tab shapes (measured: the corner got
    // a zero height), so instead the field lives in a row above the tab widget,
    // pinned to the left at the column's width — visually "at the top of the
    // tab list". With kSearchMinChars or more it filters the tabs down to the
    // ones that contain the text; see applySearchFilter().
    auto *searchRow = new QWidget(this);
    auto *searchRowLayout = new QHBoxLayout(searchRow);
    searchRowLayout->setContentsMargins(0, 0, 0, 2);
    searchRowLayout->setSpacing(0);
    auto *searchBox = new QWidget(searchRow);
    auto *searchLayout = new QVBoxLayout(searchBox);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(2);
    m_searchEdit = new QLineEdit(searchBox);
    m_searchEdit->setObjectName(QStringLiteral("settingsSearch"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setPlaceholderText(tr("Search options…"));
    m_searchEdit->setToolTip(tr("Escribí al menos %1 caracteres para filtrar "
                                "las solapas por sus opciones.").arg(kSearchMinChars));
    searchLayout->addWidget(m_searchEdit);
    m_searchNoResults = new QLabel(searchBox);
    m_searchNoResults->setObjectName(QStringLiteral("settingsSearchNoResults"));
    m_searchNoResults->setWordWrap(true);
    m_searchNoResults->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
    m_searchNoResults->setVisible(false);
    searchLayout->addWidget(m_searchNoResults);
    searchRowLayout->addWidget(searchBox);
    searchRowLayout->addStretch();
    mainLayout->addWidget(searchRow);

    mainLayout->addWidget(m_tabWidget);

    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(150);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { m_searchDebounce->start(); });
    connect(m_searchDebounce, &QTimer::timeout, this, &SettingsDialog::applySearchFilter);
    connect(m_tabWidget, &QTabWidget::currentChanged, this,
            &SettingsDialog::highlightMatchesInTab);

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

    // The dock being edited can be removed from under us: by this dialog's own
    // Docks tab, or by the right-click menu of any dock. removeDock() deletes
    // its DockConfig, so m_config would dangle from then on.
    //
    // `this` is the right context here, unlike everything inside a create*Tab():
    // this connection belongs to the whole dialog, not to a tab that buildTabs()
    // will delete. Deferred, because it usually arrives from inside the handler
    // of the very button that removed the dock, and switching docks rebuilds the
    // tabs — deleting that button while its own lambda is still running.
    if (m_manager) {
        connect(m_manager, &DockManager::dockListChanged, this, [this] {
            QTimer::singleShot(0, this, [this] { ensureEditedDockExists(); });
        });
    }

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
    // Next to DarkMode: a purely visual dock effect (a neon halo around the
    // panels), global with an opt-in-per-monitor list like the Desktop tab.
    addTab(createNeonTab(), tr("Neon"));
    // Next to DarkMode on purpose: it is the other feature that rewrites the
    // desktop's appearance, and the two are interlocked (dark mode suspends it).
    m_colorAutoDefaults = nullptr;
    m_colorAutoTabIndex = -1;
    if (m_manager && m_manager->autoColorScheme()) {
        addTab(createColorAutoTab(), tr("ColorAuto"));
        m_colorAutoTabIndex = m_tabWidget->count() - 1;
    }
    // Third of the tabs that rewrite the desktop's appearance, hence next to
    // the other two: the KDE color scheme and icon set translated onto the LXQt
    // session appearance, plus its font. Both widget members are reset before
    // the tab is rebuilt — a language change deletes every widget the old one
    // owned.
    m_qtCompatForm = nullptr;
    m_qtCompatIcons = nullptr;
    m_qtCompatUiSettings = nullptr;
    // Same reset for the keyboard group, which lives in that tab: its widgets
    // are deleted with it, and the two helpers that refill them decide by
    // whether these are null.
    m_kbLayout = m_kbVariant = m_kbModel = nullptr;
    m_kbOptions = nullptr;
    m_kbStatus = nullptr;
    m_qtCompatTabIndex = -1;
    if (m_manager && m_manager->qtCompat()) {
        addTab(createQtCompatTab(), tr("Modo QT"));
        m_qtCompatTabIndex = m_tabWidget->count() - 1;
    }
    m_audioTabIndex = -1;
    m_audioOutGroup = m_audioInGroup = m_audioAppGroup = nullptr;
    m_audioOutLayout = m_audioInLayout = m_audioAppLayout = nullptr;
    // Same reset for the video tab: buildTabs() runs again on a language
    // change, and every widget the old tabs owned is already deleted.
    m_videoTabIndex = -1;
    m_videoBrightnessLayout = m_videoPowerLayout = nullptr;
    m_videoWheelTarget = nullptr;
    m_videoPowerGroup = nullptr;
    m_videoAllButtons = nullptr;
    addTab(createLayoutTab(), tr("Layout"));
    if (m_relanzadores)
        addTab(createRelanzadoresTab(), tr("Relanzadores"));
    if (m_scriptRunners)
        addTab(createScriptRunnersTab(), tr("Script Runner"));
    addTab(createPresetsTab(), tr("Presets"));
    m_monitorsTabIndex = -1;
    if (m_manager) {
        addTab(createMonitorsTab(), tr("Docks"));
        m_monitorsTabIndex = m_tabWidget->count() - 1;
    }
    // Not a per-dock setting either: it drives the whole session's wallpapers.
    m_wallpaperSnapshotList = nullptr;
    m_wallpapersTabIndex = -1;
    if (m_manager) {
        addTab(createWallpapersTab(), tr("Wallpapers"));
        m_wallpapersTabIndex = m_tabWidget->count() - 1;
    }
    // Not a per-dock setting: the previews are their own process with their own
    // config, so this tab looks the same whichever dock is selected.
    if (PreviewsLauncher::available())
        addTab(createPreviewsTab(), tr("Previews"));
    // The desktop-widget canvas: its own process, its own config, so like
    // Previews the tab looks the same whichever dock is selected.
    if (DesktopLauncher::installed())
        addTab(createDesktopTab(), tr("Desktop"));
    // Audio and Redes last: neither is per-dock, and both are what the volume
    // and network widgets' right-click jump to.
    if (m_audio && m_audio->available()) {
        addTab(createAudioTab(), tr("Audio"));
        m_audioTabIndex = m_tabWidget->count() - 1;
    }
    m_networkTabIndex = -1;
    addTab(createNetworkTab(), tr("Redes"));
    m_networkTabIndex = m_tabWidget->count() - 1;
    // Same idea, for the brightness widget's right-click: per-monitor
    // brightness plus the power profile, none of it per-dock.
    m_videoTabIndex = -1;
    if (m_brightness || m_screens || m_battery) {
        addTab(createVideoTab(), tr("VideoEnergía"));
        m_videoTabIndex = m_tabWidget->count() - 1;
    }
    // Dead last on purpose: it is the tab that decides in which language every
    // other tab is written.
    m_translationsTabIndex = -1;
    if (Translations::instance()) {
        addTab(createTranslationsTab(), tr("Traducciones"));
        m_translationsTabIndex = m_tabWidget->count() - 1;
    }
    applyTabColors();
    // The tabs were just rebuilt (dock switch, language change); re-apply the
    // active search so the filter does not silently reset. The search box lives
    // outside m_tabWidget, so it survives buildTabs().
    applySearchFilter();
    // The search box sits above the tab column; pin it to exactly the column's
    // width so the field does not look like a stub next to the labels. Column
    // width follows the language, hence it is refreshed on every rebuild.
    if (m_searchEdit && m_searchEdit->parentWidget())
        m_searchEdit->parentWidget()->setFixedWidth(m_tabWidget->coloredTabBar()->columnWidth());
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

void SettingsDialog::clearSearch()
{
    if (!m_searchEdit)
        return;
    m_searchEdit->clear();
    applySearchFilter();
}

void SettingsDialog::applySearchFilter()
{
    m_searchQuery = m_searchEdit->text().trimmed();
    const int n = m_tabWidget->count();

    // Below the threshold nothing filters: every tab shows, no "no results"
    // state. Clearing the field (the clear button) lands here too.
    if (m_searchQuery.length() < kSearchMinChars) {
        for (int i = 0; i < n; ++i)
            m_tabWidget->setTabVisible(i, true);
        m_searchNoResults->setVisible(false);
        return;
    }

    // Which tabs contain the query anywhere in their option strings. The tab's
    // own title counts too: typing "Audio" finds the Audio tab without opening
    // it first.
    int firstVisible = -1;
    int visibleCount = 0;
    for (int i = 0; i < n; ++i) {
        QVector<QPair<QString, QWidget *>> strings;
        if (auto *scroll = qobject_cast<QScrollArea *>(m_tabWidget->widget(i)))
            collectTabStrings(scroll->widget(), strings);
        bool match = m_tabWidget->tabText(i).contains(m_searchQuery, Qt::CaseInsensitive);
        for (const auto &s : strings) {
            if (s.first.contains(m_searchQuery, Qt::CaseInsensitive)) {
                match = true;
                break;
            }
        }
        m_tabWidget->setTabVisible(i, match);
        if (match) {
            if (firstVisible < 0)
                firstVisible = i;
            ++visibleCount;
        }
    }

    // Explicit "no results" state: empty tab list + a notice under the search
    // box. Nothing to switch to, so clear the selection (shows an empty page).
    if (visibleCount == 0) {
        m_tabWidget->setCurrentIndex(-1);
        m_searchNoResults->setText(tr("No matches for \"%1\"").arg(m_searchQuery));
        m_searchNoResults->setVisible(true);
        return;
    }
    m_searchNoResults->setVisible(false);
    // The current tab may have been filtered out: move to the first match so
    // the page under the column is not an unrelated tab.
    if (m_tabWidget->currentIndex() < 0
        || !m_tabWidget->isTabVisible(m_tabWidget->currentIndex())) {
        m_tabWidget->setCurrentIndex(firstVisible);
    }
}

void SettingsDialog::highlightMatchesInTab(int index)
{
    if (m_searchQuery.length() < kSearchMinChars || index < 0 || index >= m_tabWidget->count())
        return;
    if (auto *scroll = qobject_cast<QScrollArea *>(m_tabWidget->widget(index))) {
        QVector<QPair<QString, QWidget *>> strings;
        collectTabStrings(scroll->widget(), strings);
        for (const auto &s : strings) {
            if (!s.first.contains(m_searchQuery, Qt::CaseInsensitive))
                continue;
            QWidget *hit = s.second;
            scroll->ensureWidgetVisible(hit, 0, 60);
            // Temporary highlight so the eye lands on the match instead of
            // scanning the tab. Restored on a short timer; the color follows
            // the theme's accent when there is one.
            const QString old = hit->styleSheet();
            const QString accent = m_theme && m_theme->highlight().isValid()
                                       ? m_theme->highlight().name()
                                       : QStringLiteral("#3daee9");
            hit->setStyleSheet(QStringLiteral("background-color:%1; border-radius:4px;")
                                   .arg(accent));
            QTimer::singleShot(1800, hit, [hit, old] { hit->setStyleSheet(old); });
            return;
        }
    }
}

void SettingsDialog::collectTabStrings(const QWidget *root, QVector<QPair<QString, QWidget *>> &out)
{
    if (!root)
        return;
    // One pass over the whole subtree: every widget is classified by its
    // textual role (option label, checkbox/radio, group title, button, combo
    // item) and contributes its strings. A QLabel *inside* a group or a layout
    // is found just like one sitting directly in the form, so nothing that is
    // on screen is missed.
    const QList<QWidget *> widgets = root->findChildren<QWidget *>();
    for (QWidget *w : widgets) {
        if (auto *label = qobject_cast<QLabel *>(w)) {
            if (!label->text().isEmpty())
                out.append({label->text(), label});
        } else if (auto *btn = qobject_cast<QAbstractButton *>(w)) {
            if (!btn->text().isEmpty())
                out.append({btn->text(), btn});
        } else if (auto *group = qobject_cast<QGroupBox *>(w)) {
            if (!group->title().isEmpty())
                out.append({group->title(), group});
        } else if (auto *combo = qobject_cast<QComboBox *>(w)) {
            for (int i = 0; i < combo->count(); ++i)
                out.append({combo->itemText(i), combo});
        }
    }
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

void SettingsDialog::ensureEditedDockExists()
{
    if (!m_manager || m_dockId.isEmpty())
        return;
    const QStringList docks = m_manager->configuredDocks();
    if (docks.contains(m_dockId))
        return;
    // The dock we were editing is gone and its DockConfig with it, so m_config
    // is already dangling: nothing below may read it. selectDock() only
    // overwrites the pointer, and close() touches no config at all.
    if (docks.isEmpty()) {
        close();
        return;
    }
    // Another dock on the same monitor first: that is the one the user was most
    // likely working with, and it leaves the monitor selector where it was
    // instead of teleporting the dialog to some other screen.
    const QString screen = DockConfig::screenOfDockId(m_dockId);
    for (const QString &id : docks) {
        if (DockConfig::screenOfDockId(id) == screen) {
            selectDock(id);
            return;
        }
    }
    selectDock(docks.first());
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
    // Bound to `tab`: buildTabs() deletes it on every dock switch, and a
    // connection left on `this` would pile up one more copy per switch.
    connect(m_config, &DockConfig::showAppIconsChanged, tab, [this] {
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
    // Bound to `tab`, so the connections die when buildTabs() deletes it.
    connect(m_config, &DockConfig::panelModeChanged, tab,
            [updateAlignmentEnabled]() { updateAlignmentEnabled(); });
    connect(m_config, &DockConfig::widgetOrderChanged, tab,
            [updateAlignmentEnabled]() { updateAlignmentEnabled(); });
    connect(m_config, &DockConfig::dockLengthChanged, tab,
            [updateAlignmentEnabled]() { updateAlignmentEnabled(); });
    updateAlignmentEnabled();

    m_dockLength = new QSpinBox(tab);
    m_dockLength->setRange(0, 100);
    m_dockLength->setSingleStep(5);
    m_dockLength->setSuffix(QStringLiteral("%"));
    m_dockLength->setSpecialValueText(tr("Auto"));
    m_dockLength->setValue(m_config->dockLength());
    // No " = " in the string: the translation catalogue splits `key = value` on
    // the first one, so a tooltip written "0 = auto…" enters as the key "0" and
    // can never be translated (tests/static/check-tr-separator.py guards this).
    m_dockLength->setToolTip(tr("0: auto (panel stretches 100% or adjusts to content). "
                                "Above 0: fixed length as a percentage of the screen edge."));
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

    // ---- Appearance section separator -----------------------------------
    {
        auto *sep = new QFrame(tab);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        form->addRow(sep);
    }

    // Qt widget style for the whole kdock suite, persisted in the shared config.
    {
        auto *styleCombo = new QComboBox(tab);
        styleCombo->addItem(tr("(System default)"), QString());
        const QStringList keys = QStyleFactory::keys();
        for (const QString &k : keys)
            styleCombo->addItem(k, k);
        const int idx = styleCombo->findData(DockConfig::qtStyle());
        {
            const QSignalBlocker b(styleCombo);
            styleCombo->setCurrentIndex(idx < 0 ? 0 : idx);
        }
        styleCombo->setToolTip(tr("Qt default (Breeze on KDE, Fusion elsewhere)"));
        connect(styleCombo, &QComboBox::currentIndexChanged, this,
                [this, styleCombo](int i) {
                    const QString val = styleCombo->itemData(i).toString();
                    if (DockConfig::qtStyle() == val)
                        return;
                    DockConfig::setQtStyle(val);
                    QMessageBox::information(
                        this, tr("Qt Style"),
                        tr("El estilo se aplicará la próxima vez que inicies kdock."));
                });
        form->addRow(tr("Qt style:"), styleCombo);
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

    m_hideMode = new QComboBox(tab);
    m_hideMode->addItem(tr("Always visible"), int(DockConfig::AlwaysVisible));
    m_hideMode->addItem(tr("Hide the dock when not in use"), int(DockConfig::AutoHide));
    m_hideMode->addItem(tr("Intelligent hide (hides when a window reaches it)"),
                        int(DockConfig::DodgeWindows));
    m_hideMode->addItem(tr("Windows go below (always visible, no reserved space)"),
                        int(DockConfig::WindowsBelow));
    m_hideMode->setCurrentIndex(qMax(0, m_hideMode->findData(m_config->hideMode())));
    connect(m_hideMode, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_config->setHideMode(m_hideMode->itemData(i).toInt());
    });
    // The auto-hide widget and the dock menu flip the same setting, so the
    // combo re-reads it instead of drifting while the dialog stays open.
    connect(m_config, &DockConfig::hideModeChanged, m_hideMode, [this] {
        const int i = m_hideMode->findData(m_config->hideMode());
        if (i >= 0 && i != m_hideMode->currentIndex()) {
            QSignalBlocker blocker(m_hideMode);
            m_hideMode->setCurrentIndex(i);
        }
    });
    form->addRow(tr("Hide mode:"), m_hideMode);

    // How the dock slides in and out. Shared, not per dock (like the tooltip
    // switch below): it is a feel setting, and setting it once per dock on a
    // machine with a dozen of them is not a setting, it is a chore.
    {
        auto *anim = new QSpinBox(tab);
        anim->setRange(0, DockConfig::kHideAnimationMax);
        anim->setSingleStep(25);
        anim->setSuffix(tr(" ms"));
        anim->setSpecialValueText(tr("Instant"));
        anim->setValue(DockConfig::hideAnimationMs());
        anim->setToolTip(tr("How long the dock takes to slide out of sight and back. "
                            "0: no animation. Applies to every dock."));
        connect(anim, &QSpinBox::valueChanged, this,
                [](int v) { DockConfig::setHideAnimationMs(v); });
        form->addRow(tr("Hide animation:"), anim);

        auto *delay = new QSpinBox(tab);
        delay->setRange(0, DockConfig::kHideDelayMax);
        delay->setSingleStep(50);
        delay->setSuffix(tr(" ms"));
        delay->setSpecialValueText(tr("No delay"));
        delay->setValue(DockConfig::hideDelayMs());
        delay->setToolTip(tr("How long the dock waits, after the pointer leaves it, before "
                             "it starts hiding. Keeps it from disappearing when the pointer "
                             "slips off it for a moment. Applies to every dock."));
        connect(delay, &QSpinBox::valueChanged, this,
                [](int v) { DockConfig::setHideDelayMs(v); });
        form->addRow(tr("Hide delay:"), delay);

        // Both are meaningless while the dock never hides, but they stay
        // readable: the value is what the other docks use too.
        const auto syncHideTiming = [this, anim, delay] {
            const bool hides = m_config->hideMode() == DockConfig::AutoHide
                               || m_config->hideMode() == DockConfig::DodgeWindows;
            anim->setEnabled(hides);
            delay->setEnabled(hides);
        };
        // Bound to `tab`, so the connection dies when buildTabs() deletes it.
        connect(m_config, &DockConfig::hideModeChanged, tab, syncHideTiming);
        syncHideTiming();
    }

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

    // Hover previews of the apps block: a small undecorated window with the
    // capture of the app's window. Shared, like the tooltips above.
    {
        auto *cb = new QCheckBox(tr("Vista previa de la ventana al pasar el mouse"), tab);
        cb->setChecked(DockConfig::appPreview());
        cb->setToolTip(tr("Al pasar el mouse por un ícono de Apps que tenga una ventana "
                          "abierta, muestra una ventanita sin bordes con la captura de esa "
                          "ventana. Necesita KWin, y que el kdock.desktop instalado declare "
                          "X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2. Vale "
                          "para el bloque Apps; cada widget Apps Seleccionables tiene su "
                          "propia casilla, en la solapa Widgets y en su clic derecho."));
        form->addRow(tr("Vista previa:"), cb);

        auto *size = new QSpinBox(tab);
        size->setRange(DockConfig::kAppPreviewSizeMin, DockConfig::kAppPreviewSizeMax);
        size->setSingleStep(20);
        size->setSuffix(tr(" px"));
        size->setValue(DockConfig::appPreviewSize());
        size->setToolTip(tr("Ancho de la vista previa, acá y en los widgets Apps "
                            "Seleccionables. El alto sale de la proporción real de la "
                            "ventana, así que la captura nunca se deforma."));
        connect(size, &QSpinBox::valueChanged, this,
                [](int px) { DockConfig::setAppPreviewSize(px); });
        form->addRow(tr("· Tamaño:"), size);

        auto *delay = new QSpinBox(tab);
        delay->setRange(0, DockConfig::kAppPreviewDelayMax);
        delay->setSingleStep(50);
        delay->setSuffix(tr(" ms"));
        delay->setValue(DockConfig::appPreviewDelayMs());
        // No " = " in a translatable string: the catalog splits on the first one
        // (see tests/static/check-tr-separator.py).
        delay->setToolTip(tr("Cuánto tiene que quedarse el mouse sobre el ícono antes de "
                             "que aparezca la vista previa. Con 0 aparece enseguida."));
        connect(delay, &QSpinBox::valueChanged, this,
                [](int ms) { DockConfig::setAppPreviewDelayMs(ms); });
        form->addRow(tr("· Retardo:"), delay);

        auto *buttons = new QCheckBox(tr("Minimizar, maximizar y cerrar"), tab);
        buttons->setChecked(DockConfig::appPreviewButtons());
        buttons->setToolTip(tr("Dibuja tres botones sobre la esquina de la vista previa, que "
                               "actúan sobre esa ventana. Con o sin botones, la vista previa "
                               "se queda mientras el mouse esté encima de ella, y un clic en "
                               "la captura trae la ventana al frente."));
        connect(buttons, &QCheckBox::toggled, this,
                [](bool on) { DockConfig::setAppPreviewButtons(on); });
        form->addRow(tr("· Botones:"), buttons);

        // The three rows are meaningless with the feature off, and disabling
        // them says so better than a tooltip.
        size->setEnabled(cb->isChecked());
        delay->setEnabled(cb->isChecked());
        buttons->setEnabled(cb->isChecked());
        connect(cb, &QCheckBox::toggled, size, &QWidget::setEnabled);
        connect(cb, &QCheckBox::toggled, delay, &QWidget::setEnabled);
        connect(cb, &QCheckBox::toggled, buttons, &QWidget::setEnabled);
        connect(cb, &QCheckBox::toggled, this,
                [](bool on) { DockConfig::setAppPreview(on); });
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
        accentBtn->setToolTip(tr("Color del resaltado de las apps que están corriendo (que en "
                                 "modo oscuro deja de usar el color de cada ícono y pasa a este "
                                 "único color). Los nombres de apps y widgets también lo usan, "
                                 "salvo que no contraste con el fondo del dock: ahí el dock cae "
                                 "a blanco o negro para que se puedan leer."));
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

    // The weather widget draws a temperature next to its icon — again neither an
    // app name nor a section name, so it gets its own size with the same ladder
    // as the Control Manager text above (own value → clock font → icon size).
    auto *weatherFont = new QSpinBox(tab);
    weatherFont->setRange(0, 96);
    weatherFont->setSpecialValueText(tr("Automatic"));
    weatherFont->setSuffix(tr(" px"));
    weatherFont->setValue(m_config->weatherFontSize());
    weatherFont->setToolTip(tr("Tamaño de la temperatura que el widget del clima dibuja en "
                               "el dock. Automático sigue a la fuente del reloj y, sin ella, "
                               "a una fracción del tamaño del ícono."));
    connect(weatherFont, &QSpinBox::valueChanged, m_config, &DockConfig::setWeatherFontSize);
    connect(m_config, &DockConfig::weatherFontSizeChanged, weatherFont,
            [this, weatherFont] { weatherFont->setValue(m_config->weatherFontSize()); });
    const auto syncWeatherFontEnabled = [this, weatherFont] {
        weatherFont->setEnabled(m_config->showWeather());
    };
    connect(m_config, &DockConfig::showWeatherChanged, tab, syncWeatherFontEnabled);
    syncWeatherFontEnabled();
    form->addRow(tr("Weather text size:"), weatherFont);

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

    // --- Las otras dos superficies de kdock ---------------------------------
    //
    // El panel de control y la ventana del clima son procesos aparte con su
    // propia configuración, y cada uno tiene estos mismos controles en su propio
    // diálogo. Están acá porque "el tamaño de la letra" es lo que más se busca y
    // nadie quiere adivinar en cuál de los tres diálogos vive.
    //
    // Los dos caminos para que el cambio se vea al instante son distintos, y esa
    // diferencia es real: el clima vigila su archivo (lo leen tres procesos),
    // mientras que el panel reescribe el suyo en cada arrastre de tarjeta, así
    // que un watcher ahí dispararía todo el tiempo — se le avisa por D-Bus.
    auto *othersHeader = new QLabel(tr("<b>Panel de control y ventana del clima</b>"), tab);
    form->addRow(othersHeader);

    auto *panelFont = new QSpinBox(tab);
    panelFont->setRange(0, 40);
    panelFont->setSpecialValueText(tr("Automatic"));
    panelFont->setSuffix(tr(" px"));
    panelFont->setValue(ControlManagerLauncher::panelFontSize());
    panelFont->setToolTip(tr("Tamaño de fuente de TODO el panel de control (tarjetas, "
                             "solapas y botones escalan con él). Se aplica en el acto si el "
                             "panel está abierto."));
    panelFont->setEnabled(ControlManagerLauncher::installed());
    connect(panelFont, &QSpinBox::valueChanged, tab, [](int v) {
        ControlManagerLauncher::setPanelFontSize(v);
    });
    form->addRow(tr("Control panel font size:"), panelFont);

    auto *panelBtnW = new QSpinBox(tab);
    panelBtnW->setRange(0, 400);
    panelBtnW->setSingleStep(10);
    panelBtnW->setSpecialValueText(tr("Automatic"));
    panelBtnW->setSuffix(tr(" px"));
    panelBtnW->setValue(ControlManagerLauncher::buttonWidth());
    panelBtnW->setToolTip(tr("Ancho mínimo de los botones del panel de control. Automático "
                             "es el ancho natural de cada botón."));
    panelBtnW->setEnabled(ControlManagerLauncher::installed());
    connect(panelBtnW, &QSpinBox::valueChanged, tab, [](int v) {
        ControlManagerLauncher::setButtonWidth(v);
    });
    form->addRow(tr("Control panel button width:"), panelBtnW);

    auto *panelBtnH = new QSpinBox(tab);
    panelBtnH->setRange(0, 200);
    panelBtnH->setSingleStep(2);
    panelBtnH->setSpecialValueText(tr("Automatic"));
    panelBtnH->setSuffix(tr(" px"));
    panelBtnH->setValue(ControlManagerLauncher::buttonHeight());
    panelBtnH->setToolTip(tr("Alto mínimo de los botones del panel de control."));
    panelBtnH->setEnabled(ControlManagerLauncher::installed());
    connect(panelBtnH, &QSpinBox::valueChanged, tab, [](int v) {
        ControlManagerLauncher::setButtonHeight(v);
    });
    form->addRow(tr("Control panel button height:"), panelBtnH);

    // Una instancia propia: escribir por el setter (y no a mano con QSettings)
    // es lo que hace que el clima corriendo se entere, porque su config vigila
    // el archivo.
    if (!m_weatherConfig)
        m_weatherConfig = new WeatherConfig(this);
    auto *weatherWindowFont = new QSpinBox(tab);
    weatherWindowFont->setRange(0, 40);
    weatherWindowFont->setSpecialValueText(tr("Automatic"));
    weatherWindowFont->setSuffix(tr(" px"));
    weatherWindowFont->setValue(m_weatherConfig->fontSize());
    weatherWindowFont->setToolTip(tr("Tamaño de fuente de la ventana del clima (los íconos "
                                     "acompañan al texto, así que la ventana entera crece con "
                                     "él). Es el mismo ajuste que su propio diálogo."));
    weatherWindowFont->setEnabled(WeatherLauncher::installed());
    connect(weatherWindowFont, &QSpinBox::valueChanged, tab, [this](int v) {
        m_weatherConfig->setFontSize(v);
    });
    connect(m_weatherConfig, &WeatherConfig::changed, weatherWindowFont,
            [this, weatherWindowFont] {
                QSignalBlocker block(weatherWindowFont);
                weatherWindowFont->setValue(m_weatherConfig->fontSize());
            });
    form->addRow(tr("Weather window font size:"), weatherWindowFont);

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

    auto *showWeather = new QCheckBox(tr("Mostrar el clima (ícono y temperatura)"), tab);
    showWeather->setChecked(m_config->showWeather());
    showWeather->setToolTip(tr("El clima se configura en su propia ventana (clic derecho sobre "
                               "el widget → Configurar el clima…): la ciudad, las unidades y "
                               "cada cuánto se actualiza. Los datos son de Open-Meteo."));
    connect(showWeather, &QCheckBox::toggled, m_config, &DockConfig::setShowWeather);
    form->addRow(tr("Clima:"), showWeather);

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
    // (dormant) — see AGENTS.md "Dormant / UI-unreachable code". Its LXQt
    // successor below *is* reachable: it drives kdock's own wallpaper engine,
    // which is the only one there is under LXQt.

    auto *showNextWallpaperQt = new QCheckBox(tr("Show \"Avanzar Wallpaper QT\" button"), tab);
    showNextWallpaperQt->setChecked(m_config->showNextWallpaperQt());
    showNextWallpaperQt->setToolTip(
        tr("Advances the wallpaper kdock draws (Wallpapers tab). Left-click moves "
           "this dock's monitor, right-click every connected monitor — always on "
           "the current virtual desktop only, so the others keep their images. "
           "Shift+right-click opens the widget menu."));
    connect(showNextWallpaperQt, &QCheckBox::toggled, m_config,
            &DockConfig::setShowNextWallpaperQt);
    form->addRow(tr("Avanzar Wallpaper QT:"), showNextWallpaperQt);

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

    auto *showColorAuto = new QCheckBox(tr("Mostrar «Generar Color»"), tab);
    showColorAuto->setChecked(m_config->showColorAuto());
    showColorAuto->setToolTip(tr("Un clic arma un esquema de color con el fondo de este "
                                 "monitor y lo aplica al dock y al sistema, esté ColorAuto "
                                 "activado o no; repetirlo pasa al siguiente color del mismo "
                                 "fondo. Clic derecho: la solapa ColorAuto."));
    connect(showColorAuto, &QCheckBox::toggled, m_config, &DockConfig::setShowColorAuto);
    form->addRow(tr("Generar Color:"), showColorAuto);

    // The system tray now lives in its own resident process (kdock-systray): the
    // "systray" widget is a button that opens that window near the dock. This
    // checkbox is **per-dock** — it decides which docks draw the button (no
    // exclusivity: several buttons harmlessly toggle the one shared window).
    auto *showSystray = new QCheckBox(tr("Mostrar la bandeja del sistema"), tab);
    showSystray->setChecked(m_config->showSystray());
    connect(showSystray, &QCheckBox::toggled, m_config, &DockConfig::setShowSystray);
    form->addRow(tr("System tray:"), showSystray);

    // The remaining two are process-wide (they live in systray.conf): whether
    // kdock brings the resident tray up at startup, and a shortcut to its own
    // settings dialog (window size, edge, icons).
    auto *systrayPreload = new QCheckBox(tr("Precargar la bandeja al iniciar"), tab);
    systrayPreload->setChecked(SystrayLauncher::preload());
    systrayPreload->setToolTip(tr("La bandeja debe estar residente para juntar los íconos de la "
                                  "sesión; se recomienda dejarlo activado."));
    connect(systrayPreload, &QCheckBox::toggled, this,
            [](bool on) { SystrayLauncher::setPreload(on); });
    form->addRow(QString(), systrayPreload);

    auto *systraySettings = new QPushButton(tr("Configurar bandeja…"), tab);
    connect(systraySettings, &QPushButton::clicked, this,
            [] { SystrayLauncher().openSettings(); });
    form->addRow(QString(), systraySettings);

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

    // Bound to `tab`, so the connection dies when buildTabs() deletes it. The
    // lambda (rather than the slot pointer) is what lets the receiver be the tab
    // while the work still runs on the dialog; m_writingPinned is what keeps our
    // own setPinned() from rebuilding the list under the user's hands.
    connect(m_config, &DockConfig::pinnedChanged, tab, [this] {
        if (!m_writingPinned)
            reloadPinnedList();
    });
    reloadPinnedList();

    // One panel per selectable-apps widget. Rebuilt in place, because the set of
    // widgets is edited from the *Layout* tab and changing tabs does not
    // reconstruct the dialog.
    m_appsWidgetsBox = new QGroupBox(tr("Selectable apps"), tab);
    m_appsWidgetsLayout = new QVBoxLayout(m_appsWidgetsBox);
    layout->addWidget(m_appsWidgetsBox);
    rebuildAppsWidgetsGroup();

    return tab;
}

void SettingsDialog::rebuildAppsWidgetsGroup()
{
    if (!m_appsWidgetsLayout)
        return;
    while (QLayoutItem *item = m_appsWidgetsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const QStringList tokens = m_config->appsWidgetTokens();
    m_appsWidgetsBox->setVisible(true);
    if (tokens.isEmpty()) {
        auto *hint = new QLabel(
            tr("No selectable-apps widget on this dock. Add one from the Layout tab "
               "(\"Add selectable apps\"): it is a block of app icons with its own "
               "list, and a dock can hold several."),
            m_appsWidgetsBox);
        hint->setWordWrap(true);
        m_appsWidgetsLayout->addWidget(hint);
        return;
    }

    for (const QString &token : tokens)
        m_appsWidgetsLayout->addWidget(createAppsWidgetPanel(token));
}

QWidget *SettingsDialog::createAppsWidgetPanel(const QString &token)
{
    auto *box = new QGroupBox(m_config->widgetName(token), m_appsWidgetsBox);
    auto *layout = new QVBoxLayout(box);

    // The three flags in one row, the filters first: they are what decides what
    // this widget picks up, and they only have an effect on what "Show pinned
    // only" lets through.
    auto *flags = new QHBoxLayout;
    auto *excludeOthers = new QCheckBox(tr("Skip apps pinned in other Selectable apps"), box);
    excludeOthers->setChecked(m_config->widgetExcludeOthers(token));
    excludeOthers->setToolTip(
        tr("The widget does not draw a window whose app is in another Selectable apps "
           "widget's list. With \"Show pinned only\" off, that turns this one into the "
           "catch-all block: everything that is open and is not already drawn "
           "somewhere else. The apps of its own list below are always drawn."));
    connect(excludeOthers, &QCheckBox::toggled, this,
            [this, token](bool on) { m_config->setWidgetExcludeOthers(token, on); });

    auto *onlyPinned = new QCheckBox(tr("Show pinned only"), box);
    onlyPinned->setChecked(m_config->widgetOnlyPinned(token));
    onlyPinned->setToolTip(tr("On: the widget draws exactly the apps below. Off: it also "
                              "draws every open window, like \"Show applications\"."));
    connect(onlyPinned, &QCheckBox::toggled, this,
            [this, token](bool on) { m_config->setWidgetOnlyPinned(token, on); });

    auto *excludeMonitor = new QCheckBox(tr("Skip apps pinned on this monitor"), box);
    excludeMonitor->setChecked(m_config->widgetExcludeMonitor(token));
    excludeMonitor->setToolTip(
        tr("The same filter, one screen wide: the widget skips a window whose app is in "
           "the list of any Selectable apps widget of any dock on this monitor. It is the "
           "wider version of the checkbox on the left, so turning it on turns that one off. "
           "Other monitors are never looked at. The apps of its own list below are always "
           "drawn."));
    connect(excludeMonitor, &QCheckBox::toggled, this,
            [this, token](bool on) { m_config->setWidgetExcludeMonitor(token, on); });

    // Independent of the three above: it does not change *which* windows the
    // widget takes, only whether they share an icon. Its only condition is the
    // dock-wide "Group windows" (a widget can ungroup further, never regroup),
    // which is why it is disabled — and says so — while that one is off.
    auto *ungroup = new QCheckBox(tr("Ungroup windows"), box);
    ungroup->setChecked(m_config->widgetUngroupWindows(token));
    const auto syncUngroup = [this, ungroup] {
        const bool grouping = m_config->groupWindows();
        ungroup->setEnabled(grouping);
        ungroup->setToolTip(
            grouping ? tr("One icon per window instead of one per application: a browser with "
                          "two windows, or two Konsole instances, draw two icons in this widget. "
                          "The extra icons come after the pinned ones and carry the same name; "
                          "the window title is in the tooltip. Windows of the apps below are "
                          "drawn even with \"Show pinned only\" on.")
                     : tr("The dock already ungroups every window: turn on \"Group windows of "
                          "the same application\" above for this to mean anything."));
    };
    syncUngroup();
    connect(m_config, &DockConfig::groupWindowsChanged, ungroup,
            [syncUngroup] { syncUngroup(); });
    connect(ungroup, &QCheckBox::toggled, this,
            [this, token](bool on) { m_config->setWidgetUngroupWindows(token, on); });

    flags->addWidget(excludeOthers);
    flags->addWidget(excludeMonitor);
    flags->addWidget(onlyPinned);
    flags->addStretch();
    layout->addLayout(flags);

    // On a row of its own, and not only because it is not a filter: the three
    // above already measure ~700 px and a QHBoxLayout of checkboxes cannot
    // shrink below its labels, so a fourth one pushed the panel past the
    // dialog's width (measured 862 against 785) and brought a scrollbar with it.
    // Hover previews, per widget. **Independent of the General tab's checkbox**,
    // which governs the apps block and nothing else: previews on this widget and
    // on nothing else is a valid setup, so there is no master to disable it
    // against. Size and delay are the shared ones (General, once).
    auto *preview = new QCheckBox(tr("Vista previa de la ventana"), box);
    preview->setChecked(m_config->widgetAppPreview(token));
    preview->setToolTip(
        tr("Al pasar el mouse por un ícono de este widget que tenga una ventana abierta, "
           "muestra una ventanita sin bordes con la captura de esa ventana. Es "
           "independiente de la casilla de la solapa General, que solo vale para el "
           "bloque Apps; el tamaño y el retardo sí salen de ahí. Con esto apagado el "
           "widget no le pide ni una captura a KWin."));
    connect(preview, &QCheckBox::toggled, this,
            [this, token](bool on) { m_config->setWidgetAppPreview(token, on); });

    auto *ungroupRow = new QHBoxLayout;
    ungroupRow->addWidget(ungroup);
    ungroupRow->addWidget(preview);
    ungroupRow->addStretch();
    layout->addLayout(ungroupRow);

    // The filters are what the other checkbox lets through, so they say nothing
    // while the widget is a fixed list of icons. The monitor-wide one being the
    // superset of the dock-local one is settled in setWidgetExcludeMonitor(),
    // which unsets the other: the same pair of checkboxes lives in the dock's
    // right-click menu, and a rule written in one of the two UIs drifts.
    const auto syncFlags = [excludeOthers, excludeMonitor, onlyPinned] {
        const bool live = !onlyPinned->isChecked();
        excludeMonitor->setEnabled(live);
        excludeOthers->setEnabled(live && !excludeMonitor->isChecked());
    };
    connect(onlyPinned, &QCheckBox::toggled, box, [syncFlags](bool) { syncFlags(); });
    connect(excludeMonitor, &QCheckBox::toggled, box, [syncFlags](bool) { syncFlags(); });

    // Config -> checkbox, the direction this panel used to do without: these
    // four flags are now editable from the widget's own right-click menu too, so
    // an open dialog would show state that is no longer true (and the setter
    // above unsets excludeOthers by itself). setChecked() on an unchanged value
    // emits nothing, so this cannot bounce.
    const auto follow = [this, token, box](void (DockConfig::*sig)(const QString &),
                                           QCheckBox *cb, bool (DockConfig::*get)(const QString &) const) {
        connect(m_config, sig, box, [this, token, cb, get](const QString &changed) {
            if (changed == token)
                cb->setChecked((m_config->*get)(token));
        });
    };
    follow(&DockConfig::widgetOnlyPinnedChanged, onlyPinned, &DockConfig::widgetOnlyPinned);
    follow(&DockConfig::widgetExcludeOthersChanged, excludeOthers, &DockConfig::widgetExcludeOthers);
    follow(&DockConfig::widgetExcludeMonitorChanged, excludeMonitor, &DockConfig::widgetExcludeMonitor);
    follow(&DockConfig::widgetUngroupWindowsChanged, ungroup, &DockConfig::widgetUngroupWindows);
    follow(&DockConfig::widgetAppPreviewChanged, preview, &DockConfig::widgetAppPreview);
    syncFlags();

    auto *list = new QListWidget(box);
    list->setMaximumHeight(140);
    layout->addWidget(list);

    // The list is rebuilt from the config after every edit, so the buttons never
    // work from what the widget happens to show.
    const auto reload = [this, token, list] {
        const int row = list->currentRow();
        list->clear();
        for (const QString &id : m_config->widgetApps(token)) {
            const DesktopEntry entry = m_apps->byId(id);
            auto *item = new QListWidgetItem(
                QIcon::fromTheme(entry.isValid() ? entry.icon
                                                 : QStringLiteral("application-x-executable")),
                entry.isValid() ? entry.name : id, list);
            item->setData(Qt::UserRole, id);
        }
        list->setCurrentRow(qMin(row, list->count() - 1));
    };
    reload();
    // Pinning from the dock's own right-click writes the same list: without
    // this the panel goes stale the moment the user uses the widget.
    connect(m_config, &DockConfig::widgetAppsChanged, list,
            [token, reload](const QString &changed) {
                if (changed == token)
                    reload();
            });

    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Add..."), box);
    auto *remove = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")),
                                   tr("Remove"), box);
    auto *up = new QPushButton(QIcon::fromTheme(QStringLiteral("go-up")), tr("Up"), box);
    auto *down = new QPushButton(QIcon::fromTheme(QStringLiteral("go-down")), tr("Down"), box);
    // Same action as "Recargar este widget" in the widget's own right-click
    // menu: re-read the monitor's lists and rebuild this one.
    auto *reloadBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                      tr("Reload"), box);
    reloadBtn->setToolTip(tr("Re-reads the pinned lists of every Selectable apps widget of this "
                          "monitor and redraws this one. For when a change made elsewhere did "
                          "not reach it: the reload is manual so nothing has to poll."));
    connect(reloadBtn, &QPushButton::clicked, this,
            [this, token] { m_config->reloadAppsWidget(token); });
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addWidget(reloadBtn);
    buttons->addStretch();
    buttons->addWidget(up);
    buttons->addWidget(down);
    layout->addLayout(buttons);

    connect(add, &QPushButton::clicked, this, [this, token] {
        const QList<DesktopEntry> entries = m_apps->all();
        QStringList names;
        for (const DesktopEntry &e : entries)
            names.append(e.name);
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, tr("Add app"), tr("Application:"),
                                                     names, 0, false, &ok);
        if (!ok)
            return;
        const int idx = names.indexOf(chosen);
        if (idx < 0)
            return;
        QStringList apps = m_config->widgetApps(token);
        if (!apps.contains(entries[idx].id)) {
            apps.append(entries[idx].id);
            m_config->setWidgetApps(token, apps);
        }
    });
    connect(remove, &QPushButton::clicked, this, [this, token, list] {
        QListWidgetItem *item = list->currentItem();
        if (!item)
            return;
        QStringList apps = m_config->widgetApps(token);
        apps.removeAll(item->data(Qt::UserRole).toString());
        m_config->setWidgetApps(token, apps);
    });
    const auto move = [this, token, list](int delta) {
        const int row = list->currentRow();
        QStringList apps = m_config->widgetApps(token);
        if (row < 0 || row + delta < 0 || row + delta >= apps.size())
            return;
        apps.move(row, row + delta);
        m_config->setWidgetApps(token, apps);
        list->setCurrentRow(row + delta);
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(1); });

    return box;
}

void SettingsDialog::rebuildGapsGroup()
{
    if (!m_gapsLayout)
        return;
    while (QLayoutItem *item = m_gapsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const QStringList tokens = m_config->gapTokens();
    if (tokens.isEmpty()) {
        auto *hint = new QLabel(
            tr("No transparent separator on this dock. Add one with \"Add transparent "
               "separator\" above: it leaves a hole in the dock's background, so the "
               "desktop shows through and the dock reads as two."),
            m_gapsBox);
        hint->setWordWrap(true);
        m_gapsLayout->addWidget(hint);
        return;
    }

    for (const QString &token : tokens)
        m_gapsLayout->addWidget(createGapRow(token));
}

QWidget *SettingsDialog::createGapRow(const QString &token)
{
    auto *row = new QWidget(m_gapsBox);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *name = new QLabel(m_config->widgetName(token), row);
    name->setMinimumWidth(180);
    layout->addWidget(name);

    auto *fixed = new QCheckBox(tr("Fixed width"), row);
    fixed->setChecked(m_config->gapFixedWidth(token));
    fixed->setToolTip(tr("Off: the separator expands like a dynamic one and pushes the "
                         "sections apart. On: it measures exactly the width below, so the "
                         "hole is a gap of its own between two blocks of dock."));
    connect(fixed, &QCheckBox::toggled, this,
            [this, token](bool on) { m_config->setGapFixedWidth(token, on); });
    layout->addWidget(fixed);

    auto *size = new QSpinBox(row);
    // The range lives in DockConfig, next to the value it clamps.
    size->setRange(DockConfig::kGapMinSize, DockConfig::kGapMaxSize);
    size->setSuffix(tr(" px"));
    size->setValue(m_config->gapSize(token));
    connect(size, &QSpinBox::valueChanged, this,
            [this, token](int px) { m_config->setGapSize(token, px); });
    layout->addWidget(size);
    layout->addStretch();

    // The width says nothing while the separator expands.
    const auto sync = [fixed, size] { size->setEnabled(fixed->isChecked()); };
    connect(fixed, &QCheckBox::toggled, row, [sync](bool) { sync(); });
    sync();

    // The dock's own right-click menu writes the same keys, and a rename shows
    // up in the label: without this the panel goes stale behind the user's back.
    connect(m_config, &DockConfig::gapsChanged, row, [this, token, fixed, size, sync] {
        QSignalBlocker b1(fixed), b2(size);
        fixed->setChecked(m_config->gapFixedWidth(token));
        size->setValue(m_config->gapSize(token));
        sync();
    });
    connect(m_config, &DockConfig::widgetNamesChanged, name,
            [this, token, name] { name->setText(m_config->widgetName(token)); });

    return row;
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

    // Bound to `tab`, same as the pinned list above.
    connect(m_config, &DockConfig::menuFavoritesChanged, tab, [this] {
        if (!m_writingFavorites)
            reloadFavoritesList();
    });
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

QWidget *SettingsDialog::createDesktopTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    auto *info = new QLabel(
        tr("Los widgets de escritorio son una capa transparente a pantalla completa, por "
           "debajo del dock y de las ventanas, donde se colocan tarjetas (reloj, clima, "
           "sistema…). Corre un kdock-desktop por monitor, cada uno con su propia "
           "configuración independiente. Marcá abajo en qué monitores querés uno."),
        tab);
    info->setWordWrap(true);
    layout->addWidget(info);

    // --- master switch: off = ninguno corre ---
    auto *master = new QCheckBox(tr("Activar widgets de escritorio"), tab);
    master->setChecked(DesktopLauncher::masterEnabled());
    master->setToolTip(tr("Interruptor general. Apagado, no corre ningún escritorio en ningún "
                          "monitor, sin perder qué monitores tenés marcados."));
    layout->addWidget(master);

    // --- per-monitor list ---
    layout->addWidget(new QLabel(tr("Monitores (marcá dónde querés un escritorio):"), tab));
    auto *monitors = new QListWidget(tab);
    monitors->setMaximumHeight(150);
    layout->addWidget(monitors);

    auto *row = new QHBoxLayout;
    auto *configureBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")),
                                         tr("Configurar este monitor…"), tab);
    auto *restartBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                       tr("Reiniciar este monitor"), tab);
    row->addWidget(configureBtn);
    row->addWidget(restartBtn);
    row->addStretch();
    layout->addLayout(row);

    auto *status = new QLabel(tab);
    status->setWordWrap(true);
    layout->addWidget(status);

    // The connectors to list: connected monitors only (an unplugged one has no
    // canvas anyway). DockManager is the same source the Docks tab uses.
    const auto connectors = [this]() -> QStringList {
        if (m_manager)
            return m_manager->connectedScreens();
        QStringList names;
        for (QScreen *s : QGuiApplication::screens())
            names << s->name();
        return names;
    };

    const auto selectedConnector = [monitors]() -> QString {
        QListWidgetItem *it = monitors->currentItem();
        return it ? it->data(Qt::UserRole).toString() : QString();
    };

    // Rebuild the checkable monitor rows from the config.
    const auto reload = [this, monitors, master] {
        const bool on = master->isChecked();
        QSignalBlocker block(monitors);
        const QString keep = monitors->currentItem()
                                 ? monitors->currentItem()->data(Qt::UserRole).toString()
                                 : QString();
        monitors->clear();
        const QStringList list = m_manager ? m_manager->connectedScreens() : QStringList();
        QStringList names = list;
        if (names.isEmpty())
            for (QScreen *s : QGuiApplication::screens())
                names << s->name();
        for (const QString &c : names) {
            auto *item = new QListWidgetItem(c, monitors);
            item->setData(Qt::UserRole, c);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(DesktopLauncher::screenEnabled(c) ? Qt::Checked
                                                                  : Qt::Unchecked);
            if (keep == c)
                monitors->setCurrentItem(item);
        }
        // Master off greys the list: the marks are kept, nothing runs.
        monitors->setEnabled(on);
    };
    reload();

    const auto refreshStatus = [status, restartBtn, configureBtn, selectedConnector] {
        int n = 0;
        for (QScreen *s : QGuiApplication::screens())
            if (DesktopLauncher::runningOn(s->name()))
                ++n;
        status->setText(n == 0 ? tr("Estado: ningún escritorio en ejecución")
                               : tr("Estado: %n escritorio(s) en ejecución", nullptr, n));
        const QString c = selectedConnector();
        const bool sel = !c.isEmpty();
        configureBtn->setEnabled(sel);
        restartBtn->setEnabled(sel);
        restartBtn->setText(sel && DesktopLauncher::runningOn(c) ? tr("Reiniciar este monitor")
                                                                 : tr("Lanzar este monitor"));
    };
    refreshStatus();

    connect(master, &QCheckBox::toggled, this, [reload, refreshStatus](bool on) {
        DesktopLauncher::setMasterEnabled(on);
        DesktopLauncher::applyState();
        reload();
        refreshStatus();
    });

    connect(monitors, &QListWidget::itemChanged, this,
            [refreshStatus](QListWidgetItem *item) {
                const QString c = item->data(Qt::UserRole).toString();
                DesktopLauncher::setScreenEnabled(c, item->checkState() == Qt::Checked);
                // Reconcile just this monitor (the master gates it inside applyState).
                DesktopLauncher::applyState();
                QTimer::singleShot(600, item->listWidget(), refreshStatus);
            });
    connect(monitors, &QListWidget::currentRowChanged, tab,
            [refreshStatus](int) { refreshStatus(); });

    connect(configureBtn, &QPushButton::clicked, this,
            [this, selectedConnector, refreshStatus] {
                const QString c = selectedConnector();
                if (c.isEmpty())
                    return;
                DesktopLauncher::openSettingsOn(c);
                QTimer::singleShot(600, this, refreshStatus);
            });
    connect(restartBtn, &QPushButton::clicked, this,
            [this, selectedConnector, refreshStatus] {
                const QString c = selectedConnector();
                if (c.isEmpty())
                    return;
                DesktopLauncher::restartOn(c);
                QTimer::singleShot(900, this, refreshStatus);
            });

    // Bound to `tab`, so the timer dies when buildTabs() deletes the tab. Only
    // the status polls; rebuilding the list under the user would flicker the
    // selection. A monitor hot-plug is picked up on the next open of the tab.
    auto *poll = new QTimer(tab);
    poll->setInterval(2000);
    connect(poll, &QTimer::timeout, tab, refreshStatus);
    poll->start();

    layout->addStretch();
    return tab;
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
    makeSection(tr("Audio cards"), m_audioCardsGroup, m_audioCardsLayout);
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

    clearLayout(m_audioCardsLayout);
    const auto cards = m_audio->cards();
    for (const AudioControl::Card &card : cards) {
        auto *row = new QWidget(m_audioCardsGroup);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);

        auto *label = new QLabel(card.description, row);
        label->setToolTip(card.name);
        label->setMinimumWidth(150);
        h->addWidget(label, 1);

        auto *combo = new QComboBox(row);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        // The active profile is what the sinks/sources above actually are. A
        // profile whose ports are all unplugged is useless, so only the
        // available ones are offered — except when the active profile itself is
        // not available (a port got unplugged while selected), in which case it
        // is shown disabled so the combo reflects reality instead of lying.
        bool activeShown = false;
        for (const AudioControl::Profile &p : card.profiles) {
            if (!p.available && p.name != card.activeProfile)
                continue;
            const int idx = combo->count();
            combo->addItem(p.description, p.name);
            if (p.name == card.activeProfile) {
                combo->setCurrentIndex(idx);
                activeShown = true;
            }
        }
        if (!activeShown && !card.activeProfile.isEmpty()) {
            const int idx = combo->count();
            combo->addItem(card.activeProfile);
            combo->setItemData(idx, card.activeProfile, Qt::UserRole);
            combo->setCurrentIndex(idx);
            // Show it but grey it out: its port is gone, it is only the truth.
            if (auto *m = qobject_cast<QStandardItemModel *>(combo->model()))
                if (auto *item = m->item(idx))
                    item->setEnabled(false);
        }
        if (combo->count() == 0) {
            combo->addItem(tr("No profiles"));
            combo->setEnabled(false);
        }
        combo->setToolTip(tr("Switch the card's active profile. Only ports that "
                             "are physically connected are offered; the HDMI "
                             "output of a plugged-in monitor is one of these."));
        const int cardIndex = card.index;
        connect(combo, &QComboBox::activated, this, [this, combo, cardIndex](int index) {
            const QString name = combo->itemData(index, Qt::UserRole).toString();
            if (m_audio && !name.isEmpty())
                m_audio->setCardProfile(cardIndex, name);
        });
        h->addWidget(combo);

        m_audioCardsLayout->addWidget(row);
    }
    m_audioCardsGroup->setVisible(!cards.isEmpty());
}

// Brightness of every monitor + the power profile, backed by ScreenBrightness
// (PowerDevil), BrightnessControl (brightnessctl) and BatteryControl
// (power-profiles-daemon). Invoked by the dock's brightness-widget right-click
// (DockWindow::openVideoSettings -> showVideoTab).
//
// The dock widget itself only ever drives ONE monitor — the wheel is the
// equivalent of the volume wheel on the default sink — so this tab is the only
// place the other screens can be dimmed, and where that one monitor is chosen.
QWidget *SettingsDialog::createVideoTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    auto *brightGroup = new QGroupBox(tr("Brillo de los monitores"), tab);
    m_videoBrightnessLayout = new QVBoxLayout(brightGroup);
    m_videoBrightnessLayout->setSpacing(4);
    layout->addWidget(brightGroup);

    // "Todos al …": only useful with more than one monitor, so rebuildVideoTab()
    // shows and hides it.
    m_videoAllButtons = new QWidget(brightGroup);
    {
        auto *h = new QHBoxLayout(m_videoAllButtons);
        h->setContentsMargins(0, 0, 0, 0);
        auto *all100 = new QPushButton(tr("Todos al 100 %"), m_videoAllButtons);
        auto *all50 = new QPushButton(tr("Todos al 50 %"), m_videoAllButtons);
        connect(all100, &QPushButton::clicked, this, [this] {
            if (m_screens)
                m_screens->setAll(1.0);
        });
        connect(all50, &QPushButton::clicked, this, [this] {
            if (m_screens)
                m_screens->setAll(0.5);
        });
        h->addWidget(all100);
        h->addWidget(all50);
        h->addStretch(1);
    }
    m_videoBrightnessLayout->addWidget(m_videoAllButtons);

    auto *wheelGroup = new QGroupBox(tr("Rueda del widget de brillo"), tab);
    auto *wheelForm = new QFormLayout(wheelGroup);
    m_videoWheelTarget = new QComboBox(wheelGroup);
    m_videoWheelTarget->setToolTip(
        tr("La rueda sobre el widget del dock cambia el brillo de este monitor y de ningún "
           "otro. Los demás se ajustan desde acá."));
    wheelForm->addRow(tr("Monitor:"), m_videoWheelTarget);
    connect(m_videoWheelTarget, &QComboBox::activated, this, [this](int index) {
        if (m_brightness && index >= 0)
            m_brightness->setWheelTarget(m_videoWheelTarget->itemData(index).toString());
    });
    layout->addWidget(wheelGroup);

    m_videoPowerGroup = new QGroupBox(tr("Perfil de energía"), tab);
    m_videoPowerLayout = new QVBoxLayout(m_videoPowerGroup);
    m_videoPowerLayout->setSpacing(4);
    layout->addWidget(m_videoPowerGroup);

    layout->addStretch(1);

    rebuildVideoTab();

    // Bound to `tab`, so the connections die when buildTabs() deletes it.
    if (m_screens)
        connect(m_screens, &ScreenBrightness::changed, tab, [this] { scheduleVideoRebuild(); });
    if (m_brightness)
        connect(m_brightness, &BrightnessControl::changed, tab,
                [this] { scheduleVideoRebuild(); });
    if (m_battery)
        connect(m_battery, &BatteryControl::changed, tab, [this] { scheduleVideoRebuild(); });
    return tab;
}

void SettingsDialog::scheduleVideoRebuild()
{
    if (m_videoRebuildQueued)
        return;
    m_videoRebuildQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_videoRebuildQueued = false;
        rebuildVideoTab();
    });
}

QWidget *SettingsDialog::makeBrightnessRow(QWidget *parent, const QString &label, qreal value,
                                           std::function<void(qreal)> setter)
{
    auto *row = new QWidget(parent);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);

    const int percent = qRound(value * 100.0);

    auto *icon = new QLabel(row);
    // breeze has no -medium, so the split is at half (same as the panel's card).
    icon->setPixmap(QIcon::fromTheme(percent < 50 ? QStringLiteral("brightness-low")
                                                  : QStringLiteral("brightness-high"))
                        .pixmap(16, 16));
    h->addWidget(icon);

    auto *name = new QLabel(label, row);
    name->setToolTip(label);
    name->setMinimumWidth(150);
    h->addWidget(name, 1);

    auto *slider = new QSlider(Qt::Horizontal, row);
    // Never all the way down: a screen at 0 looks broken and cannot be found
    // again with the mouse. Same floor the backends clamp to.
    slider->setRange(qRound(BrightnessControl::MinBrightness * 100), 100);
    {
        QSignalBlocker blk(slider);
        slider->setValue(percent);
    }
    slider->setMinimumWidth(150);

    auto *pct = new QLabel(row);
    pct->setMinimumWidth(46);
    pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pct->setText(QString::number(percent) + QStringLiteral(" %"));

    connect(slider, &QSlider::sliderPressed, this, [this] { m_videoSliderDown = true; });
    connect(slider, &QSlider::sliderReleased, this, [this] {
        m_videoSliderDown = false;
        // Deferred: we're inside this slider's own signal, and a direct rebuild
        // would delete it under our feet.
        scheduleVideoRebuild();
    });
    connect(slider, &QSlider::valueChanged, this, [pct, setter](int v) {
        pct->setText(QString::number(v) + QStringLiteral(" %"));
        setter(v / 100.0);
    });
    h->addWidget(slider);
    h->addWidget(pct);
    return row;
}

void SettingsDialog::rebuildVideoTab()
{
    if (!m_videoBrightnessLayout)
        return;
    if (m_videoSliderDown)
        return; // don't yank a slider handle out from under the user mid-drag

    const auto clearLayout = [](QVBoxLayout *lay, QWidget *keep) {
        for (int i = lay->count() - 1; i >= 0; --i) {
            QLayoutItem *item = lay->itemAt(i);
            if (item->widget() && item->widget() == keep)
                continue;
            delete lay->takeAt(i)->widget();
        }
    };

    // --- one row per monitor -------------------------------------------------
    clearLayout(m_videoBrightnessLayout, m_videoAllButtons);

    const QVariantList displays = m_screens ? m_screens->displays() : QVariantList();
    bool haveInternalDisplay = false;
    int inserted = 0;
    for (const QVariant &v : displays) {
        const QVariantMap d = v.toMap();
        const QString name = d.value(QStringLiteral("name")).toString();
        QString label = d.value(QStringLiteral("label")).toString();
        if (label.isEmpty())
            label = name;
        if (d.value(QStringLiteral("internal")).toBool())
            haveInternalDisplay = true;
        QWidget *row = makeBrightnessRow(
            m_videoBrightnessLayout->parentWidget(), label, d.value(QStringLiteral("value")).toReal(),
            [this, name](qreal value) {
                if (m_screens)
                    m_screens->setBrightness(name, value);
            });
        m_videoBrightnessLayout->insertWidget(inserted++, row);
    }

    // PowerDevil usually reports only the DDC monitors, so without this row the
    // laptop panel would have no control at all in a docked session.
    const bool internalRow = m_brightness && m_brightness->internalAvailable()
                             && !haveInternalDisplay;
    if (internalRow) {
        QWidget *row = makeBrightnessRow(m_videoBrightnessLayout->parentWidget(),
                                         tr("Pantalla interna"),
                                         m_brightness->internalBrightness(), [this](qreal value) {
                                             if (m_brightness)
                                                 m_brightness->setInternalBrightness(value);
                                         });
        m_videoBrightnessLayout->insertWidget(inserted++, row);
    }

    if (inserted == 0) {
        auto *none = new QLabel(tr("Ni PowerDevil ni brightnessctl responden."),
                                m_videoBrightnessLayout->parentWidget());
        none->setWordWrap(true);
        m_videoBrightnessLayout->insertWidget(inserted++, none);
    }
    m_videoAllButtons->setVisible(displays.size() > 1);

    // --- which monitor the wheel drives -------------------------------------
    if (m_videoWheelTarget) {
        QSignalBlocker blk(m_videoWheelTarget);
        m_videoWheelTarget->clear();
        m_videoWheelTarget->addItem(tr("Automático (la pantalla interna)"), QString());
        for (const QVariant &v : displays) {
            const QVariantMap d = v.toMap();
            const QString label = d.value(QStringLiteral("label")).toString();
            // Keyed by label and not by the D-Bus object name, which PowerDevil
            // renumbers whenever a monitor sleeps (see screenbrightness.h).
            if (!label.isEmpty())
                m_videoWheelTarget->addItem(label, label);
        }
        if (internalRow)
            m_videoWheelTarget->addItem(tr("Pantalla interna"), BrightnessControl::InternalTarget);
        const QString target = m_brightness ? m_brightness->wheelTarget() : QString();
        const int idx = m_videoWheelTarget->findData(target);
        m_videoWheelTarget->setCurrentIndex(qMax(0, idx));
        m_videoWheelTarget->setEnabled(m_brightness != nullptr);
    }

    // --- power profile -------------------------------------------------------
    if (m_videoPowerLayout) {
        clearLayout(m_videoPowerLayout, nullptr);
        const QStringList profiles = m_battery ? m_battery->profiles() : QStringList();
        m_videoPowerGroup->setVisible(m_battery && m_battery->profilesAvailable()
                                      && !profiles.isEmpty());
        auto *bg = new QButtonGroup(m_videoPowerGroup);
        for (const QString &profile : profiles) {
            // Same three names the control panel's Video card uses.
            const QString title = profile == QLatin1String("power-saver") ? tr("Ahorro")
                                  : profile == QLatin1String("balanced")  ? tr("Equilibrado")
                                  : profile == QLatin1String("performance")
                                      ? tr("Rendimiento")
                                      : profile;
            auto *radio = new QRadioButton(title, m_videoPowerGroup);
            radio->setChecked(m_battery->activeProfile() == profile);
            bg->addButton(radio);
            connect(radio, &QRadioButton::clicked, this, [this, profile] {
                if (m_battery)
                    m_battery->setProfile(profile);
            });
            m_videoPowerLayout->addWidget(radio);
        }
        if (m_battery && m_battery->available()) {
            auto *state = new QLabel(m_battery->tooltipText(), m_videoPowerGroup);
            state->setWordWrap(true);
            m_videoPowerLayout->addWidget(state);
        }
    }
}

void SettingsDialog::showVideoTab()
{
    // The target tab may be hidden by the search: these right-click arrivals
    // are explicit "show me this" gestures, so drop the filter first.
    clearSearch();
    if (m_videoTabIndex >= 0 && m_videoTabIndex < m_tabWidget->count())
        m_tabWidget->setCurrentIndex(m_videoTabIndex);
}

void SettingsDialog::showColorAutoTab()
{
    clearSearch();
    if (m_colorAutoTabIndex >= 0 && m_colorAutoTabIndex < m_tabWidget->count())
        m_tabWidget->setCurrentIndex(m_colorAutoTabIndex);
}

void SettingsDialog::showQtCompatTab()
{
    clearSearch();
    if (m_qtCompatTabIndex >= 0 && m_qtCompatTabIndex < m_tabWidget->count())
        m_tabWidget->setCurrentIndex(m_qtCompatTabIndex);
}

void SettingsDialog::showWallpapersTab()
{
    clearSearch();
    if (m_wallpapersTabIndex >= 0 && m_wallpapersTabIndex < m_tabWidget->count())
        m_tabWidget->setCurrentIndex(m_wallpapersTabIndex);
}

void SettingsDialog::showAudioTab()
{
    clearSearch();
    if (m_audioTabIndex >= 0 && m_audioTabIndex < m_tabWidget->count())
        m_tabWidget->setCurrentIndex(m_audioTabIndex);
}

void SettingsDialog::showNetworkTab()
{
    clearSearch();
    if (m_networkTabIndex >= 0 && m_networkTabIndex < m_tabWidget->count())
        m_tabWidget->setCurrentIndex(m_networkTabIndex);
}

void SettingsDialog::showMonitorsTab(const QString &dockId)
{
    clearSearch();
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
    accentBtn->setToolTip(tr("Color del resaltado de las apps que están corriendo (que en "
                             "modo oscuro deja de usar el color de cada ícono y pasa a este "
                             "único color). Los nombres de apps y widgets también lo usan, "
                             "salvo que no contraste con el fondo del dock: ahí el dock cae "
                             "a blanco o negro para que se puedan leer."));
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

    // "Activate neon with dark mode": lights the neon halo whenever dark mode is
    // on, even if the neon master (Neon tab) is off. Read-time link, see
    // DockConfig::neonActive(). The per-monitor list of the Neon tab still
    // applies; with no monitors picked there, it lights all of them.
    auto *neonWithDark = new QCheckBox(tr("Encender el brillo neón al entrar en modo oscuro"), tab);
    neonWithDark->setChecked(DockConfig::neonWithDarkMode());
    neonWithDark->setToolTip(tr("Con esto tildado, el modo oscuro enciende el neón aunque el "
                                "interruptor de la solapa Neon esté apagado. Respeta los monitores "
                                "marcados en la solapa Neon; si no marcaste ninguno, se enciende en "
                                "todos."));
    connect(neonWithDark, &QCheckBox::toggled, this,
            [](bool on) { DockConfig::setNeonWithDarkMode(on); });
    form->addRow(tr("Brillo neón:"), neonWithDark);
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

void SettingsDialog::addColorAutoIconsetRow(QFormLayout *form, QWidget *parent, bool dark,
                                            const QString &title, const QString &tip)
{
    if (!m_appearance)
        return;
    auto *check = new QCheckBox(title, parent);
    check->setChecked(AutoColorScheme::iconsetEnabled(dark));
    check->setToolTip(tip);

    auto *pick = new ThemePickerButton(m_appearance, QStringLiteral("icons"),
                                       ThemePickerPopup::PickValue, parent);
    // Never leave the picker sitting on whatever sorts first: with no explicit
    // empty-id row, ticking the checkbox would quietly apply *that* icon set.
    pick->setSpecialEntry(tr("(usar el guardado)"));
    pick->setCurrentId(AutoColorScheme::iconsetValue(dark));
    pick->setEnabled(check->isChecked());

    connect(check, &QCheckBox::toggled, this, [dark, pick](bool on) {
        // Persist what the row shows before turning it on, so the switch uses
        // the value the user is looking at.
        if (on)
            AutoColorScheme::setIconsetValue(dark, pick->currentId());
        AutoColorScheme::setIconsetEnabled(dark, on);
        pick->setEnabled(on);
    });
    connect(pick, &ThemePickerButton::picked, this,
            [dark](const QString &id) { AutoColorScheme::setIconsetValue(dark, id); });

    form->addRow(check);
    form->addRow(tr("· Iconset:"), pick);
}

void SettingsDialog::reloadColorAutoDefaults()
{
    if (!m_colorAutoDefaults)
        return;
    if (!AutoColorScheme::defaultsSaved()) {
        m_colorAutoDefaults->setText(
            tr("Todavía no se guardó nada: se captura al activar la casilla."));
        return;
    }
    const QString colors = AutoColorScheme::defaultColorScheme();
    const QString icons = AutoColorScheme::defaultIconTheme();
    m_colorAutoDefaults->setText(
        tr("Esquema de color: <b>%1</b> — Iconset del dock: <b>%2</b>")
            .arg(colors.isEmpty() ? tr("(sin definir)") : colors,
                 icons.isEmpty() ? tr("(seguir el del sistema)") : icons));
}

QWidget *SettingsDialog::createNeonTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    auto *info = new QLabel(
        tr("El brillo neón dibuja un halo luminoso alrededor de los paneles del dock. "
           "Es un efecto global, activable por monitor: marcá abajo en qué monitores lo "
           "querés. El color, la intensidad y el tamaño del halo se ajustan acá."),
        tab);
    info->setWordWrap(true);
    layout->addWidget(info);

    // --- master switch: off = ningún halo en ningún dock ---
    auto *master = new QCheckBox(tr("Activar Neon"), tab);
    master->setChecked(DockConfig::neonEnabled());
    master->setToolTip(tr("Interruptor general. Apagado, no se dibuja ningún halo, sin perder "
                          "qué monitores tenés marcados."));
    layout->addWidget(master);

    // --- appearance: color + two sliders ---
    auto *lookBox = new QGroupBox(tr("Apariencia del halo"), tab);
    auto *lookForm = new QFormLayout(lookBox);

    auto *colorBtn = new QPushButton(lookBox);
    const auto refreshColor = makeColorButton(colorBtn, &DockConfig::neonColorSetting,
                                              &DockConfig::setNeonColor, tr("Color del neón"));
    colorBtn->setToolTip(tr("Color del halo."));
    lookForm->addRow(tr("Color:"), colorBtn);

    // Intensity 0..1 <-> slider 0..100.
    auto *intensity = new QSlider(Qt::Horizontal, lookBox);
    intensity->setRange(0, 100);
    intensity->setValue(qRound(DockConfig::neonIntensitySetting() * 100.0));
    auto *intensityVal = new QLabel(lookBox);
    const auto showIntensity = [intensityVal](int v) {
        intensityVal->setText(QStringLiteral("%1 %").arg(v));
    };
    showIntensity(intensity->value());
    auto *intensityRow = new QHBoxLayout;
    intensityRow->addWidget(intensity, 1);
    intensityRow->addWidget(intensityVal);
    lookForm->addRow(tr("Intensidad:"), intensityRow);

    // Size in px; a modest ceiling keeps the halo from swamping the panel.
    auto *size = new QSlider(Qt::Horizontal, lookBox);
    size->setRange(0, 40);
    size->setValue(qRound(DockConfig::neonSizeSetting()));
    auto *sizeVal = new QLabel(lookBox);
    const auto showSize = [sizeVal](int v) {
        sizeVal->setText(QStringLiteral("%1 px").arg(v));
    };
    showSize(size->value());
    auto *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(size, 1);
    sizeRow->addWidget(sizeVal);
    lookForm->addRow(tr("Tamaño:"), sizeRow);

    // Render style: the inward rim (default) or an outward halo. The halo needs
    // transparent room around the panel, so a floating dock glows on all four
    // sides while one flush to the edge glows on the free ones; it does not run
    // in the auto-hiding modes (falls back to the rim there).
    auto *styleCombo = new QComboBox(lookBox);
    styleCombo->addItem(tr("Rim (contorno interior)"));
    styleCombo->addItem(tr("Halo exterior"));
    styleCombo->setCurrentIndex(DockConfig::neonGlowMode() == 1 ? 1 : 0);
    styleCombo->setToolTip(tr("Rim: el resplandor se derrama hacia adentro del panel. "
                              "Halo exterior: se derrama hacia afuera (un dock flotante brilla "
                              "por los cuatro lados; no corre en los modos de auto-ocultar)."));
    lookForm->addRow(tr("Estilo:"), styleCombo);

    layout->addWidget(lookBox);

    // --- per-monitor list (same idiom as the Desktop tab) ---
    layout->addWidget(new QLabel(tr("Monitores (marcá dónde querés el halo):"), tab));
    auto *monitors = new QListWidget(tab);
    monitors->setMaximumHeight(150);
    layout->addWidget(monitors);

    // Rebuild the checkable monitor rows from the config.
    const auto reload = [this, monitors, master] {
        const bool on = master->isChecked();
        QSignalBlocker block(monitors);
        monitors->clear();
        QStringList names = m_manager ? m_manager->connectedScreens() : QStringList();
        if (names.isEmpty())
            for (QScreen *s : QGuiApplication::screens())
                names << s->name();
        for (const QString &c : names) {
            auto *item = new QListWidgetItem(c, monitors);
            item->setData(Qt::UserRole, c);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(DockConfig::neonScreenEnabled(c) ? Qt::Checked : Qt::Unchecked);
        }
        // Master off greys the list: the marks are kept, nothing glows.
        monitors->setEnabled(on);
    };
    reload();

    connect(master, &QCheckBox::toggled, this, [reload](bool on) {
        DockConfig::setNeonEnabled(on);
        reload();
    });
    connect(monitors, &QListWidget::itemChanged, this, [](QListWidgetItem *item) {
        const QString c = item->data(Qt::UserRole).toString();
        DockConfig::setNeonScreenEnabled(c, item->checkState() == Qt::Checked);
    });

    connect(intensity, &QSlider::valueChanged, this, [showIntensity](int v) {
        showIntensity(v);
        DockConfig::setNeonIntensity(v / 100.0);
    });
    connect(size, &QSlider::valueChanged, this, [showSize](int v) {
        showSize(v);
        DockConfig::setNeonSize(v);
    });
    connect(styleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [](int idx) { DockConfig::setNeonGlowMode(idx); });

    layout->addStretch();
    return tab;
}

QWidget *SettingsDialog::createColorAutoTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);
    // Under LXQt both ends of this feature are somebody else's: the wallpaper
    // comes from kdock's own engine instead of from plasmashell, and the scheme
    // only reaches the session's Qt applications through Modo QT. Neither is
    // visible from here, so the tab has to say so — see the status lines in the
    // system group below. Same branch idiom as the Wallpapers tab.
    const bool lxqt = Session::isLxqt();

    auto *intro = new QLabel(
        lxqt ? tr("Genera un esquema de color a partir del color predominante del fondo "
                  "de pantalla y lo aplica al cambiar de fondo. El esquema es "
                  "<b>temporal</b>: se reescribe en cada cambio y se borra al desactivar "
                  "esto. Las fuentes y los botones se calculan por contraste, así que el "
                  "resultado se lee sea cual sea la foto.<br><br>"
                  "Acá el fondo lo lee de las imágenes que <b>dibuja el propio kdock</b> "
                  "(solapa Wallpapers): bajo LXQt no hay plasmashell a quien preguntarle, "
                  "y el fondo de PCManFM es uno solo para toda la sesión, así que no "
                  "serviría para darle un color a cada monitor.")
             : tr("Genera un esquema de color de KDE a partir del color predominante del "
                  "fondo de pantalla y lo aplica al cambiar de fondo. El esquema es "
                  "<b>temporal</b>: se reescribe en cada cambio y se borra al desactivar "
                  "esto. Las fuentes y los botones se calculan por contraste, así que el "
                  "resultado se lee sea cual sea la foto."),
        tab);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    auto *warn = new QLabel(
        tr("<i>Esto le cambia la apariencia a todo el escritorio, no solo al dock. Se "
           "configura una vez para todos los docks. Mientras el modo oscuro esté "
           "activo, ColorAuto se apaga solo y vuelve al salir.</i>"),
        tab);
    warn->setWordWrap(true);
    layout->addWidget(warn);

    // --- Manual actions ----------------------------------------------------
    // Deliberately ABOVE the master switch: these two work with ColorAuto
    // switched off, which is the whole point of them (same as the dock widget
    // and the panel card).
    auto *manualBox = new QGroupBox(tr("A mano"), tab);
    auto *manualLayout = new QVBoxLayout(manualBox);
    auto *manualRow = new QHBoxLayout;

    auto *generate = new QPushButton(QIcon::fromTheme(QStringLiteral("color-management")),
                                     tr("Generar Color"), manualBox);
    generate->setToolTip(tr("Genera un esquema del fondo actual y lo aplica al dock y al "
                            "sistema, esté ColorAuto activado o no. Apretarlo otra vez sobre "
                            "el mismo fondo pasa al siguiente color del mismo, así que se "
                            "puede ir probando hasta que guste."));
    auto *save = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")),
                                 tr("Guardar Color Scheme"), manualBox);
    save->setToolTip(tr("Guarda el esquema que está puesto como uno permanente "
                        "(kdock-1, kdock-2…) en tu carpeta de esquemas, y lo aplica. A "
                        "partir de ahí es tuyo: ni apagar ColorAuto ni reiniciar el dock "
                        "lo pisan."));
    manualRow->addWidget(generate);
    manualRow->addWidget(save);
    manualRow->addStretch();
    manualLayout->addLayout(manualRow);

    // Preview of the scheme on offer: the same three swatches (window, text,
    // selection) the theme picker draws for any other scheme, so a generated
    // one is judged the same way as an installed one — without applying it
    // first and then having to undo it.
    auto *previewRow = new QHBoxLayout;
    auto *previewSwatches = new QLabel(manualBox);
    auto *previewText = new QLabel(manualBox);
    previewRow->addWidget(new QLabel(tr("Color generado:"), manualBox));
    previewRow->addWidget(previewSwatches);
    previewRow->addWidget(previewText, 1);
    manualLayout->addLayout(previewRow);

    const auto refreshPreview = [this, previewSwatches, previewText] {
        const QVariantMap entry =
            m_manager && m_manager->autoColorScheme()
                ? m_manager->autoColorScheme()->previewEntry()
                : QVariantMap();
        if (entry.isEmpty()) {
            previewSwatches->clear();
            previewText->setText(tr("(todavía no se generó ninguno)"));
            return;
        }
        previewSwatches->setPixmap(
            themePreviewPixmap(false, entry, previewSwatches->devicePixelRatioF()));
        previewText->setText(tr("fondo %1 · texto %2 · selección %3")
                                 .arg(entry.value(QStringLiteral("bg")).toString().toUpper(),
                                      entry.value(QStringLiteral("fg")).toString().toUpper(),
                                      entry.value(QStringLiteral("sel")).toString().toUpper()));
    };

    auto *manualStatus = new QLabel(manualBox);
    manualStatus->setWordWrap(true);
    manualLayout->addWidget(manualStatus);

    connect(generate, &QPushButton::clicked, this, [this, manualStatus] {
        if (!m_manager || !m_manager->autoColorScheme())
            return;
        // Set **before** generating, not after. Under LXQt the wallpaper source
        // is a plain call, so generateNow() finishes — changed() and all —
        // before it returns, and setting the text afterwards would leave
        // "Generando…" on screen for ever over a scheme that is already up.
        // The changed() handler below is what clears it, on both paths.
        manualStatus->setText(tr("Generando…"));
        m_manager->autoColorScheme()->generateNow();
    });
    connect(save, &QPushButton::clicked, this, [this, manualStatus, lxqt] {
        if (!m_manager || !m_manager->autoColorScheme())
            return;
        const QString id = m_manager->autoColorScheme()->saveCurrentScheme();
        if (!id.isEmpty()) {
            manualStatus->setText(tr("Guardado y aplicado como <b>%1</b>.").arg(id));
            return;
        }
        // Empty means two different things depending on the path, and telling
        // the user to press again is only right on one of them. Under Plasma
        // saving generates first and the generation is a D-Bus round trip, so
        // there genuinely is nothing to write yet. With a synchronous source
        // that generation already finished, so an empty id means it produced
        // nothing at all — which under LXQt means no wallpaper to read.
        manualStatus->setText(
            lxqt ? tr("No se pudo generar ningún esquema: no hay ningún fondo que leer. "
                      "Fijate el estado de <b>Fondos</b>, más abajo.")
                 : tr("Todavía no hay nada generado: se generó uno ahora, "
                      "volvé a apretar Guardar."));
    });
    // Generating is a D-Bus round trip, so the new colors only exist a moment
    // later: repainting the preview from the click handler would keep showing
    // the previous scheme. AutoColorScheme::changed is emitted once the new one
    // is in place, which is the only correct moment.
    if (m_manager && m_manager->autoColorScheme()) {
        connect(m_manager->autoColorScheme(), &AutoColorScheme::changed, manualBox,
                [refreshPreview, manualStatus] {
                    // Clearing here and not in the click handler is what makes
                    // "Generando…" correct on both paths: it goes away when the
                    // scheme is actually up, whether that was synchronous (an
                    // injected source) or a D-Bus round trip later. The save
                    // handler runs *after* its own changed(), so its message
                    // survives this.
                    manualStatus->clear();
                    refreshPreview();
                });
    }
    refreshPreview();
    layout->addWidget(manualBox);

    auto *topForm = new QFormLayout;

    auto *enabled = new QCheckBox(tr("Activar ColorAuto"), tab);
    enabled->setChecked(AutoColorScheme::enabled());
    enabled->setToolTip(tr("Regenera el esquema solo, cada vez que cambia el fondo. Con esto "
                           "apagado los ajustes de abajo se siguen usando: valen igual para "
                           "«Generar Color», el widget del dock y la tarjeta del panel."));
    topForm->addRow(tr("ColorAuto:"), enabled);

    auto *lightness = new QComboBox(tab);
    lightness->addItem(tr("Automático: según el fondo"),
                       int(WallpaperColors::Options::Auto));
    lightness->addItem(tr("Siempre claro"), int(WallpaperColors::Options::ForceLight));
    lightness->addItem(tr("Siempre oscuro"), int(WallpaperColors::Options::ForceDark));
    lightness->setCurrentIndex(qMax(0, lightness->findData(AutoColorScheme::lightness())));
    lightness->setToolTip(tr("Con Automático manda la luminancia media de la imagen. Las "
                             "otras dos existen porque una foto justo en el límite hace "
                             "titilar el esquema entre claro y oscuro en cada paso del "
                             "pase de diapositivas."));
    connect(lightness, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, lightness](int) {
                AutoColorScheme::setLightness(lightness->currentData().toInt());
                if (m_manager && m_manager->autoColorScheme())
                    m_manager->autoColorScheme()->refreshNow();
            });
    topForm->addRow(tr("Claridad:"), lightness);
    layout->addLayout(topForm);

    // Everything below the master switch. Only a container for the layout —
    // it is deliberately never disabled, see syncEnabled() at the end.
    auto *body = new QWidget(tab);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(body);

    // --- Docks -------------------------------------------------------------
    auto *docksBox = new QGroupBox(tr("Colorear docks"), body);
    auto *docksForm = new QFormLayout(docksBox);
    auto *colorDocks = new QCheckBox(tr("Aplicar a los docks, por monitor"), docksBox);
    colorDocks->setChecked(AutoColorScheme::colorDocks());
    colorDocks->setToolTip(tr("Cada dock toma los colores del fondo de SU monitor. Como el "
                              "modo oscuro, es un override momentáneo: el color de panel "
                              "que configuraste sigue guardado y vuelve al desactivar."));
    connect(colorDocks, &QCheckBox::toggled, this, [this](bool on) {
        AutoColorScheme::setColorDocks(on);
        if (m_manager && m_manager->autoColorScheme())
            m_manager->autoColorScheme()->refreshNow();
    });
    docksForm->addRow(colorDocks);
    bodyLayout->addWidget(docksBox);

    // --- System ------------------------------------------------------------
    auto *sysBox = new QGroupBox(lxqt ? tr("Esquema del sistema") : tr("Esquema del sistema (KDE)"),
                                 body);
    auto *sysForm = new QFormLayout(sysBox);
    auto *systemScheme = new QCheckBox(tr("Cambiar el esquema de color de todo el escritorio"),
                                       sysBox);
    systemScheme->setChecked(AutoColorScheme::systemScheme());
    sysForm->addRow(systemScheme);

    auto *monitor = new QComboBox(sysBox);
    monitor->addItem(tr("(el del widget de Brillo)"), QString());
    monitor->addItem(tr("(el interno / principal)"), AutoColorScheme::InternalMonitor);
    for (const QString &name : DesktopWallpapers::configuredScreens())
        monitor->addItem(name, name);
    monitor->setCurrentIndex(qMax(0, monitor->findData(AutoColorScheme::systemMonitor())));
    monitor->setToolTip(tr("De qué monitor sale el fondo que manda: el esquema del sistema "
                           "es uno solo. Por omisión sigue al monitor que maneja la rueda "
                           "del brillo; si ese monitor no se puede identificar, elegilo "
                           "acá a mano."));
    connect(monitor, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, monitor](int) {
                AutoColorScheme::setSystemMonitor(monitor->currentData().toString());
                if (m_manager && m_manager->autoColorScheme())
                    m_manager->autoColorScheme()->refreshNow();
            });
    sysForm->addRow(tr("Monitor que manda:"), monitor);
    connect(systemScheme, &QCheckBox::toggled, this, [this, monitor](bool on) {
        AutoColorScheme::setSystemScheme(on);
        monitor->setEnabled(on);
        if (m_manager && m_manager->autoColorScheme())
            m_manager->autoColorScheme()->refreshNow();
    });
    monitor->setEnabled(systemScheme->isChecked());

    // --- The two things this depends on under LXQt --------------------------
    //
    // Neither is visible from this tab and each can silently make the whole
    // feature look broken: with no wallpaper engine there is nothing to sample,
    // and with Modo QT off the scheme stops at kdock and the KDE applications.
    // Both are read live rather than described in prose, because "it depends on
    // another tab" is exactly the sentence a user cannot act on.
    if (lxqt) {
        const auto statusRow = [sysBox, sysForm](const QString &label, QLabel **out,
                                                 const QString &jump) {
            auto *host = new QWidget(sysBox);
            auto *row = new QHBoxLayout(host);
            row->setContentsMargins(0, 0, 0, 0);
            *out = new QLabel(host);
            (*out)->setWordWrap(true);
            (*out)->setTextFormat(Qt::RichText);
            row->addWidget(*out, 1);
            auto *button = new QPushButton(jump, host);
            row->addWidget(button);
            sysForm->addRow(label, host);
            return button;
        };

        QLabel *wallStatus = nullptr;
        QPushButton *wallJump = statusRow(tr("Fondos:"), &wallStatus, tr("Ir a Wallpapers"));
        connect(wallJump, &QPushButton::clicked, this, &SettingsDialog::showWallpapersTab);

        QLabel *qtStatus = nullptr;
        QPushButton *qtJump = statusRow(tr("Modo QT:"), &qtStatus, tr("Ir a Modo QT"));
        connect(qtJump, &QPushButton::clicked, this, &SettingsDialog::showQtCompatTab);

        const auto refreshStatus = [this, wallStatus, qtStatus] {
            LxqtWallpapers *engine = m_manager ? m_manager->lxqtWallpapers() : nullptr;
            // active() and not enabled(): the master switch is shared with the
            // Plasma engine, so a config carried over from there arrives with it
            // already on while nothing of ours is drawing.
            const int count = engine ? engine->currentImages().size() : 0;
            if (engine && engine->active() && count > 0) {
                wallStatus->setText(tr("los dibuja kdock — %n monitor(es) con fondo para "
                                       "leer.", nullptr, count));
            } else if (engine && engine->active()) {
                wallStatus->setText(tr("<b>los dibuja kdock, pero no hay ninguna imagen "
                                       "configurada</b>: no hay nada que leer."));
            } else {
                wallStatus->setText(tr("<b>apagados</b> — el escritorio es de PCManFM y "
                                       "ColorAuto no tiene ningún fondo que leer."));
            }

            const bool on = QtCompat::enabled() && QtCompat::colorsEnabled();
            qtStatus->setText(on ? tr("activado — el esquema llega también a las apps Qt "
                                      "de LXQt.")
                                 : tr("<b>apagado</b> — el esquema llega al dock y a las "
                                      "apps de KDE, pero no a las demás apps Qt."));
        };

        if (QtCompat *compat = m_manager ? m_manager->qtCompat() : nullptr)
            connect(compat, &QtCompat::changed, sysBox, refreshStatus);
        if (LxqtWallpapers *engine = m_manager ? m_manager->lxqtWallpapers() : nullptr)
            connect(engine, &LxqtWallpapers::activeChanged, sysBox, refreshStatus);
        // A generation is also the moment the wallpaper count can have changed
        // (a slideshow step, a monitor plugged in).
        if (m_manager && m_manager->autoColorScheme()) {
            connect(m_manager->autoColorScheme(), &AutoColorScheme::changed, sysBox,
                    refreshStatus);
        }
        refreshStatus();
    }

    bodyLayout->addWidget(sysBox);

    // --- Selection ---------------------------------------------------------
    auto *selBox = new QGroupBox(tr("Color de selección"), body);
    auto *selForm = new QFormLayout(selBox);

    auto *selMode = new QComboBox(selBox);
    selMode->addItem(tr("Grises por omisión"), int(WallpaperColors::Options::DefaultGrays));
    selMode->addItem(tr("Color propio"), int(WallpaperColors::Options::Custom));
    selMode->addItem(tr("Del fondo de pantalla"),
                     int(WallpaperColors::Options::FromWallpaper));
    selMode->setCurrentIndex(qMax(0, selMode->findData(AutoColorScheme::selectionMode())));
    selMode->setToolTip(tr("Por omisión: un esquema claro selecciona con gris oscuro y "
                           "letra blanca, y uno oscuro con gris claro y letra negra. La "
                           "letra no se configura: sale del contraste contra el color de "
                           "selección."));
    selForm->addRow(tr("Origen:"), selMode);

    auto *lightBtn = new QPushButton(selBox);
    const auto refreshLight = makeColorButton(lightBtn, &AutoColorScheme::selectionLight,
                                              &AutoColorScheme::setSelectionLight,
                                              tr("Selección en esquemas claros"));
    auto *darkBtn = new QPushButton(selBox);
    const auto refreshDark = makeColorButton(darkBtn, &AutoColorScheme::selectionDark,
                                             &AutoColorScheme::setSelectionDark,
                                             tr("Selección en esquemas oscuros"));
    selForm->addRow(tr("· Esquemas claros:"), lightBtn);
    selForm->addRow(tr("· Esquemas oscuros:"), darkBtn);

    const auto syncSelection = [this, selMode, lightBtn, darkBtn] {
        const bool custom =
            selMode->currentData().toInt() == int(WallpaperColors::Options::Custom);
        lightBtn->setEnabled(custom);
        darkBtn->setEnabled(custom);
    };
    connect(selMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, selMode, syncSelection](int) {
                AutoColorScheme::setSelectionMode(selMode->currentData().toInt());
                syncSelection();
                if (m_manager && m_manager->autoColorScheme())
                    m_manager->autoColorScheme()->refreshNow();
            });
    // The two swatches write app-wide statics, so a click has to re-run the
    // engine or the change only shows on the next wallpaper.
    for (QPushButton *btn : {lightBtn, darkBtn}) {
        connect(btn, &QPushButton::clicked, this, [this] {
            if (m_manager && m_manager->autoColorScheme())
                m_manager->autoColorScheme()->refreshNow();
        });
    }
    syncSelection();
    bodyLayout->addWidget(selBox);

    // --- Icon sets ---------------------------------------------------------
    auto *iconBox = new QGroupBox(tr("Iconsets"), body);
    auto *iconForm = new QFormLayout(iconBox);
    iconForm->addRow(new QLabel(
        tr("<i>Solo el iconset que usa kdock, sin tocar el del escritorio. Sin tildar, "
           "se usa el guardado abajo.</i>"),
        iconBox));
    addColorAutoIconsetRow(iconForm, iconBox, false, tr("Iconset para esquemas claros"),
                           tr("Se aplica cuando el esquema generado sale claro."));
    addColorAutoIconsetRow(iconForm, iconBox, true, tr("Iconset para esquemas oscuros"),
                           tr("Se aplica cuando el esquema generado sale oscuro."));
    bodyLayout->addWidget(iconBox);

    // --- Saved defaults ----------------------------------------------------
    auto *defBox = new QGroupBox(tr("Guardado para volver atrás"), body);
    auto *defLayout = new QVBoxLayout(defBox);
    defLayout->addWidget(new QLabel(
        tr("<i>Se captura la primera vez que activás ColorAuto y es lo que se restaura al "
           "desactivarlo.</i>"),
        defBox));
    m_colorAutoDefaults = new QLabel(defBox);
    m_colorAutoDefaults->setWordWrap(true);
    defLayout->addWidget(m_colorAutoDefaults);
    auto *recapture = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                      tr("Volver a capturar"), defBox);
    recapture->setToolTip(tr("Toma el esquema de color y el iconset que hay puestos ahora "
                             "como los nuevos valores por omisión."));
    connect(recapture, &QPushButton::clicked, this, [this] {
        if (m_manager && m_manager->autoColorScheme())
            m_manager->autoColorScheme()->captureDefaults();
        reloadColorAutoDefaults();
    });
    defLayout->addWidget(recapture);
    bodyLayout->addWidget(defBox);
    reloadColorAutoDefaults();

    // --- Apply now ---------------------------------------------------------
    auto *applyNow = new QPushButton(QIcon::fromTheme(QStringLiteral("color-management")),
                                     tr("Aplicar ahora"), body);
    applyNow->setToolTip(tr("Vuelve a leer el fondo de cada monitor y regenera el esquema "
                            "sin esperar al próximo cambio."));
    connect(applyNow, &QPushButton::clicked, this, [this] {
        if (m_manager && m_manager->autoColorScheme())
            m_manager->autoColorScheme()->refreshNow();
    });
    bodyLayout->addWidget(applyNow);

    // The master switch does NOT gate the settings below it, and that is the
    // point: every one of them (lightness, selection color, icon sets, which
    // monitor wins, whether the docks are coloured) is read by "Generar Color",
    // the dock widget and the panel card too, and those work with the feature
    // switched off. Greying them out made the whole tab unusable for anyone who
    // only wanted the manual button — reported 2026-08-12. The switch decides
    // *when* the scheme is regenerated, not *how*.
    //
    // State is read back from AutoColorScheme rather than from the checkbox:
    // turning it on captures the defaults, and dark mode can turn it off from
    // underneath us.
    const auto syncEnabled = [this, enabled] {
        const QSignalBlocker block(enabled);
        enabled->setChecked(AutoColorScheme::enabled());
        reloadColorAutoDefaults();
    };
    connect(enabled, &QCheckBox::toggled, this, [this, syncEnabled](bool on) {
        if (m_manager && m_manager->autoColorScheme())
            m_manager->autoColorScheme()->setEnabled(on);
        syncEnabled();
    });
    if (m_manager && m_manager->autoColorScheme()) {
        connect(m_manager->autoColorScheme(), &AutoColorScheme::changed, tab, syncEnabled);
    }
    // Dark mode suspends the feature, so the tab has to follow that too.
    connect(m_config, &DockConfig::darkModeChanged, tab, syncEnabled);
    syncEnabled();

    layout->addStretch();
    return tab;
}

// One font row of the Modo QT tab: a button that opens QFontDialog and shows
// what is stored, plus a reset that hands the font back to LXQt.
//
// The value travels as QFont::toString() from end to end — that is the very
// encoding lxqt.conf uses, so nothing is parsed and re-serialised on the way and
// a font kdock never heard of (a weight, a style name) survives a round trip.
void SettingsDialog::addQtCompatFontRow(QFormLayout *form, QWidget *parent,
                                        int kind, const QString &title)
{
    const auto fontKind = static_cast<QtCompat::FontKind>(kind);
    auto *button = new QPushButton(parent);
    auto *reset = new QPushButton(tr("Seguir la de LXQt"), parent);
    // Kept so the "(sin definir)" state can go back to the dialog's own font
    // instead of to whatever the last preview left on the button.
    const QFont plainFont = button->font();

    const auto refresh = [button, reset, fontKind, plainFont] {
        const QString stored = QtCompat::font(fontKind);
        const QString shown = QtCompat::fontFor(fontKind);
        QFont f;
        if (!shown.isEmpty() && f.fromString(shown)) {
            button->setText(QStringLiteral("%1 %2").arg(f.family()).arg(f.pointSize()));
            // Preview in the button itself, capped so a huge desktop font does
            // not blow the row up.
            QFont sample = f;
            sample.setPointSize(qBound(8, f.pointSize(), 14));
            button->setFont(sample);
        } else {
            button->setText(tr("(sin definir)"));
            button->setFont(plainFont);
        }
        // Nothing stored = kdock is not driving this one; the row shows LXQt's
        // current font greyed out in meaning, and the reset has nothing to do.
        reset->setEnabled(!stored.isEmpty());
    };

    connect(button, &QPushButton::clicked, this, [this, button, fontKind, refresh] {
        QFont start;
        const QString shown = QtCompat::fontFor(fontKind);
        if (!shown.isEmpty())
            start.fromString(shown);
        bool ok = false;
        // The dialog is parented to this dialog, not to the row: a QDialog with
        // no parent is never deleted by anyone (see CLAUDE-TRAMPS.md).
        const QFont picked = QFontDialog::getFont(&ok, start, this, tr("Fuente"));
        if (!ok)
            return;
        if (m_manager && m_manager->qtCompat())
            m_manager->qtCompat()->setFont(fontKind, picked.toString());
        refresh();
    });
    connect(reset, &QPushButton::clicked, this, [this, refresh, fontKind] {
        if (m_manager && m_manager->qtCompat())
            m_manager->qtCompat()->setFont(fontKind, QString());
        refresh();
    });

    auto *row = new QHBoxLayout;
    row->addWidget(button, 1);
    row->addWidget(reset);
    form->addRow(title, row);
    refresh();
}

// Refill the variant combo for whatever layout is selected. Variants are
// per-layout in the rules file ("nodeadkeys" exists for a dozen of them), so
// this runs on every layout change and not once at build time.
void SettingsDialog::rebuildKeyboardVariants()
{
    if (!m_kbVariant)
        return;
    const QString wanted = KeyboardControl::variant();
    const QString layoutId = m_kbLayout ? m_kbLayout->currentData().toString() : QString();

    m_kbFilling = true;
    m_kbVariant->clear();
    // "No variant" is a real choice, not the absence of one, and it is not a
    // row of the rules file — hence the explicit entry with an empty id.
    m_kbVariant->addItem(tr("(sin variante)"), QString());
    for (const auto &v : KeyboardControl::availableVariants(layoutId))
        m_kbVariant->addItem(QStringLiteral("%1 — %2").arg(v.id, v.name), v.id);
    const int at = m_kbVariant->findData(wanted);
    m_kbVariant->setCurrentIndex(at >= 0 ? at : 0);
    m_kbFilling = false;
}

// The status line: what kxkbrc says and what KWin answers. Both, because the
// interesting failure is them disagreeing — and because a line that only showed
// the file would be green on exactly the bug this feature exists for.
void SettingsDialog::reloadKeyboardStatus()
{
    if (!m_kbStatus)
        return;
    KeyboardControl *kb = m_manager ? m_manager->keyboard() : nullptr;

    const QString file = KeyboardControl::configuredLayout();
    const QString fileVariant = KeyboardControl::configuredVariant();
    const QString fileDesc =
        file.isEmpty() ? tr("(sin definir)")
                       : (fileVariant.isEmpty() ? file : file + QLatin1Char('/') + fileVariant);

    QString live;
    if (!kb) {
        live = tr("(sin backend)");
    } else if (!kb->kwinAnswered()) {
        live = tr("(consultando…)");
    } else {
        const auto active = kb->activeLayouts();
        if (active.isEmpty()) {
            live = tr("KWin no contesta (¿no hay KWin en esta sesión?)");
        } else {
            QStringList parts;
            for (const auto &l : active) {
                parts << (l.variant.isEmpty()
                              ? tr("<b>%1</b> (%2)").arg(l.name, l.displayName)
                              : tr("<b>%1/%2</b> (%3)").arg(l.name, l.variant, l.displayName));
            }
            live = parts.join(QStringLiteral(", "));
        }
    }

    QString text = tr("En <tt>kxkbrc</tt>: <b>%1</b><br>KWin está usando: %2")
                       .arg(fileDesc, live);
    const QString sys = KeyboardControl::systemLayout();
    if (!sys.isEmpty()) {
        // The value everybody assumes is in effect, and the one that is not:
        // /etc/default/keyboard is the X11 answer and Wayland never reads it.
        text += tr("<br><i>/etc/default/keyboard dice <b>%1</b> — es la respuesta de X11, "
                   "Wayland no la lee.</i>")
                    .arg(sys);
    }
    m_kbStatus->setText(text);
}

void SettingsDialog::createKeyboardGroup(QVBoxLayout *parentLayout)
{
    QWidget *tab = parentLayout->parentWidget();
    if (!tab)
        tab = qobject_cast<QWidget *>(parentLayout->parent());
    KeyboardControl *kb = m_manager ? m_manager->keyboard() : nullptr;

    auto *box = new QGroupBox(tr("Teclado (KWin/Wayland)"), tab);
    auto *outer = new QVBoxLayout(box);

    auto *intro = new QLabel(
        tr("Bajo Wayland la distribución de teclado la arma el compositor, y acá el "
           "compositor es KWin: la saca del grupo <tt>[Layout]</tt> de <tt>kxkbrc</tt> e "
           "<b>ignora</b> tanto <tt>/etc/default/keyboard</tt> como lo que aplique "
           "lxqt-config-input (que usa <tt>setxkbmap</tt>, o sea X11). Por eso una sesión "
           "configurada entera desde LXQt puede seguir escribiendo con otra distribución y "
           "no haber forma de moverla desde el centro de control.<br><br>"
           "kdock escribe ese archivo y le avisa a KWin por la notificación de KConfig, que "
           "es lo único que le hace recompilar el mapa de teclas <b>en caliente</b> — el "
           "método <tt>reconfigure</tt> de KWin no sirve para esto. Y lo vuelve a aplicar en "
           "cada arranque, así que la elección sobrevive al login."),
        box);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto *on = new QCheckBox(tr("Aplicar la distribución de teclado al arrancar"), box);
    on->setToolTip(tr("Al destildarlo kdock deja de escribir <tt>kxkbrc</tt>, pero NO devuelve "
                      "lo anterior: lo último que escribió queda puesto."));
    on->setChecked(KeyboardControl::enabled());
    connect(on, &QCheckBox::toggled, this, [this](bool checked) {
        KeyboardControl *ctl = m_manager ? m_manager->keyboard() : nullptr;
        if (!ctl)
            return;
        if (checked && KeyboardControl::layout().isEmpty() && m_kbLayout) {
            // The combo can be showing a value nobody ever stored: with nothing
            // in kdock.conf it falls back to kxkbrc and then to
            // /etc/default/keyboard, so that it does not open on the first entry
            // of an alphabetical list. Ticking the box has to apply *that* —
            // otherwise the switch goes on, `layout()` is still empty, and
            // apply() writes nothing at all. Which is the shape of a checkbox
            // that does nothing (found driving the dialog from a probe,
            // 2026-08-22: the combo said latam and the config said "").
            ctl->setLayout(m_kbLayout->currentData().toString());
        }
        ctl->setEnabled(checked);
    });
    outer->addWidget(on);

    auto *form = new QFormLayout;
    outer->addLayout(form);

    m_kbLayout = new QComboBox(box);
    m_kbVariant = new QComboBox(box);
    m_kbModel = new QComboBox(box);
    m_kbOptions = new QLineEdit(box);

    const auto layouts = KeyboardControl::availableLayouts();
    m_kbFilling = true;
    if (layouts.isEmpty()) {
        // No rules file: rather than an empty combo that looks broken, keep
        // whatever is configured so the user can at least see and re-apply it.
        m_kbLayout->addItem(tr("(no se encontró el catálogo de xkb)"), QString());
    }
    for (const auto &l : layouts)
        m_kbLayout->addItem(QStringLiteral("%1 — %2").arg(l.id, l.name), l.id);
    {
        // The starting value, in order of how much it is worth trusting: what
        // kdock already stores, then what KWin is actually using, then what the
        // X11 file says. Opening on the first entry of an alphabetical list
        // would make an accidental OK switch the desktop to Albanian.
        QString initial = KeyboardControl::layout();
        if (initial.isEmpty())
            initial = KeyboardControl::configuredLayout();
        if (initial.isEmpty())
            initial = KeyboardControl::systemLayout();
        const int at = m_kbLayout->findData(initial);
        if (at >= 0)
            m_kbLayout->setCurrentIndex(at);
    }
    m_kbFilling = false;
    form->addRow(tr("Distribución:"), m_kbLayout);

    rebuildKeyboardVariants();
    form->addRow(tr("Variante:"), m_kbVariant);

    m_kbFilling = true;
    m_kbModel->addItem(tr("(el que decida KWin)"), QString());
    for (const auto &m : KeyboardControl::availableModels())
        m_kbModel->addItem(QStringLiteral("%1 — %2").arg(m.id, m.name), m.id);
    {
        const int at = m_kbModel->findData(KeyboardControl::model());
        m_kbModel->setCurrentIndex(at >= 0 ? at : 0);
    }
    m_kbFilling = false;
    m_kbModel->setToolTip(tr("Vacío deja la clave <tt>Model</tt> fuera de kxkbrc, y ahí KWin "
                             "usa su valor de fábrica (<tt>pc104</tt>), que no es "
                             "necesariamente el de <tt>/etc/default/keyboard</tt>."));
    form->addRow(tr("Modelo:"), m_kbModel);

    m_kbOptions->setText(KeyboardControl::options());
    m_kbOptions->setPlaceholderText(tr("p. ej. grp:alt_shift_toggle,terminate:ctrl_alt_bksp"));
    m_kbOptions->setToolTip(tr("Opciones de xkb, separadas por comas. Vacío quita la clave "
                               "<tt>Options</tt> de kxkbrc."));
    form->addRow(tr("Opciones xkb:"), m_kbOptions);

    connect(m_kbLayout, &QComboBox::currentIndexChanged, this, [this] {
        if (m_kbFilling || !m_manager || !m_manager->keyboard())
            return;
        // The variant belongs to the layout, so a layout change invalidates it.
        // Clearing first and rebuilding second means one apply(), not two.
        m_manager->keyboard()->setVariant(QString());
        m_manager->keyboard()->setLayout(m_kbLayout->currentData().toString());
        rebuildKeyboardVariants();
    });
    connect(m_kbVariant, &QComboBox::currentIndexChanged, this, [this] {
        if (m_kbFilling || !m_manager || !m_manager->keyboard())
            return;
        m_manager->keyboard()->setVariant(m_kbVariant->currentData().toString());
    });
    connect(m_kbModel, &QComboBox::currentIndexChanged, this, [this] {
        if (m_kbFilling || !m_manager || !m_manager->keyboard())
            return;
        m_manager->keyboard()->setModel(m_kbModel->currentData().toString());
    });
    // editingFinished and not textChanged: this is a free-text field whose
    // half-typed states are invalid xkb option lists, and every write recompiles
    // the session's keymap.
    connect(m_kbOptions, &QLineEdit::editingFinished, this, [this] {
        if (!m_manager || !m_manager->keyboard())
            return;
        if (m_kbOptions->text() == KeyboardControl::options())
            return;
        m_manager->keyboard()->setOptions(m_kbOptions->text());
    });

    m_kbStatus = new QLabel(box);
    m_kbStatus->setWordWrap(true);
    m_kbStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outer->addWidget(m_kbStatus);

    auto *row = new QHBoxLayout;
    auto *applyBtn = new QPushButton(tr("Aplicar ahora"), box);
    applyBtn->setToolTip(tr("Escribe kxkbrc y le manda la notificación a KWin aunque el "
                            "archivo ya estuviera bien. Es lo que hace falta cuando KWin y "
                            "el archivo se fueron de sincro."));
    connect(applyBtn, &QPushButton::clicked, this, [this] {
        if (!m_manager || !m_manager->keyboard())
            return;
        m_manager->keyboard()->applyNow();
        m_manager->keyboard()->refreshActive();
    });
    auto *refreshBtn =
        new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Refrescar"), box);
    connect(refreshBtn, &QPushButton::clicked, this, [this] {
        if (m_manager && m_manager->keyboard())
            m_manager->keyboard()->refreshActive();
        reloadKeyboardStatus();
    });
    row->addWidget(applyBtn);
    row->addStretch();
    row->addWidget(refreshBtn);
    outer->addLayout(row);

    if (kb) {
        connect(kb, &KeyboardControl::activeChanged, box, [this] { reloadKeyboardStatus(); });
        connect(kb, &KeyboardControl::changed, box, [this] { reloadKeyboardStatus(); });
        kb->refreshActive();
    }
    reloadKeyboardStatus();

    parentLayout->addWidget(box);
}

void SettingsDialog::createKWinScriptsGroup(QVBoxLayout *parentLayout)
{
    QWidget *tab = parentLayout->parentWidget() ? qobject_cast<QWidget *>(parentLayout->parentWidget()) : nullptr;
    // parentLayout lives inside the tab widget; retrieve tab via layout->parent()
    // Fallback: parentLayout's parent is the tab QWidget
    if (!tab) {
        // layout is QVBoxLayout(tab), its parent is QWidget*
        // qobject_cast from QObject* is safe
        tab = qobject_cast<QWidget *>(parentLayout->parent());
    }
    auto *box = new QGroupBox(tr("Scripts de KWin"), tab);
    m_kwinScriptsGroup = box;
    auto *outer = new QVBoxLayout(box);

    // Intro: LXQt uses KWin but does not manage its scripts — kdock does.
    auto *intro = new QLabel(
        tr("KWin es el window manager de esta sesión LXQt. Los scripts instalados "
           "en <tt>~/.local/share/kwin/scripts/</tt> se habilitan por <tt>kwinrc [Plugins] "
           "<i>id</i>Enabled=true</tt> y KWin los carga tras <i>Reconfigurar</i>. "
           "Si un efecto no se activa (ej. <i>Truely Maximized</i>), suele ser esa clave en <i>false</i>, "
           "no interferencia de LXQt."),
        box);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    m_kwinScriptsList = new QListWidget(box);
    m_kwinScriptsList->setMinimumHeight(160);
    m_kwinScriptsList->setSelectionMode(QAbstractItemView::SingleSelection);
    outer->addWidget(m_kwinScriptsList, 1);

    m_kwinScriptsStatus = new QLabel(box);
    m_kwinScriptsStatus->setWordWrap(true);
    m_kwinScriptsStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_kwinScriptsStatus->setVisible(false);
    outer->addWidget(m_kwinScriptsStatus);

    auto *btnRow1 = new QHBoxLayout;
    auto *toggleBtn = new QPushButton(tr("Activar/Desactivar"), box);
    toggleBtn->setToolTip(tr("Alterna kwinrc [Plugins] <id>Enabled y hace Reconfigurar KWin."));
    auto *reconfBtn = new QPushButton(tr("Reconfigurar KWin"), box);
    reconfBtn->setToolTip(tr("qdbus org.kde.KWin /KWin reconfigure"));
    auto *refreshBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Refrescar"), box);
    btnRow1->addWidget(toggleBtn);
    btnRow1->addWidget(reconfBtn);
    btnRow1->addStretch();
    btnRow1->addWidget(refreshBtn);
    outer->addLayout(btnRow1);

    auto *btnRow2 = new QHBoxLayout;
    auto *installBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Instalar desde archivo…"), box);
    installBtn->setToolTip(tr("kpackagetool6 --type KWin/Script --install <archivo.kwinscript>"));
    auto *removeBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), tr("Desinstalar"), box);
    removeBtn->setToolTip(tr("kpackagetool6 --type KWin/Script --remove <id>"));
    auto *openFolderBtn = new QPushButton(tr("Abrir carpeta"), box);
    btnRow2->addWidget(installBtn);
    btnRow2->addWidget(removeBtn);
    btnRow2->addWidget(openFolderBtn);
    btnRow2->addStretch();
    outer->addLayout(btnRow2);

    auto updateButtons = [this, toggleBtn, removeBtn] {
        QListWidgetItem *it = m_kwinScriptsList ? m_kwinScriptsList->currentItem() : nullptr;
        const bool has = it != nullptr;
        toggleBtn->setEnabled(has);
        removeBtn->setEnabled(has);
        if (has) {
            const bool en = it->data(Qt::UserRole + 1).toBool();
            toggleBtn->setText(en ? tr("Desactivar") : tr("Activar"));
        } else {
            toggleBtn->setText(tr("Activar/Desactivar"));
        }
    };

    connect(m_kwinScriptsList, &QListWidget::currentRowChanged, box, [updateButtons](int) { updateButtons(); });
    connect(m_kwinScriptsList, &QListWidget::itemSelectionChanged, box, [updateButtons] { updateButtons(); });

    // Every "re-read from disk" goes through here and never through
    // rebuildKWinScriptsList(): refresh() emits changed(), changed() rebuilds
    // the list, and a rebuild that refreshes again closes the cycle. See the
    // comment on rebuildKWinScriptsList().
    auto requestRefresh = [this] {
        if (m_manager && m_manager->kwinScripts())
            m_manager->kwinScripts()->refresh();
    };

    connect(toggleBtn, &QPushButton::clicked, this, [this, requestRefresh] {
        QListWidgetItem *it = m_kwinScriptsList ? m_kwinScriptsList->currentItem() : nullptr;
        if (!it || !m_manager || !m_manager->kwinScripts())
            return;
        const QString id = it->data(Qt::UserRole).toString();
        const bool en = it->data(Qt::UserRole + 1).toBool();
        QString err = m_manager->kwinScripts()->setEnabled(id, !en);
        if (!err.isEmpty()) {
            m_kwinScriptsStatus->setText(tr("Error: %1").arg(err));
            m_kwinScriptsStatus->setVisible(true);
        } else {
            m_kwinScriptsStatus->setVisible(false);
        }
        // refresh will be triggered by file watcher; force after delay
        QTimer::singleShot(800, this, requestRefresh);
    });

    connect(reconfBtn, &QPushButton::clicked, this, [this, requestRefresh] {
        if (!m_manager || !m_manager->kwinScripts())
            return;
        QString err = m_manager->kwinScripts()->reconfigure();
        if (!err.isEmpty()) {
            m_kwinScriptsStatus->setText(tr("Reconfigurar: %1").arg(err));
            m_kwinScriptsStatus->setVisible(true);
        } else {
            m_kwinScriptsStatus->setText(tr("KWin reconfigurado."));
            m_kwinScriptsStatus->setVisible(true);
            QTimer::singleShot(1500, this, [this] {
                if (m_kwinScriptsStatus && m_kwinScriptsStatus->text() == tr("KWin reconfigurado."))
                    m_kwinScriptsStatus->setVisible(false);
            });
        }
        QTimer::singleShot(700, this, requestRefresh);
    });

    connect(refreshBtn, &QPushButton::clicked, this, requestRefresh);

    connect(installBtn, &QPushButton::clicked, this, [this, requestRefresh] {
        if (!m_manager || !m_manager->kwinScripts())
            return;
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Instalar script de KWin"), QDir::homePath(),
            tr("KWin scripts (*.kwinscript *.tar.gz *.tgz);;Todos (*.*)"));
        if (path.isEmpty())
            return;
        QString err = m_manager->kwinScripts()->install(path);
        if (!err.isEmpty()) {
            QMessageBox::warning(this, tr("Instalar script"), err);
            m_kwinScriptsStatus->setText(tr("Instalar: %1").arg(err));
            m_kwinScriptsStatus->setVisible(true);
        } else {
            m_kwinScriptsStatus->setText(tr("Instalado: %1").arg(QFileInfo(path).fileName()));
            m_kwinScriptsStatus->setVisible(true);
        }
        requestRefresh();
    });

    connect(removeBtn, &QPushButton::clicked, this, [this, requestRefresh] {
        QListWidgetItem *it = m_kwinScriptsList ? m_kwinScriptsList->currentItem() : nullptr;
        if (!it || !m_manager || !m_manager->kwinScripts())
            return;
        const QString id = it->data(Qt::UserRole).toString();
        if (QMessageBox::question(this, tr("Desinstalar script"),
                                   tr("¿Desinstalar \"%1\" (%2)?").arg(it->text(), id),
                                   QMessageBox::Yes | QMessageBox::No)
            != QMessageBox::Yes)
            return;
        QString err = m_manager->kwinScripts()->uninstall(id);
        if (!err.isEmpty()) {
            QMessageBox::warning(this, tr("Desinstalar"), err);
            m_kwinScriptsStatus->setText(tr("Desinstalar: %1").arg(err));
            m_kwinScriptsStatus->setVisible(true);
        } else {
            m_kwinScriptsStatus->setVisible(false);
        }
        requestRefresh();
    });

    connect(openFolderBtn, &QPushButton::clicked, this, [] {
        const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                                .filePath(QStringLiteral("kwin/scripts"));
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

    // Live updates first, then one scan to fill the cache: refresh() emits
    // changed() only when the scan differs, so the explicit rebuild below is
    // what covers the "nothing installed" case.
    if (m_manager && m_manager->kwinScripts()) {
        connect(m_manager->kwinScripts(), &KWinScripts::changed, box, [this] { rebuildKWinScriptsList(); });
        m_manager->kwinScripts()->refresh();
    }
    rebuildKWinScriptsList();

    parentLayout->addWidget(box);
    updateButtons();
}

// Pure view update: paints m_kwinScriptsList from the KWinScripts cache and
// touches nothing else. It must NOT call KWinScripts::refresh() — refresh()
// emits changed(), which is wired back to this function, so a refresh from
// here recursed until the stack overflowed and took the whole dock down with
// it (2026-08-21, first click on "Activar"). The re-entrancy guard is the
// second lock on the same door, in case a future caller re-adds the cycle.
void SettingsDialog::rebuildKWinScriptsList()
{
    if (!m_kwinScriptsList || !m_manager || !m_manager->kwinScripts())
        return;
    if (m_kwinScriptsRebuilding)
        return;
    m_kwinScriptsRebuilding = true;
    const QScopeGuard clearRebuilding([this] { m_kwinScriptsRebuilding = false; });
    KWinScripts *ks = m_manager->kwinScripts();
    if (!ks->available()) {
        m_kwinScriptsList->clear();
        auto *it = new QListWidgetItem(tr("(KWin no responde — org.kde.KWin no está en el bus)"));
        it->setFlags(it->flags() & ~Qt::ItemIsSelectable);
        m_kwinScriptsList->addItem(it);
        if (m_kwinScriptsGroup)
            m_kwinScriptsGroup->setEnabled(false);
        return;
    }
    if (m_kwinScriptsGroup)
        m_kwinScriptsGroup->setEnabled(true);

    const QString prevId = m_kwinScriptsList->currentItem()
                               ? m_kwinScriptsList->currentItem()->data(Qt::UserRole).toString()
                               : QString();
    m_kwinScriptsList->clear();
    const QVariantList list = ks->scripts();
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        const QString id = m.value(QStringLiteral("id")).toString();
        const QString name = m.value(QStringLiteral("name")).toString();
        const QString version = m.value(QStringLiteral("version")).toString();
        const QString api = m.value(QStringLiteral("api")).toString();
        const bool en = m.value(QStringLiteral("enabled")).toBool();
        const bool loaded = m.value(QStringLiteral("loaded")).toBool();
        QString label = QStringLiteral("%1  (%2)").arg(name, id);
        if (!version.isEmpty())
            label += QStringLiteral("  v%1").arg(version);
        if (!api.isEmpty())
            label += QStringLiteral("  [%1]").arg(api);
        label += en ? tr(" — Activado") : tr(" — Desactivado");
        label += loaded ? tr(" · Cargado") : tr(" · No cargado");

        auto *it = new QListWidgetItem(label);
        it->setData(Qt::UserRole, id);
        it->setData(Qt::UserRole + 1, en);
        it->setData(Qt::UserRole + 2, loaded);
        QString tip = m.value(QStringLiteral("description")).toString();
        if (!tip.isEmpty())
            it->setToolTip(tip + QStringLiteral("\n") + m.value(QStringLiteral("path")).toString());
        else
            it->setToolTip(m.value(QStringLiteral("path")).toString());
        // Icon: checkmark for enabled
        if (en)
            it->setIcon(QIcon::fromTheme(QStringLiteral("emblem-checked")));
        else
            it->setIcon(QIcon::fromTheme(QStringLiteral("emblem-unavailable")));
        m_kwinScriptsList->addItem(it);
        if (!prevId.isEmpty() && id == prevId)
            m_kwinScriptsList->setCurrentItem(it);
    }
    if (list.isEmpty()) {
        auto *it = new QListWidgetItem(tr("(No hay scripts instalados en ~/.local/share/kwin/scripts)"));
        it->setFlags(it->flags() & ~Qt::ItemIsSelectable);
        m_kwinScriptsList->addItem(it);
    }
}

// One row of the translation table: the lxqt.conf key, a swatch and the hex.
// The swatch is drawn here and not through themePreviewPixmap(): that one draws
// the three-color preview of a whole scheme, this is one color per row.
void SettingsDialog::reloadQtCompatTranslation()
{
    if (!m_qtCompatForm)
        return;
    QtCompat *compat = m_manager ? m_manager->qtCompat() : nullptr;
    if (!compat)
        return;

    while (m_qtCompatForm->rowCount() > 0)
        m_qtCompatForm->removeRow(0);

    if (m_qtCompatUiSettings) {
        const QString general = compat->kdeColorScheme();
        const QString ui = compat->kdeUiSettingsScheme();
        if (general.isEmpty()) {
            m_qtCompatUiSettings->setText(tr("KDE no tiene esquema definido por nombre."));
        } else if (general == ui) {
            m_qtCompatUiSettings->setText(tr("<b>%1</b> — al día").arg(ui));
        } else {
            m_qtCompatUiSettings->setText(
                tr("<b>%1</b> → hay que copiarlo a <tt>[UiSettings]</tt> (ahora: %2)")
                    .arg(general, ui.isEmpty() ? tr("sin definir") : ui));
        }
    }

    if (m_qtCompatIcons) {
        const QString icons = compat->iconTheme();
        m_qtCompatIcons->setText(icons.isEmpty()
                                     ? tr("(KDE no tiene iconset definido — no se escribe nada)")
                                     : icons);
    }

    QWidget *parent = m_qtCompatForm->parentWidget();
    const QVariantList rows = compat->translation();
    if (rows.isEmpty()) {
        m_qtCompatForm->addRow(new QLabel(
            tr("No se pudo leer kdeglobals: no hay nada que traducir."), parent));
        return;
    }
    const int side = 16;
    for (const QVariant &entry : rows) {
        const QVariantMap map = entry.toMap();
        const QString key = map.value(QStringLiteral("key")).toString();
        const QString hex = map.value(QStringLiteral("color")).toString();

        // Outlined, not a plain fill: half of these colors are near-black or
        // near-white by design, and a borderless swatch of one of them is
        // invisible against the dialog — which reads as a missing row.
        QPixmap swatch(side, side);
        swatch.fill(Qt::transparent);
        {
            QPainter p(&swatch);
            p.fillRect(1, 1, side - 2, side - 2, QColor(hex));
            QColor edge = parent ? parent->palette().color(QPalette::WindowText)
                                 : QColor(Qt::gray);
            edge.setAlpha(120);
            p.setPen(edge);
            p.drawRect(0, 0, side - 1, side - 1);
        }
        auto *value = new QLabel(parent);
        auto *icon = new QLabel(parent);
        icon->setPixmap(swatch);
        value->setText(hex);
        auto *row = new QWidget(parent);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(icon);
        rowLayout->addWidget(value);
        rowLayout->addStretch();
        m_qtCompatForm->addRow(key, row);
    }
}

QWidget *SettingsDialog::createQtCompatTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);
    QtCompat *compat = m_manager ? m_manager->qtCompat() : nullptr;

    auto *intro = new QLabel(
        tr("Bajo LXQt las apps Qt no leen la apariencia de KDE: la paleta sale del grupo "
           "<b>[Palette]</b> de <tt>lxqt.conf</tt>, el iconset de <tt>icon_theme</tt> y la "
           "fuente de <tt>[Qt] font</tt>. Esto traduce a esas claves lo que ya elegís en "
           "kdock y las escribe ahí, así que <b>las apps que ya están abiertas se re-tematizan "
           "solas</b> — el tema de plataforma de LXQt vigila ese archivo.<br><br>"
           "Ni el esquema ni el iconset se guardan acá: son siempre los que están puestos en "
           "KDE, así que cambiarlos desde cualquier otro selector de kdock (el widget del "
           "dock, la solapa Colores, ColorAuto, el modo oscuro) también llega a LXQt. La "
           "fuente sí es un ajuste propio: kdock no tenía ninguno que copiar."), tab);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *enabled = new QCheckBox(tr("Aplicar la apariencia a las apps Qt/LXQt"), tab);
    enabled->setToolTip(tr("Interruptor maestro de las tres partes de abajo. Al destildarlo "
                           "kdock deja de aplicar, pero NO devuelve lo anterior: lo último "
                           "que escribió queda puesto, para manejarlo con "
                           "lxqt-config-appearance."));
    enabled->setChecked(QtCompat::enabled());
    connect(enabled, &QCheckBox::toggled, this, [this](bool on) {
        if (m_manager && m_manager->qtCompat())
            m_manager->qtCompat()->setEnabled(on);
    });
    layout->addWidget(enabled);

    // --- The scheme, which is KDE's own ------------------------------------
    // ApplyToDesktop on purpose: this picker is a mirror, not a second setting.
    // It writes kdeglobals like every other color-scheme selector of kdock, and
    // QtCompat follows through Theme::changed.
    auto *schemeBox = new QGroupBox(tr("Esquema de color"), tab);
    auto *schemeForm = new QFormLayout(schemeBox);
    auto *colorsOn = new QCheckBox(tr("Aplicar el esquema de color"), schemeBox);
    colorsOn->setChecked(QtCompat::colorsEnabled());
    connect(colorsOn, &QCheckBox::toggled, this, [this](bool on) {
        if (m_manager && m_manager->qtCompat())
            m_manager->qtCompat()->setColorsEnabled(on);
    });
    schemeForm->addRow(colorsOn);
    if (m_appearance) {
        auto *pick = new ThemePickerButton(m_appearance, QStringLiteral("colors"),
                                           ThemePickerPopup::ApplyToDesktop, schemeBox);
        pick->setToolTip(tr("La misma lista que el selector de KDE. Elegir acá cambia el "
                            "esquema del escritorio y, con la casilla de arriba tildada, "
                            "lo traduce a la paleta de LXQt."));
        schemeForm->addRow(tr("Esquema:"), pick);
    } else {
        schemeForm->addRow(new QLabel(tr("(sin AppearanceControl)"), schemeBox));
    }
    // La clave que hace que las apps de KDE hagan caso. Se muestra porque es
    // invisible de otro modo y porque es exactamente lo que estaba roto: sin
    // ella, Dolphin y compañía se pintan de Breeze Light y parece que la solapa
    // entera no hace nada.
    m_qtCompatUiSettings = new QLabel(schemeBox);
    m_qtCompatUiSettings->setWordWrap(true);
    m_qtCompatUiSettings->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_qtCompatUiSettings->setToolTip(
        tr("plasma-apply-colorscheme escribe <tt>[General] ColorScheme</tt>, pero las apps "
           "de KDE leen <tt>[UiSettings] ColorScheme</tt> (KColorSchemeManager). Si falta, "
           "cada app de KDE pisa la paleta con su Breeze Light de fábrica. kdock copia la "
           "primera sobre la segunda."));
    schemeForm->addRow(tr("· Apps KDE:"), m_qtCompatUiSettings);
    layout->addWidget(schemeBox);

    // --- Icon set, same mirror idiom ---------------------------------------
    auto *iconsBox = new QGroupBox(tr("Iconset"), tab);
    auto *iconsForm = new QFormLayout(iconsBox);
    auto *iconsOn = new QCheckBox(tr("Aplicar el iconset"), iconsBox);
    iconsOn->setToolTip(tr("Copia <tt>Icons/Theme</tt> de kdeglobals a <tt>icon_theme</tt> de "
                           "lxqt.conf. El plugin de LXQt recarga el tema de íconos de todas "
                           "las apps abiertas."));
    iconsOn->setChecked(QtCompat::iconsEnabled());
    connect(iconsOn, &QCheckBox::toggled, this, [this](bool on) {
        if (m_manager && m_manager->qtCompat())
            m_manager->qtCompat()->setIconsEnabled(on);
    });
    iconsForm->addRow(iconsOn);
    if (m_appearance) {
        auto *pickIcons = new ThemePickerButton(m_appearance, QStringLiteral("icons"),
                                                ThemePickerPopup::ApplyToDesktop, iconsBox);
        pickIcons->setToolTip(tr("La misma lista que el selector de KDE. Ojo: el dock puede "
                                 "tener su propio override de iconset (solapa General), que "
                                 "le gana a este para sus propios íconos."));
        iconsForm->addRow(tr("Iconset:"), pickIcons);
    }
    // What would actually land in the file, which is not always what the picker
    // shows: the dock's own override does not travel, and KDE may have none.
    m_qtCompatIcons = new QLabel(iconsBox);
    m_qtCompatIcons->setTextInteractionFlags(Qt::TextSelectableByMouse);
    iconsForm->addRow(tr("· icon_theme:"), m_qtCompatIcons);
    layout->addWidget(iconsBox);

    // --- Fonts: the one part with a value of its own ------------------------
    auto *fontsBox = new QGroupBox(tr("Fuentes de las aplicaciones"), tab);
    auto *fontsForm = new QFormLayout(fontsBox);
    auto *fontsOn = new QCheckBox(tr("Aplicar las fuentes"), fontsBox);
    fontsOn->setToolTip(tr("Escribe <tt>[Qt] font</tt> y <tt>fixedFont</tt> de lxqt.conf. "
                           "Vale para todo el escritorio LXQt <b>y para kdock</b>: su texto "
                           "usa la fuente de la aplicación, que sale del mismo lugar."));
    fontsOn->setChecked(QtCompat::fontsEnabled());
    connect(fontsOn, &QCheckBox::toggled, this, [this](bool on) {
        if (m_manager && m_manager->qtCompat())
            m_manager->qtCompat()->setFontsEnabled(on);
    });
    fontsForm->addRow(fontsOn);
    addQtCompatFontRow(fontsForm, fontsBox, QtCompat::GeneralFont, tr("General:"));
    addQtCompatFontRow(fontsForm, fontsBox, QtCompat::FixedFont, tr("Monoespaciada:"));
    layout->addWidget(fontsBox);

    // --- What would be written ---------------------------------------------
    auto *transBox = new QGroupBox(tr("Traducción a la paleta de LXQt"), tab);
    auto *transOuter = new QVBoxLayout(transBox);
    auto *transHost = new QWidget(transBox);
    m_qtCompatForm = new QFormLayout(transHost);
    transOuter->addWidget(transHost);
    layout->addWidget(transBox);
    reloadQtCompatTranslation();

    auto *applyNow = new QPushButton(tr("Aplicar ahora"), tab);
    applyNow->setToolTip(tr("Reescribe las claves aunque nada haya cambiado en KDE. "
                            "Sirve cuando lxqt.conf se fue de sincro por "
                            "afuera (lxqt-config-appearance, por ejemplo)."));
    connect(applyNow, &QPushButton::clicked, this, [this] {
        if (m_manager && m_manager->qtCompat())
            m_manager->qtCompat()->applyNow();
    });
    layout->addWidget(applyNow);

    // --- Keyboard and KWin scripts: both here because KWin is the WM under
    // LXQt, and both are things the LXQt control centre cannot reach. ---
    createKeyboardGroup(layout);
    createKWinScriptsGroup(layout);

    // --- Diagnostics --------------------------------------------------------
    // Which file is being written and whether anything is going to notice. The
    // write happens either way (it is the session's file, not kdock's), but
    // under another platform theme nothing repaints, and without this line that
    // reads exactly like a broken feature.
    auto *diag = new QLabel(tab);
    diag->setWordWrap(true);
    diag->setTextInteractionFlags(Qt::TextSelectableByMouse);
    const QString platform = qEnvironmentVariable("QT_QPA_PLATFORMTHEME");
    QString text = tr("Sesión detectada: <b>%1</b>%2<br>Archivo: <tt>%3</tt><br>"
                      "QT_QPA_PLATFORMTHEME: <b>%4</b>")
                       .arg(Session::isLxqt()   ? tr("LXQt")
                            : Session::isKde()  ? tr("KDE / Plasma")
                                                : tr("otra"),
                            // The one that surprises: this session runs KWin
                            // *under* LXQt, and half of kdock's features depend
                            // on KWin rather than on the desktop.
                            Session::hasKWin() ? tr(" (con KWin)") : QString(),
                            QtCompat::lxqtConfPath(),
                            platform.isEmpty() ? tr("(sin definir)") : platform);
    if (Session::isLxqt()) {
        text += tr("<br><i>Al detectarse LXQt, este modo se enciende solo la primera vez. "
                   "Apagarlo acá es definitivo: no se vuelve a encender.</i>");
    }
    if (!QtCompat::lxqtPlatformTheme()) {
        text += tr("<br><i>Esta sesión no usa el tema de plataforma «lxqt»: las claves se "
                   "escriben igual, pero no va a repintarse nada.</i>");
    }
    diag->setText(text);
    layout->addWidget(diag);

    // The table follows the KDE scheme, whoever changed it.
    if (m_theme)
        connect(m_theme, &Theme::changed, tab, [this] { reloadQtCompatTranslation(); });
    if (compat)
        connect(compat, &QtCompat::changed, tab, [this] { reloadQtCompatTranslation(); });

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

    // Two rows instead of one: everything in a single row overflowed the
    // dialog's width. Row 1 is the separator family; row 2 acts on a widget
    // (remove / add a selectable-apps one / rename), with Up/Down pushed to the
    // right.
    auto *addButtons = new QHBoxLayout;
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
                          "dock length — or give it a fixed width below, which needs no room "
                          "to distribute and works in any mode."));
    addButtons->addWidget(addSep);
    addButtons->addWidget(addSpring);
    addButtons->addWidget(addGap);
    layout->addLayout(addButtons);

    auto *actionButtons = new QHBoxLayout;
    auto *remove = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")),
                                   tr("Remove separator"), tab);
    // Not with the "Add separator" family above: this one adds a *widget*, and
    // it sits next to the two buttons that also act on one (Remove, Rename).
    auto *addApps = new QPushButton(QIcon::fromTheme(QStringLiteral("applications-all")),
                                    tr("Add selectable apps"), tab);
    addApps->setToolTip(tr("A block of app icons like the one \"Show applications\" draws, "
                           "but with its own list of apps: right-click an icon → \"Pin\" "
                           "adds it to this widget only. A dock can hold several, each with "
                           "its own apps (see the Widgets tab)."));
    auto *rename = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-rename")),
                                   tr("Rename..."), tab);
    rename->setToolTip(tr("Name shown for this section in the dock (see the "
                          "\"Widget name\" setting in Appearance). Leave the field "
                          "empty to restore the default name."));
    auto *up = new QPushButton(QIcon::fromTheme(QStringLiteral("go-up")), tr("Up"), tab);
    auto *down = new QPushButton(QIcon::fromTheme(QStringLiteral("go-down")), tr("Down"), tab);
    actionButtons->addWidget(remove);
    actionButtons->addWidget(addApps);
    actionButtons->addWidget(rename);
    actionButtons->addStretch();
    actionButtons->addWidget(up);
    actionButtons->addWidget(down);
    layout->addLayout(actionButtons);

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
        const QString token = it ? it->data(Qt::UserRole).toString() : QString();
        const bool appsWidget = DockConfig::isAppsWidgetToken(token);
        const bool sep = it && DockConfig::isRepeatableToken(token);
        // A selectable-apps widget is on both sides of this: it is removed like
        // a separator (nothing else brings it back) and renamed like a widget
        // (its name is drawn, and with several of them the default numbers are
        // all the user has to tell them apart).
        remove->setEnabled(sep);
        rename->setEnabled(it && (!sep || appsWidget));
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
            // Sin " = " adentro: el catálogo parte por el primero y esta cadena
            // pasó meses con su traducción al español cortada al medio
            // (tests/static/check-tr-separator.py).
            tr("Name for \"%1\" (empty: default):").arg(sectionLabel(token)),
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
    connect(addApps, &QPushButton::clicked, this, [this, orderIndexOfRow] {
        const int row = m_layoutList->currentRow();
        const int oi = orderIndexOfRow(row);
        const int at = oi >= 0 ? oi + 1 : m_config->widgetOrder().size();
        m_config->insertAppsWidget(at);
        m_layoutList->setCurrentRow(row + 1);
        // The new widget has no apps yet and its own panel is in the Widgets
        // tab, so that tab has to grow a group for it right now.
        rebuildAppsWidgetsGroup();
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
        if (oi < 0)
            return;
        const bool wasAppsWidget =
            DockConfig::isAppsWidgetToken(m_config->widgetOrder().value(oi));
        m_config->removeSectionAt(oi);
        if (wasAppsWidget)
            rebuildAppsWidgetsGroup();
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

    // Bound to `tab`, so the connections die when buildTabs() deletes it. The
    // config outlives every tab (it is the live dock's), and updateRemove holds
    // raw pointers to this tab's buttons: on `this` the connection would survive
    // the rebuild that switching docks does and call setEnabled() on freed
    // widgets. That is a hard crash, and it was one (SIGSEGV, 2026-08-15).
    connect(m_config, &DockConfig::widgetOrderChanged, tab, [this, updateRemove] {
        reloadLayoutList();
        updateRemove();
        // A separator may have come or gone with the new order, and its width
        // row lives right below this list.
        rebuildGapsGroup();
    });
    connect(m_config, &DockConfig::widgetNamesChanged, tab, [this] { reloadLayoutList(); });
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

    // One row per transparent separator: its width is per instance, so there is
    // nothing to put in the form above. Rebuilt in place, because the list right
    // above is where they are added and removed.
    m_gapsBox = new QGroupBox(tr("Transparent separators"), tab);
    m_gapsLayout = new QVBoxLayout(m_gapsBox);
    layout->addWidget(m_gapsBox);
    rebuildGapsGroup();

    for (auto signal : {&DockConfig::separator1Changed, &DockConfig::separator2Changed,
                        &DockConfig::separator1TransparentChanged,
                        &DockConfig::separator2TransparentChanged,
                        &DockConfig::pinnedChanged}) {
        // Bound to `tab` for the same reason as above: updateAppSepButtons holds
        // raw pointers to this tab's buttons, and pinnedChanged is emitted by
        // every pin/unpin from the dock itself, i.e. long after a dock switch
        // rebuilt the tabs. This is the exact pair that crashed.
        connect(m_config, signal, tab, [this, updateAppSepButtons] {
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
    for (int i = 0; i < order.size(); ++i) {
        const QString token = order.at(i);
        if (kHiddenFromLayout.contains(token))
            continue;
        const bool spring = token == QLatin1String("spring");
        // A transparent separator carries its own number in its token (it keys
        // the width setting), so it is named like an appsel widget rather than
        // counted positionally — the two would disagree the moment one is
        // removed from the middle, and the width panel below reads the token.
        const bool gap = DockConfig::isGapToken(token);
        // A selectable-apps widget repeats like a separator (that is what lets
        // a dock hold several), but it reads as a widget: its own name, its own
        // icon, and no number appended — its label already carries one.
        const bool appsWidget = DockConfig::isAppsWidgetToken(token);
        const bool separator =
            DockConfig::isRepeatableToken(token) && !appsWidget && !gap;
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
            QIcon::fromTheme(appsWidget  ? QStringLiteral("applications-all")
                             : gap       ? QStringLiteral("edit-clear-all")
                             : !separator ? QStringLiteral("view-list-symbolic")
                             : spring    ? QStringLiteral("distribute-horizontal-margin")
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

    // Bound to `tab`, so the connection dies when buildTabs() deletes it: the
    // manager is shared by every dock and outlives all of them.
    connect(m_relanzadores, &RelanzadoresManager::itemsChanged, tab,
            [this] { reloadRelanzadoresList(); });

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
    m_writingPinned = true;
    m_config->setPinned(pinned);
    m_writingPinned = false;
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
    m_writingFavorites = true;
    m_config->setMenuFavorites(favs);
    m_writingFavorites = false;
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

    // Bound to `tab`, same as the Relanzadores tab above.
    connect(m_scriptRunners, &ScriptRunnersManager::itemsChanged, tab,
            [this] { reloadScriptRunnersList(); });

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

QWidget *SettingsDialog::createPresetsTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    auto *info = new QLabel(
        tr("A preset is a complete kdock configuration: every dock, the "
           "relanzadores and script runners, and the settings of the accessory "
           "windows (previews, tile menu, control panel, weather).\n\n"
           "\"Current\" is the configuration in use. Save it under a name to "
           "turn it into a preset; choosing another one and pressing Apply "
           "replaces the current configuration with it and restarts kdock."),
        tab);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *presetRow = new QHBoxLayout;
    presetRow->addWidget(new QLabel(tr("Preset:"), tab));
    m_presetCombo = new QComboBox(tab);
    m_presetCombo->setMinimumWidth(220);
    presetRow->addWidget(m_presetCombo, 1);
    auto *saveBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")),
                                    tr("Save…"), tab);
    saveBtn->setToolTip(tr("Save the configuration in use as a new preset."));
    presetRow->addWidget(saveBtn);
    layout->addLayout(presetRow);

    auto *manageRow = new QHBoxLayout;
    m_presetOverwriteBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save-as")),
                                           tr("Overwrite"), tab);
    m_presetOverwriteBtn->setToolTip(
        tr("Replace the selected preset with the configuration in use."));
    m_presetRenameBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-rename")),
                                        tr("Rename…"), tab);
    m_presetDeleteBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")),
                                        tr("Delete"), tab);
    manageRow->addWidget(m_presetOverwriteBtn);
    manageRow->addWidget(m_presetRenameBtn);
    manageRow->addWidget(m_presetDeleteBtn);
    manageRow->addStretch();
    layout->addLayout(manageRow);

    auto *applyRow = new QHBoxLayout;
    m_presetApplyBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")),
                                       tr("Apply"), tab);
    m_presetApplyBtn->setToolTip(
        tr("Replace the current configuration with the selected preset and restart kdock."));
    m_presetNoPrompt = new QCheckBox(tr("Apply without warning"), tab);
    m_presetNoPrompt->setChecked(DockConfig::presetApplyNoPrompt());
    m_presetNoPrompt->setToolTip(
        tr("Skip the confirmation window: Apply replaces the configuration and "
           "restarts kdock right away."));
    applyRow->addWidget(m_presetApplyBtn);
    applyRow->addWidget(m_presetNoPrompt);
    applyRow->addStretch();
    layout->addLayout(applyRow);

    connect(m_presetCombo, &QComboBox::currentIndexChanged, this,
            [this]() { updatePresetButtons(); });
    connect(m_presetNoPrompt, &QCheckBox::toggled, this,
            [](bool on) { DockConfig::setPresetApplyNoPrompt(on); });
    connect(saveBtn, &QPushButton::clicked, this, [this]() { savePresetInteractive(); });
    connect(m_presetApplyBtn, &QPushButton::clicked, this,
            [this]() { applySelectedPreset(); });

    connect(m_presetOverwriteBtn, &QPushButton::clicked, this, [this]() {
        const QString name = m_presetCombo->currentData().toString();
        if (name.isEmpty())
            return;
        if (QMessageBox::question(
                this, tr("Overwrite preset"),
                tr("Replace the preset \"%1\" with the configuration in use?").arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QString err;
        if (!ConfigArchive::savePreset(name, &err))
            QMessageBox::warning(this, tr("Save failed"), err);
        else
            reloadPresetList(name);
    });

    connect(m_presetRenameBtn, &QPushButton::clicked, this, [this]() {
        const QString name = m_presetCombo->currentData().toString();
        if (name.isEmpty())
            return;
        bool ok = false;
        const QString to = QInputDialog::getText(this, tr("Rename preset"), tr("New name:"),
                                                 QLineEdit::Normal, name, &ok);
        if (!ok || ConfigArchive::sanitizePresetName(to).isEmpty())
            return;
        QString err;
        if (!ConfigArchive::renamePreset(name, to, &err))
            QMessageBox::warning(this, tr("Rename failed"), err);
        else
            reloadPresetList(ConfigArchive::sanitizePresetName(to));
    });

    connect(m_presetDeleteBtn, &QPushButton::clicked, this, [this]() {
        const QString name = m_presetCombo->currentData().toString();
        if (name.isEmpty())
            return;
        if (QMessageBox::question(this, tr("Delete preset"),
                                  tr("Delete the preset \"%1\"?").arg(name),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return;
        QString err;
        if (!ConfigArchive::deletePreset(name, &err))
            QMessageBox::warning(this, tr("Delete failed"), err);
        reloadPresetList();
    });

    reloadPresetList();

    // Favorites-only export/import (a small JSON list of .desktop ids), separate
    // from the full config presets above.
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

    // Foot of the tab: the same whole-configuration .zip a preset is, but to and
    // from anywhere on disk.
    auto *footLine = new QFrame(tab);
    footLine->setFrameShape(QFrame::HLine);
    footLine->setFrameShadow(QFrame::Sunken);
    layout->addWidget(footLine);

    auto *footRow = new QHBoxLayout;
    auto *exportBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-export")),
                                      tr("Export…"), tab);
    exportBtn->setToolTip(tr("Write the configuration in use to a .zip file."));
    auto *importBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-import")),
                                      tr("Import…"), tab);
    importBtn->setToolTip(
        tr("Read a .zip written by Export and add it to the preset list. Nothing "
           "changes until you press Apply."));
    footRow->addStretch();
    footRow->addWidget(exportBtn);
    footRow->addWidget(importBtn);
    layout->addLayout(footRow);

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

    // Importing lands the archive in the preset list instead of replacing the
    // configuration on the spot: nothing the user has now is touched until they
    // pick it and press Apply.
    connect(importBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import configuration"), dir, tr("Zip archives (*.zip)"));
        if (path.isEmpty())
            return;
        if (!ConfigArchive::isConfigArchive(path)) {
            QMessageBox::warning(this, tr("Import failed"),
                                 tr("Not a valid kdock configuration archive:\n%1").arg(path));
            return;
        }
        bool ok = false;
        const QString suggested = QFileInfo(path).completeBaseName();
        const QString name = QInputDialog::getText(
            this, tr("Import configuration"), tr("Save it as the preset:"),
            QLineEdit::Normal, suggested, &ok);
        if (!ok || ConfigArchive::sanitizePresetName(name).isEmpty())
            return;
        const QString clean = ConfigArchive::sanitizePresetName(name);
        if (ConfigArchive::presetNames().contains(clean)
            && QMessageBox::question(
                   this, tr("Import configuration"),
                   tr("The preset \"%1\" already exists. Replace it?").arg(clean),
                   QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QString err;
        if (!ConfigArchive::importPreset(path, clean, &err)) {
            QMessageBox::warning(this, tr("Import failed"), err);
            return;
        }
        reloadPresetList(clean);
        QMessageBox::information(
            this, tr("Import configuration"),
            tr("Imported as the preset \"%1\". Press Apply to use it.").arg(clean));
    });

    return tab;
}

void SettingsDialog::reloadPresetList(const QString &select)
{
    if (!m_presetCombo)
        return;
    const QString keep = select.isNull() ? m_presetCombo->currentData().toString() : select;
    QSignalBlocker blocker(m_presetCombo);
    m_presetCombo->clear();
    // First entry is the configuration in use: not a file, so it is the one
    // state Apply has nothing to do with.
    m_presetCombo->addItem(tr("Current"), QString());
    for (const QString &name : ConfigArchive::presetNames())
        m_presetCombo->addItem(name, name);
    const int at = keep.isEmpty() ? 0 : m_presetCombo->findData(keep);
    m_presetCombo->setCurrentIndex(at >= 0 ? at : 0);
    blocker.unblock();
    updatePresetButtons();
}

void SettingsDialog::updatePresetButtons()
{
    if (!m_presetCombo)
        return;
    const bool isPreset = !m_presetCombo->currentData().toString().isEmpty();
    for (QPushButton *b : {m_presetOverwriteBtn, m_presetRenameBtn, m_presetDeleteBtn,
                           m_presetApplyBtn}) {
        if (b)
            b->setEnabled(isPreset);
    }
}

QString SettingsDialog::savePresetInteractive(const QString &suggested)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Preset name:"),
                                               QLineEdit::Normal, suggested, &ok);
    if (!ok)
        return {};
    const QString clean = ConfigArchive::sanitizePresetName(name);
    if (clean.isEmpty()) {
        QMessageBox::warning(this, tr("Save preset"), tr("The name cannot be empty."));
        return {};
    }
    if (ConfigArchive::presetNames().contains(clean)
        && QMessageBox::question(this, tr("Save preset"),
                                 tr("The preset \"%1\" already exists. Replace it?").arg(clean),
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) != QMessageBox::Yes)
        return {};

    // exportTo() zips what is on disk, and QSettings batches: without this the
    // preset misses whatever the user changed in this very dialog.
    DockConfig::syncAll();
    QString err;
    if (!ConfigArchive::savePreset(clean, &err)) {
        QMessageBox::warning(this, tr("Save failed"), err);
        return {};
    }
    reloadPresetList(clean);
    return clean;
}

void SettingsDialog::applySelectedPreset()
{
    const QString name = m_presetCombo ? m_presetCombo->currentData().toString() : QString();
    if (name.isEmpty())
        return;
    const QString path = ConfigArchive::presetPath(name);
    if (!QFile::exists(path)) {
        QMessageBox::warning(this, tr("Apply preset"),
                             tr("The preset \"%1\" is gone from disk.").arg(name));
        reloadPresetList();
        return;
    }

    if (!m_presetNoPrompt || !m_presetNoPrompt->isChecked()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Apply preset"));
        box.setText(tr("Applying \"%1\" replaces the configuration in use and "
                       "restarts kdock and its accessory windows.\n\n"
                       "The configuration being replaced is copied to a "
                       "backup-<date> folder, but the quickest way back is to "
                       "save it as a preset first.")
                        .arg(name));
        QPushButton *apply = box.addButton(tr("Apply"), QMessageBox::AcceptRole);
        QPushButton *saveFirst = box.addButton(tr("Save first…"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(apply);
        box.exec();
        if (box.clickedButton() == saveFirst) {
            // Any QMessageBox button closes it, so the name prompt runs after
            // the fact: a cancelled save means the user is not ready to apply.
            if (savePresetInteractive().isEmpty())
                return;
        } else if (box.clickedButton() != apply) {
            return;
        }
    }

    // Flush before handing over: this process dies while the next one is
    // starting, and a QSettings that still has dirty keys writes them from its
    // destructor — on top of the config the preset just installed.
    DockConfig::syncAll();
    // The next process applies it, before anything has read the config — see
    // kdock::restartAll() and the --apply-preset block in main().
    kdock::restartAll({QLatin1String(kdock::kApplyPresetFlag), path});
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
    clearSearch();
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
    LxqtWallpapers *lxqtWallpapers = m_manager ? m_manager->lxqtWallpapers() : nullptr;

    // The two engines are configured from this one tab, and the difference is
    // worth explaining because it decides what the tab can offer: under Plasma
    // kdock rewrites somebody else's wallpaper (so desktop 1 is off limits),
    // under LXQt it paints them itself (so every desktop is fair game).
    const bool lxqt = Session::isLxqt();
    auto *info = new QLabel(
        lxqt ? tr("Ni LXQt ni PCManFM-Qt tienen fondos distintos por monitor —y menos por "
                  "escritorio virtual—, así que acá los dibuja kdock, con una superficie "
                  "propia por monitor. Cada escritorio tiene su juego de imágenes; un monitor "
                  "sin nada configurado no se toca.<br><br>"
                  "<b>Mientras esto esté encendido, el escritorio de PCManFM queda apagado</b> "
                  "(sin íconos ni menú del escritorio): las dos capas no pueden convivir. "
                  "Vuelve solo al apagar esta casilla y al cerrar kdock.")
             : tr("Plasma no tiene un fondo por escritorio virtual: el fondo es de la pantalla, "
                  "no del escritorio. kdock lo consigue reescribiéndolo en el momento del cambio. "
                  "El Escritorio 1 es de KDE y no se toca desde acá: su configuración se guarda "
                  "sola y vuelve cada vez que regresás a él (y cuando kdock se cierra)."),
        tab);
    info->setWordWrap(true);
    info->setTextFormat(Qt::RichText);
    layout->addWidget(info);

    auto *enable = new QCheckBox(
        lxqt ? tr("Dibujar los fondos de pantalla desde kdock")
             : tr("Cambiar el fondo al cambiar de escritorio virtual"), tab);
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
    //
    // Plasma only. Under LXQt nothing of this exists: there is no containment
    // to snapshot and nothing of anybody's to restore, so the group would be an
    // empty list and two buttons that cannot do anything.
    auto *kdeGroup = new QGroupBox(tr("Escritorio 1 — configuración de KDE (se conserva siempre)"), tab);
    kdeGroup->setVisible(!lxqt);
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

    // Under LXQt desktop 1 is a desktop like any other — kdock owns the picture
    // there too, so leaving it out would mean "the first desktop is the only one
    // you cannot configure" for no reason the user can see.
    for (int desktop = lxqt ? 1 : 2; desktop <= DockConfig::kMaxDesktops; ++desktop) {
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
    // Under LXQt every desktop is ours, so there is no "only from desktop 2 on".
    applyNow->setEnabled(lxqt ? lxqtWallpapers != nullptr
                              : (wallpapers && currentDesktop >= 2));
    applyRow->addWidget(applyNow);
    applyRow->addStretch();
    layout->addLayout(applyRow);

    layout->addStretch();

    // ---- Wiring -------------------------------------------------------------
    connect(enable, &QCheckBox::toggled, this, [wallpapers, lxqtWallpapers, lxqt](bool on) {
        DesktopWallpapers::setEnabled(on);
        if (lxqt) {
            if (!lxqtWallpapers)
                return;
            // Turning it off has to take the surfaces down and hand the desktop
            // back to PCManFM right away — the user is looking at the screen.
            if (on)
                lxqtWallpapers->start();
            else
                lxqtWallpapers->stop();
            return;
        }
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
    connect(applyNow, &QPushButton::clicked, this, [this, wallpapers, lxqtWallpapers, lxqt] {
        if (!m_manager)
            return;
        if (lxqt) {
            if (lxqtWallpapers)
                lxqtWallpapers->apply(qMax(1, m_manager->currentDesktop()));
            return;
        }
        if (wallpapers)
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
            tr("Alias para \"%1\" (vacío: dejar solo el monitor y el número):")
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

    // A dock moved via the context menu (Dock → Mover Sig. Monitor) changes the
    // enabled/known dock sets out from under the dialog; refresh this tab so its
    // lists match reality instead of going stale.
    // Bound to `tab` and registered here rather than in buildTabs(): the manager
    // outlives every rebuild, so on `this` each dock switch left one more live
    // copy of this connection behind.
    if (m_manager) {
        connect(m_manager, &DockManager::dockListChanged, tab, [this] {
            reloadDocksList();
            reloadMonitorsForSelectedDock();
        });
    }

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
