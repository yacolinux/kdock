#include "previewsettingsdialog.h"

#include "previewconfig.h"
#include "previewmanager.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

PreviewSettingsDialog::PreviewSettingsDialog(PreviewManager *manager, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
{
    setWindowTitle(tr("Dock Preview — Configuración"));
    setMinimumWidth(460);
    // Default size that fits every group on a typical screen, clamped so it never
    // exceeds the monitor — the scroll area takes over from there.
    QScreen *scr = QGuiApplication::primaryScreen();
    const int availH = scr ? scr->availableGeometry().height() : 800;
    resize(755, qMin(972, availH - 40));

    auto *layout = new QVBoxLayout(this);

    // The same switch kdock's Previews tab shows: with it off nothing is drawn,
    // so this panel is still usable to set things up before turning it on.
    m_masterEnabled = new QCheckBox(tr("Activar el Dock Preview"), this);
    m_masterEnabled->setChecked(m_manager->enabled());
    layout->addWidget(m_masterEnabled);
    connect(m_masterEnabled, &QCheckBox::toggled, this,
            [this](bool on) { m_manager->setEnabled(on); });

    // ---- Monitor selector --------------------------------------------------
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Monitor:"), this));
    m_screenSelector = new QComboBox(this);
    top->addWidget(m_screenSelector, 1);
    layout->addLayout(top);

    m_enabled = new QCheckBox(tr("Mostrar la tira de vistas previas en este monitor"), this);
    layout->addWidget(m_enabled);

    m_controls = new QWidget(this);
    m_controlsLayout = new QVBoxLayout(m_controls);
    m_controlsLayout->setContentsMargins(0, 0, 0, 0);

    // New groups are appended to m_controlsLayout, so the panel scrolls instead
    // of growing past the screen; the monitor selector and the Close button stay
    // pinned above and below the scroll area.
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(m_controls);
    layout->addWidget(scroll, 1);

    buildControls();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    // Qt labels its standard buttons from *its own* catalogs, i.e. in the system
    // locale, which would leave a Spanish "Cerrar" in a dialog the user asked to
    // see in another language. Setting the text puts it back on our layer.
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);

    connect(m_screenSelector, &QComboBox::currentTextChanged, this,
            &PreviewSettingsDialog::selectScreen);
    connect(m_enabled, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_screenName.isEmpty())
            m_manager->setScreenEnabled(m_screenName, on);
    });

    reloadScreens();
}

void PreviewSettingsDialog::reloadScreens()
{
    const QString previous = m_screenName;
    const QStringList screens = m_manager->knownScreensForUi();

    QSignalBlocker blocker(m_screenSelector);
    m_screenSelector->clear();
    m_screenSelector->addItems(screens);
    blocker.unblock();

    if (screens.isEmpty())
        return;
    const int index = qMax(0, m_screenSelector->findText(previous));
    m_screenSelector->setCurrentIndex(index);
    selectScreen(m_screenSelector->currentText());
}

void PreviewSettingsDialog::buildControls()
{
    // Every control is connected once and reads m_config at the moment it fires;
    // switching monitor only repoints that pointer and reloads the values.

    // ---- Position and size -------------------------------------------------
    auto *geo = new QGroupBox(tr("Posición y tamaño"), m_controls);
    auto *geoForm = new QFormLayout(geo);

    m_edge = new QComboBox(geo);
    // Index order matches PreviewConfig::Edge.
    m_edge->addItems({tr("Abajo"), tr("Arriba"), tr("Izquierda"), tr("Derecha")});
    geoForm->addRow(tr("Borde:"), m_edge);
    connect(m_edge, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (m_config)
            m_config->setEdge(v);
        updateThicknessLabel();
    });

    m_alignment = new QComboBox(geo);
    m_alignment->addItems({tr("Inicio"), tr("Centro"), tr("Fin")});
    geoForm->addRow(tr("Alineación:"), m_alignment);
    connect(m_alignment, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (m_config)
            m_config->setAlignment(v);
    });

    m_alignmentNote = new QLabel(
        tr("Con un largo menor a 100% mueve la tira; con «Todo el borde», las tarjetas dentro."),
        geo);
    m_alignmentNote->setWordWrap(true);
    geoForm->addRow(QString(), m_alignmentNote);

    m_thickness = new QSpinBox(geo);
    m_thickness->setRange(PreviewConfig::kMinThickness, PreviewConfig::kMaxThickness);
    m_thickness->setSuffix(tr(" px"));
    // The cross axis is the *height* on a top/bottom strip and the *width* on a
    // left/right one: one fixed word left half the users looking for a control
    // that was already there. The label follows the edge combo live.
    m_thicknessLabel = new QLabel(geo);
    geoForm->addRow(m_thicknessLabel, m_thickness);
    connect(m_thickness, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_config)
            m_config->setStripThickness(v);
    });

    m_length = new QSpinBox(geo);
    m_length->setRange(0, 100);
    m_length->setSuffix(tr(" %"));
    m_length->setSpecialValueText(tr("Todo el borde"));
    geoForm->addRow(tr("Largo:"), m_length);
    connect(m_length, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_config)
            m_config->setStripLength(v);
    });

    m_margin = new QSpinBox(geo);
    m_margin->setRange(0, 200);
    m_margin->setSuffix(tr(" px"));
    geoForm->addRow(tr("Margen:"), m_margin);
    connect(m_margin, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_config)
            m_config->setScreenMargin(v);
    });

    m_controlsLayout->addWidget(geo);

    // ---- Appearance --------------------------------------------------------
    auto *look = new QGroupBox(tr("Apariencia"), m_controls);
    auto *lookForm = new QFormLayout(look);

    m_opacity = new QSlider(Qt::Horizontal, look);
    m_opacity->setRange(10, 100);
    lookForm->addRow(tr("Opacidad:"), m_opacity);
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        if (m_config)
            m_config->setOpacity(v / 100.0);
    });

    auto *colorRow = new QHBoxLayout;
    m_colorButton = new QPushButton(tr("Elegir…"), look);
    m_colorReset = new QPushButton(tr("Del tema"), look);
    colorRow->addWidget(m_colorButton, 1);
    colorRow->addWidget(m_colorReset);
    lookForm->addRow(tr("Color de fondo:"), colorRow);
    connect(m_colorButton, &QPushButton::clicked, this, &PreviewSettingsDialog::pickColor);
    connect(m_colorReset, &QPushButton::clicked, this, [this] {
        if (m_config) {
            m_config->resetPanelColor();
            updateColorButton();
        }
    });

    m_showTitles = new QCheckBox(tr("Mostrar el título de la ventana"), look);
    lookForm->addRow(QString(), m_showTitles);
    connect(m_showTitles, &QCheckBox::toggled, this, [this](bool v) {
        if (m_config)
            m_config->setShowTitles(v);
    });

    // Kept short on purpose: a longer string pushes the dialog into a
    // horizontal scrollbar (seen under the Xvfb probe).
    m_showScrollBar = new QCheckBox(tr("Mostrar una barra de desplazamiento fina"), look);
    m_showScrollBar->setToolTip(tr("Solo aparece cuando las tarjetas no entran en la tira."));
    lookForm->addRow(QString(), m_showScrollBar);
    connect(m_showScrollBar, &QCheckBox::toggled, this, [this](bool v) {
        if (m_config)
            m_config->setShowScrollBar(v);
    });

    m_cardSpacing = new QSpinBox(look);
    m_cardSpacing->setRange(0, 60);
    m_cardSpacing->setSuffix(tr(" px"));
    lookForm->addRow(tr("Espacio entre tarjetas:"), m_cardSpacing);
    connect(m_cardSpacing, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_config)
            m_config->setCardSpacing(v);
    });

    m_controlsLayout->addWidget(look);

    // ---- Behaviour ---------------------------------------------------------
    auto *behave = new QGroupBox(tr("Comportamiento"), m_controls);
    auto *behaveLayout = new QVBoxLayout(behave);

    m_reserveSpace = new QCheckBox(
        tr("Reservar espacio en pantalla (las ventanas maximizadas se achican)"), behave);
    behaveLayout->addWidget(m_reserveSpace);
    connect(m_reserveSpace, &QCheckBox::toggled, this, [this](bool v) {
        if (m_config)
            m_config->setReserveSpace(v);
    });

    m_autohide = new QCheckBox(tr("Ocultar automáticamente (aparece al acercar el mouse)"),
                               behave);
    behaveLayout->addWidget(m_autohide);
    connect(m_autohide, &QCheckBox::toggled, this, [this](bool v) {
        if (m_config)
            m_config->setAutohide(v);
    });

    m_controlsLayout->addWidget(behave);

    // ---- Thumbnails --------------------------------------------------------
    auto *thumbs = new QGroupBox(tr("Miniaturas"), m_controls);
    auto *thumbsForm = new QFormLayout(thumbs);

    m_captureMode = new QComboBox(thumbs);
    // Index order matches PreviewConfig::CaptureMode.
    m_captureMode->addItems({tr("Una captura por ventana (al pasar a primer plano)"),
                             tr("Refresco periódico (experimental)")});
    thumbsForm->addRow(tr("Capturas:"), m_captureMode);
    connect(m_captureMode, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (m_config)
            m_config->setCaptureMode(v);
        updateThumbControls();
    });

    m_autoFit = new QCheckBox(
        tr("Ajustar el tamaño de las tarjetas al contenido (con muchas ventanas se achican)"),
        thumbs);
    thumbsForm->addRow(QString(), m_autoFit);
    connect(m_autoFit, &QCheckBox::toggled, this, [this](bool on) {
        if (m_config)
            m_config->setAutoFitCards(on);
        updateThumbControls();
    });

    m_fitMin = new QSpinBox(thumbs);
    m_fitMin->setRange(48, 800);
    m_fitMin->setSuffix(tr(" px"));
    thumbsForm->addRow(tr("Tamaño mínimo de tarjeta:"), m_fitMin);
    connect(m_fitMin, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_config)
            m_config->setFitMinCardWidth(v);
    });

    m_refresh = new QSpinBox(thumbs);
    m_refresh->setRange(500, 60000);
    m_refresh->setSingleStep(500);
    m_refresh->setSuffix(tr(" ms"));
    thumbsForm->addRow(tr("Refresco:"), m_refresh);
    connect(m_refresh, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_config)
            m_config->setRefreshInterval(v);
    });

    m_activeRefresh = new QSpinBox(thumbs);
    m_activeRefresh->setRange(300, 60000);
    m_activeRefresh->setSingleStep(100);
    m_activeRefresh->setSuffix(tr(" ms"));
    thumbsForm->addRow(tr("Refresco de la ventana activa:"), m_activeRefresh);
    connect(m_activeRefresh, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_config)
            m_config->setActiveRefreshInterval(v);
    });

    m_includeMinimized = new QCheckBox(tr("Incluir ventanas minimizadas"), thumbs);
    thumbsForm->addRow(QString(), m_includeMinimized);
    connect(m_includeMinimized, &QCheckBox::toggled, this, [this](bool v) {
        if (m_config)
            m_config->setIncludeMinimized(v);
    });

    m_currentDesktopOnly = new QCheckBox(tr("Solo el escritorio virtual actual"), thumbs);
    thumbsForm->addRow(QString(), m_currentDesktopOnly);
    connect(m_currentDesktopOnly, &QCheckBox::toggled, this, [this](bool v) {
        if (m_config)
            m_config->setCurrentDesktopOnly(v);
    });

    m_thisMonitorOnly = new QCheckBox(tr("Solo las ventanas de este monitor"), thumbs);
    thumbsForm->addRow(QString(), m_thisMonitorOnly);
    connect(m_thisMonitorOnly, &QCheckBox::toggled, this, [this](bool v) {
        if (m_config)
            m_config->setThisMonitorOnly(v);
    });

    m_controlsLayout->addWidget(thumbs);
    m_controlsLayout->addStretch(1);
}

void PreviewSettingsDialog::selectScreen(const QString &screenName)
{
    if (screenName.isEmpty())
        return;
    m_screenName = screenName;
    m_config = m_manager->configFor(screenName);

    // Values only: blocking the signals keeps the reload from writing straight
    // back into the config it is reading.
    const QSignalBlocker blockers[] = {
        QSignalBlocker(m_enabled),            QSignalBlocker(m_edge),
        QSignalBlocker(m_alignment),          QSignalBlocker(m_thickness),
        QSignalBlocker(m_length),             QSignalBlocker(m_margin),
        QSignalBlocker(m_opacity),            QSignalBlocker(m_showTitles),
        QSignalBlocker(m_showScrollBar),      QSignalBlocker(m_cardSpacing),
        QSignalBlocker(m_reserveSpace),       QSignalBlocker(m_autohide),
        QSignalBlocker(m_refresh),
        QSignalBlocker(m_activeRefresh),      QSignalBlocker(m_includeMinimized),
        QSignalBlocker(m_currentDesktopOnly), QSignalBlocker(m_thisMonitorOnly),
        QSignalBlocker(m_captureMode),        QSignalBlocker(m_autoFit),
        QSignalBlocker(m_fitMin),
    };
    Q_UNUSED(blockers);

    m_enabled->setChecked(m_manager->isScreenEnabled(screenName));
    m_edge->setCurrentIndex(m_config->edge());
    m_alignment->setCurrentIndex(m_config->alignment());
    m_thickness->setValue(m_config->stripThickness());
    updateThicknessLabel();
    m_length->setValue(m_config->stripLength());
    m_margin->setValue(m_config->screenMargin());
    m_opacity->setValue(qRound(m_config->opacity() * 100));
    m_showTitles->setChecked(m_config->showTitles());
    m_showScrollBar->setChecked(m_config->showScrollBar());
    m_cardSpacing->setValue(m_config->cardSpacing());
    m_reserveSpace->setChecked(m_config->reserveSpace());
    m_autohide->setChecked(m_config->autohide());
    m_captureMode->setCurrentIndex(m_config->captureMode());
    m_autoFit->setChecked(m_config->autoFitCards());
    m_fitMin->setValue(m_config->fitMinCardWidth());
    m_refresh->setValue(m_config->refreshInterval());
    m_activeRefresh->setValue(m_config->activeRefreshInterval());
    m_includeMinimized->setChecked(m_config->includeMinimized());
    m_currentDesktopOnly->setChecked(m_config->currentDesktopOnly());
    m_thisMonitorOnly->setChecked(m_config->thisMonitorOnly());
    updateThumbControls();
    updateColorButton();
}

void PreviewSettingsDialog::updateThumbControls()
{
    const bool periodic = m_config && m_config->captureMode() == PreviewConfig::Periodic;
    m_refresh->setEnabled(periodic);
    m_activeRefresh->setEnabled(periodic);
    // The minimum size only means something while auto-fit is on.
    m_fitMin->setEnabled(m_autoFit && m_autoFit->isChecked());
}

void PreviewSettingsDialog::updateThicknessLabel()
{
    if (!m_thicknessLabel)
        return;
    // Read the combo, not the config: the label has to follow the edge the user
    // just picked even before the config emits.
    const int edge = m_edge ? m_edge->currentIndex() : PreviewConfig::Bottom;
    const bool horizontal = edge == PreviewConfig::Bottom || edge == PreviewConfig::Top;
    m_thicknessLabel->setText(horizontal ? tr("Alto del panel:") : tr("Ancho del panel:"));
}

void PreviewSettingsDialog::updateColorButton()
{
    if (!m_config)
        return;
    if (m_config->panelColorSet()) {
        const QColor color = m_config->panelColor();
        // A swatch beats a hex string when picking a background.
        m_colorButton->setText(color.name());
        m_colorButton->setStyleSheet(
            QStringLiteral("background-color: %1; color: %2;")
                .arg(color.name(), color.lightnessF() > 0.5 ? QStringLiteral("black")
                                                            : QStringLiteral("white")));
    } else {
        m_colorButton->setText(tr("Del tema"));
        m_colorButton->setStyleSheet(QString());
    }
}

void PreviewSettingsDialog::pickColor()
{
    if (!m_config)
        return;
    const QColor initial = m_config->panelColorSet() ? m_config->panelColor() : QColor(Qt::black);
    const QColor chosen = QColorDialog::getColor(initial, this, tr("Color de fondo"));
    if (!chosen.isValid())
        return;
    m_config->setPanelColor(chosen);
    updateColorButton();
}
