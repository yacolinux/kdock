#include "cmsettingsdialog.h"

#include "cmconfig.h"
#include "cmlayout.h"
#include "cmsections.h"
#include "themepicker.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

CmSettingsDialog::CmSettingsDialog(CmConfig *config, CmLayout *layout,
                                   AppearanceControl *appearance, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
    , m_layout(layout)
    , m_appearance(appearance)
{
    setWindowTitle(tr("Control Manager"));

    auto *content = new QWidget;
    auto *inner = new QVBoxLayout(content);
    inner->addWidget(createWindowGroup());
    inner->addWidget(createAppearanceGroup());
    inner->addWidget(createGridGroup());
    inner->addWidget(createSectionsGroup());
    inner->addWidget(createLayoutGroup());
    inner->addStretch();

    // The panel is taller than a small screen, same reason SettingsDialog wraps
    // each of its tabs in one of these.
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setWidget(content);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    // Qt labels its standard buttons from *its own* catalogs, i.e. in the system
    // locale, which would leave a Spanish "Cerrar" in a dialog the user asked to
    // see in another language. Setting the text puts it back on our layer.
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *root = new QVBoxLayout(this);
    root->addWidget(m_scroll);
    root->addWidget(buttons);

    // The panel is taller than its window, so the user scrolls it — and a wheel
    // event over a spin box or a combo edits that control instead of scrolling.
    // Only widgets that were clicked into keep the wheel; for the rest it goes
    // to the scroll area.
    for (QWidget *w : content->findChildren<QWidget *>()) {
        if (qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QComboBox *>(w)) {
            w->setFocusPolicy(Qt::StrongFocus);
            w->installEventFilter(this);
        }
    }

    const QRect avail = screen() ? screen()->availableGeometry() : QRect(0, 0, 1280, 800);
    const int wanted = content->sizeHint().width() + 60;
    resize(qMin(qMax(620, wanted), avail.width() - 80), qMin(760, avail.height() - 80));
}

bool CmSettingsDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Wheel) {
        auto *w = qobject_cast<QWidget *>(watched);
        if (w && !w->hasFocus()) {
            if (m_scroll)
                QCoreApplication::sendEvent(m_scroll->viewport(), event);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

QWidget *CmSettingsDialog::createWindowGroup()
{
    auto *box = new QGroupBox(tr("Ventana"), this);
    auto *form = new QFormLayout(box);

    auto *edge = new QComboBox(box);
    edge->addItems({tr("Arriba"), tr("Abajo"), tr("Izquierda"), tr("Derecha")});
    edge->setCurrentIndex(m_config->edge());
    connect(edge, &QComboBox::currentIndexChanged, m_config, &CmConfig::setEdge);
    form->addRow(tr("Borde:"), edge);

    auto *align = new QComboBox(box);
    align->addItems({tr("Al principio"), tr("Centrado"), tr("Al final")});
    align->setCurrentIndex(m_config->alignment());
    connect(align, &QComboBox::currentIndexChanged, m_config, &CmConfig::setAlignment);
    form->addRow(tr("Alineación:"), align);

    auto *width = new QSpinBox(box);
    width->setRange(240, 8000);
    width->setSuffix(tr(" px"));
    width->setValue(m_config->panelWidth());
    connect(width, &QSpinBox::valueChanged, m_config, &CmConfig::setPanelWidth);
    form->addRow(tr("Ancho:"), width);

    auto *widthPct = new QSpinBox(box);
    widthPct->setRange(0, 100);
    widthPct->setSpecialValueText(tr("usar los píxeles de arriba"));
    widthPct->setSuffix(tr(" % de la pantalla"));
    widthPct->setValue(m_config->panelWidthPercent());
    connect(widthPct, &QSpinBox::valueChanged, m_config, &CmConfig::setPanelWidthPercent);
    connect(m_config, &CmConfig::windowChanged, width,
            [this, width] { width->setEnabled(m_config->panelWidthPercent() == 0); });
    width->setEnabled(m_config->panelWidthPercent() == 0);
    // Dragging a corner of the panel clears the percentage (it writes pixels);
    // this copy has to follow, or it keeps showing a value that no longer
    // applies. Setters only emit on a real change, so no loop.
    connect(m_config, &CmConfig::windowChanged, widthPct,
            [this, widthPct] { widthPct->setValue(m_config->panelWidthPercent()); });
    form->addRow(tr("Ancho relativo:"), widthPct);

    auto *height = new QSpinBox(box);
    height->setRange(160, 8000);
    height->setSuffix(tr(" px"));
    height->setValue(m_config->panelHeight());
    connect(height, &QSpinBox::valueChanged, m_config, &CmConfig::setPanelHeight);
    form->addRow(tr("Alto:"), height);

    auto *heightPct = new QSpinBox(box);
    heightPct->setRange(0, 100);
    heightPct->setSpecialValueText(tr("usar los píxeles de arriba"));
    heightPct->setSuffix(tr(" % de la pantalla"));
    heightPct->setValue(m_config->panelHeightPercent());
    connect(heightPct, &QSpinBox::valueChanged, m_config, &CmConfig::setPanelHeightPercent);
    connect(m_config, &CmConfig::windowChanged, height,
            [this, height] { height->setEnabled(m_config->panelHeightPercent() == 0); });
    height->setEnabled(m_config->panelHeightPercent() == 0);
    connect(m_config, &CmConfig::windowChanged, heightPct,
            [this, heightPct] { heightPct->setValue(m_config->panelHeightPercent()); });
    form->addRow(tr("Alto relativo:"), heightPct);

    auto *margin = new QSpinBox(box);
    margin->setRange(0, 400);
    margin->setSuffix(tr(" px"));
    margin->setValue(m_config->screenMargin());
    connect(margin, &QSpinBox::valueChanged, m_config, &CmConfig::setScreenMargin);
    form->addRow(tr("Margen con el borde:"), margin);

    auto *keepOpen = new QCheckBox(tr("Ventana permanente (no se cierra sola)"), box);
    keepOpen->setChecked(m_config->keepOpen());
    keepOpen->setToolTip(tr("Con esto puesto no se cierra ni con Esc, ni al perder el foco, "
                            "ni al salir el puntero: queda como un panel fijo."));
    connect(keepOpen, &QCheckBox::toggled, m_config, &CmConfig::setKeepOpen);
    connect(m_config, &CmConfig::settingsChanged, keepOpen,
            [this, keepOpen] { keepOpen->setChecked(m_config->keepOpen()); });
    form->addRow(tr("Permanente:"), keepOpen);

    auto *closeFocus = new QCheckBox(tr("Cerrar al perder el foco"), box);
    closeFocus->setChecked(m_config->closeOnFocusLoss());
    connect(closeFocus, &QCheckBox::toggled, m_config, &CmConfig::setCloseOnFocusLoss);
    form->addRow(tr("Foco:"), closeFocus);

    auto *closeLeave = new QCheckBox(tr("Cerrar cuando el puntero se va"), box);
    closeLeave->setChecked(m_config->closeOnLeave());
    connect(closeLeave, &QCheckBox::toggled, m_config, &CmConfig::setCloseOnLeave);
    form->addRow(tr("Puntero:"), closeLeave);

    auto *preload = new QCheckBox(tr("Dejarlo cargado al iniciar kdock"), box);
    preload->setChecked(CmConfig::preload());
    preload->setToolTip(tr("Sin esto, el proceso arranca en el primer clic (medio segundo) y "
                           "queda residente: las aperturas siguientes son instantáneas."));
    connect(preload, &QCheckBox::toggled, this, [](bool on) { CmConfig::setPreload(on); });
    form->addRow(tr("Precargar:"), preload);

    return box;
}

QWidget *CmSettingsDialog::createAppearanceGroup()
{
    auto *box = new QGroupBox(tr("Apariencia"), this);
    auto *form = new QFormLayout(box);

    auto *mode = new QComboBox(box);
    mode->addItems({tr("Color del tema"), tr("Color propio")});
    mode->setCurrentIndex(m_config->backgroundMode());
    connect(mode, &QComboBox::currentIndexChanged, m_config, &CmConfig::setBackgroundMode);
    form->addRow(tr("Fondo:"), mode);

    auto *colorBtn = new QPushButton(box);
    const auto refreshColor = [this, colorBtn] {
        const QColor c = m_config->backgroundColor();
        colorBtn->setText(c.isValid() ? c.name() : tr("(sin elegir)"));
    };
    refreshColor();
    connect(colorBtn, &QPushButton::clicked, this, [this, refreshColor] {
        const QColor c = QColorDialog::getColor(m_config->backgroundColor(), this,
                                                tr("Color de fondo del panel"));
        if (c.isValid()) {
            m_config->setBackgroundColor(c);
            refreshColor();
        }
    });
    form->addRow(tr("Color propio:"), colorBtn);

    // Font colour, the mirror of the background control above: automatic (the
    // luminance-derived contrast) or a colour of your own, applied to every
    // widget unless a card overrides it from its own "Color de fuente" menu.
    auto *fgMode = new QComboBox(box);
    fgMode->addItems({tr("Automático"), tr("Color propio")});
    fgMode->setCurrentIndex(m_config->foregroundMode());
    connect(fgMode, &QComboBox::currentIndexChanged, m_config, &CmConfig::setForegroundMode);
    form->addRow(tr("Fuente:"), fgMode);

    auto *fgColorBtn = new QPushButton(box);
    const auto refreshFgColor = [this, fgColorBtn] {
        const QColor c = m_config->foregroundColor();
        fgColorBtn->setText(c.isValid() ? c.name() : tr("(sin elegir)"));
    };
    refreshFgColor();
    connect(fgColorBtn, &QPushButton::clicked, this, [this, refreshFgColor] {
        const QColor c = QColorDialog::getColor(m_config->foregroundColor(), this,
                                                tr("Color de fuente"));
        if (c.isValid()) {
            m_config->setForegroundColor(c);
            refreshFgColor();
        }
    });
    form->addRow(tr("Color de fuente:"), fgColorBtn);

    auto *opacity = new QDoubleSpinBox(box);
    // From 0.0: the canvas can be fully transparent (its default), unlike the
    // control panel whose floor is 0.10. This is the *canvas* background — the
    // per-widget transparency below is a separate setting.
    opacity->setRange(0.0, 1.0);
    opacity->setSingleStep(0.05);
    opacity->setValue(m_config->backgroundOpacity());
    connect(opacity, &QDoubleSpinBox::valueChanged, m_config, &CmConfig::setBackgroundOpacity);
    connect(m_config, &CmConfig::settingsChanged, opacity, [this, opacity] {
        if (!qFuzzyCompare(opacity->value(), m_config->backgroundOpacity()))
            opacity->setValue(m_config->backgroundOpacity());
    });
    form->addRow(tr("Opacidad:"), opacity);

    // General transparency of every widget/card, as a percentage (0 = opaque).
    // Stored as the opposite opacity (widgetOpacity = 1 - transparency); a card
    // can override it from its right-click "Transparencia" submenu.
    auto *widgetTransp = new QSpinBox(box);
    widgetTransp->setRange(0, 100);
    widgetTransp->setSuffix(tr(" %"));
    widgetTransp->setValue(qRound((1.0 - m_config->widgetOpacity()) * 100));
    connect(widgetTransp, &QSpinBox::valueChanged, m_config,
            [this](int v) { m_config->setWidgetOpacity(1.0 - v / 100.0); });
    connect(m_config, &CmConfig::settingsChanged, widgetTransp, [this, widgetTransp] {
        const int v = qRound((1.0 - m_config->widgetOpacity()) * 100);
        if (widgetTransp->value() != v)
            widgetTransp->setValue(v);
    });
    form->addRow(tr("Transparencia de los widgets:"), widgetTransp);

    auto *imageRow = new QWidget(box);
    auto *imageLayout = new QHBoxLayout(imageRow);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    auto *imageEdit = new QLineEdit(m_config->backgroundImage(), imageRow);
    auto *imageBtn = new QPushButton(tr("Examinar…"), imageRow);
    auto *imageClear = new QPushButton(tr("Quitar"), imageRow);
    imageLayout->addWidget(imageEdit, 1);
    imageLayout->addWidget(imageBtn);
    imageLayout->addWidget(imageClear);
    connect(imageEdit, &QLineEdit::editingFinished, this,
            [this, imageEdit] { m_config->setBackgroundImage(imageEdit->text()); });
    connect(imageBtn, &QPushButton::clicked, this, [this, imageEdit] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Imagen de fondo del panel"), imageEdit->text(),
            tr("Imágenes (*.png *.jpg *.jpeg *.webp *.bmp *.svg);;Todos los archivos (*)"));
        if (!path.isEmpty()) {
            imageEdit->setText(path);
            m_config->setBackgroundImage(path);
        }
    });
    connect(imageClear, &QPushButton::clicked, this, [this, imageEdit] {
        imageEdit->clear();
        m_config->setBackgroundImage(QString());
    });
    form->addRow(tr("Imagen de fondo:"), imageRow);

    auto *radius = new QSpinBox(box);
    radius->setRange(0, 48);
    radius->setSuffix(tr(" px"));
    radius->setValue(m_config->cornerRadius());
    connect(radius, &QSpinBox::valueChanged, m_config, &CmConfig::setCornerRadius);
    form->addRow(tr("Esquinas redondeadas:"), radius);

    auto *tabs = new QComboBox(box);
    tabs->addItems({tr("Arriba"), tr("Abajo")});
    tabs->setCurrentIndex(m_config->tabsPosition());
    connect(tabs, &QComboBox::currentIndexChanged, m_config, &CmConfig::setTabsPosition);
    form->addRow(tr("Solapas:"), tabs);

    auto *tabIcons = new QCheckBox(tr("Íconos en las solapas"), box);
    tabIcons->setChecked(m_config->showTabIcons());
    connect(tabIcons, &QCheckBox::toggled, m_config, &CmConfig::setShowTabIcons);
    form->addRow(QString(), tabIcons);

    auto *cardTitles = new QCheckBox(tr("Título en cada tarjeta"), box);
    cardTitles->setChecked(m_config->showCardTitles());
    connect(cardTitles, &QCheckBox::toggled, m_config, &CmConfig::setShowCardTitles);
    form->addRow(QString(), cardTitles);

    auto *bold = new QCheckBox(tr("Textos en negrita"), box);
    bold->setChecked(m_config->labelBold());
    connect(bold, &QCheckBox::toggled, m_config, &CmConfig::setLabelBold);
    form->addRow(QString(), bold);

    // Every text of the panel scales from this one size. 0 = the historic fixed
    // sizes (what the panel always drew before the setting existed).
    auto *fontSize = new QSpinBox(box);
    fontSize->setRange(0, 24);
    fontSize->setSpecialValueText(tr("Predeterminado"));
    fontSize->setSuffix(tr(" px"));
    fontSize->setValue(m_config->fontSize());
    fontSize->setToolTip(tr("Tamaño de todas las fuentes del panel: solapas, "
                            "botones, tarjetas y cada sección. 12 px es el "
                            "tamaño por defecto; 0 lo deja."));
    connect(fontSize, &QSpinBox::valueChanged, m_config, &CmConfig::setFontSize);
    form->addRow(tr("Tamaño de fuente:"), fontSize);

    // The panel's icon set. Empty = follow the desktop's icon theme, adapted to
    // the panel background (the luminance pair the dock itself uses).
    if (m_appearance) {
        auto *iconPicker = new ThemePickerButton(m_appearance, QStringLiteral("icons"),
                                                 ThemePickerPopup::PickValue, box);
        iconPicker->setSpecialEntry(tr("(seguir el del sistema)"));
        iconPicker->setCurrentId(m_config->iconTheme());
        iconPicker->setToolTip(tr("Íconos de las solapas, tarjetas y botones. "
                                  "Vacíos siguen el iconset del escritorio "
                                  "adaptado al fondo del panel."));
        connect(iconPicker, &ThemePickerButton::picked, m_config, &CmConfig::setIconTheme);
        connect(m_config, &CmConfig::settingsChanged, iconPicker,
                [this, iconPicker] { iconPicker->setCurrentId(m_config->iconTheme()); });
        form->addRow(tr("Iconset del panel:"), iconPicker);
    }

    // The buttons inside the tabs (and the ones on Principal) are the only
    // controls the panel does not yet let you size. 0 = natural size.
    auto *btnW = new QSpinBox(box);
    btnW->setRange(0, 600);
    btnW->setSpecialValueText(tr("Automático"));
    btnW->setSuffix(tr(" px"));
    btnW->setValue(m_config->buttonWidth());
    btnW->setToolTip(tr("Ancho mínimo de los botones de las solapas. Un botón con "
                        "texto largo puede ser más ancho; 0 deja el tamaño natural."));
    connect(btnW, &QSpinBox::valueChanged, m_config, &CmConfig::setButtonWidth);
    form->addRow(tr("Ancho mínimo de los botones:"), btnW);

    auto *btnH = new QSpinBox(box);
    btnH->setRange(0, 400);
    btnH->setSpecialValueText(tr("Automático"));
    btnH->setSuffix(tr(" px"));
    btnH->setValue(m_config->buttonHeight());
    btnH->setToolTip(tr("Alto mínimo de los botones de las solapas; 0 deja el "
                        "tamaño natural."));
    connect(btnH, &QSpinBox::valueChanged, m_config, &CmConfig::setButtonHeight);
    form->addRow(tr("Alto mínimo de los botones:"), btnH);

    return box;
}

QWidget *CmSettingsDialog::createGridGroup()
{
    auto *box = new QGroupBox(tr("Grilla de la solapa Principal"), this);
    auto *form = new QFormLayout(box);

    auto *columns = new QSpinBox(box);
    columns->setRange(0, 24);
    columns->setSpecialValueText(tr("Automáticas (según el ancho)"));
    columns->setValue(m_config->columns());
    connect(columns, &QSpinBox::valueChanged, m_config, &CmConfig::setColumns);
    form->addRow(tr("Columnas:"), columns);

    auto *cellSize = new QSpinBox(box);
    cellSize->setRange(32, 400);
    cellSize->setSuffix(tr(" px"));
    cellSize->setValue(m_config->cellSize());
    cellSize->setToolTip(tr("Ancho de la celda; con «Estirar» encendido el ancho "
                            "se adapta al panel y este valor es el piso."));
    connect(cellSize, &QSpinBox::valueChanged, m_config, &CmConfig::setCellSize);
    form->addRow(tr("Ancho de celda:"), cellSize);

    auto *cellHeight = new QSpinBox(box);
    cellHeight->setRange(32, 600);
    cellHeight->setSuffix(tr(" px"));
    cellHeight->setValue(m_config->cellHeight());
    cellHeight->setToolTip(tr("Alto de la celda. Las tarjetas miden ancho × "
                              "alto de celda, así podés hacerlas más altas que "
                              "anchas."));
    connect(cellHeight, &QSpinBox::valueChanged, m_config, &CmConfig::setCellHeight);
    form->addRow(tr("Alto de celda:"), cellHeight);

    auto *stretch = new QCheckBox(tr("Estirar las celdas para llenar el ancho"), box);
    stretch->setChecked(m_config->cellStretch());
    connect(stretch, &QCheckBox::toggled, m_config, &CmConfig::setCellStretch);
    form->addRow(QString(), stretch);

    auto *cellMin = new QSpinBox(box);
    cellMin->setRange(32, 400);
    cellMin->setSuffix(tr(" px"));
    cellMin->setValue(m_config->cellMin());
    connect(cellMin, &QSpinBox::valueChanged, m_config, &CmConfig::setCellMin);
    form->addRow(tr("Celda mínima:"), cellMin);

    auto *cellMax = new QSpinBox(box);
    cellMax->setRange(32, 600);
    cellMax->setSuffix(tr(" px"));
    cellMax->setValue(m_config->cellMax());
    connect(cellMax, &QSpinBox::valueChanged, m_config, &CmConfig::setCellMax);
    form->addRow(tr("Celda máxima:"), cellMax);

    auto *spacing = new QSpinBox(box);
    spacing->setRange(0, 48);
    spacing->setSuffix(tr(" px"));
    spacing->setValue(m_config->cellSpacing());
    connect(spacing, &QSpinBox::valueChanged, m_config, &CmConfig::setCellSpacing);
    form->addRow(tr("Separación:"), spacing);

    return box;
}

QWidget *CmSettingsDialog::createSectionsGroup()
{
    auto *box = new QGroupBox(tr("Secciones"), this);
    auto *v = new QVBoxLayout(box);

    auto *info = new QLabel(
        tr("«Solapa» le da una solapa propia; «Principal» la muestra además como tarjeta en la "
           "primera solapa, donde se puede mover y cambiar de tamaño arrastrando."),
        box);
    info->setWordWrap(true);
    v->addWidget(info);

    m_sectionsHost = new QWidget(box);
    m_sectionsGrid = new QGridLayout(m_sectionsHost);
    m_sectionsGrid->setContentsMargins(0, 0, 0, 0);
    v->addWidget(m_sectionsHost);
    rebuildSections();

    return box;
}

void CmSettingsDialog::rebuildSections()
{
    // Clear the grid: every row is rebuilt because the order can change.
    while (QLayoutItem *item = m_sectionsGrid->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    const QStringList order = m_config->sectionOrder();
    int row = 0;

    auto *headName = new QLabel(tr("Sección"), m_sectionsHost);
    auto *headTab = new QLabel(tr("Solapa"), m_sectionsHost);
    auto *headCard = new QLabel(tr("Principal"), m_sectionsHost);
    m_sectionsGrid->addWidget(headName, row, 0);
    m_sectionsGrid->addWidget(headTab, row, 1);
    m_sectionsGrid->addWidget(headCard, row, 2);
    ++row;

    for (int i = 0; i < order.size(); ++i) {
        const QString id = order.at(i);
        const CmSectionInfo info = CmSections::byId(id);

        auto *name = new QLabel(CmSections::label(id), m_sectionsHost);
        name->setToolTip(CmSections::description(id));

        auto *tab = new QCheckBox(m_sectionsHost);
        tab->setChecked(m_config->sectionEnabled(id));
        // A card-only section (the clock) can never have a tab; showing the box
        // enabled would promise something the panel cannot do.
        tab->setEnabled(info.hasTab);
        tab->setToolTip(info.hasTab ? CmSections::description(id)
                                    : tr("Esta sección solo existe como tarjeta."));
        connect(tab, &QCheckBox::toggled, this,
                [this, id](bool on) { m_config->setSectionEnabled(id, on); });

        auto *card = new QCheckBox(m_sectionsHost);
        card->setChecked(m_config->cardEnabled(id));
        connect(card, &QCheckBox::toggled, this,
                [this, id](bool on) { m_config->setCardEnabled(id, on); });

        auto *up = new QToolButton(m_sectionsHost);
        up->setText(QStringLiteral("▲"));
        up->setEnabled(i > 0);
        connect(up, &QToolButton::clicked, this, [this, i] {
            m_config->moveSection(i, i - 1);
            rebuildSections();
        });

        auto *down = new QToolButton(m_sectionsHost);
        down->setText(QStringLiteral("▼"));
        down->setEnabled(i < order.size() - 1);
        connect(down, &QToolButton::clicked, this, [this, i] {
            m_config->moveSection(i, i + 1);
            rebuildSections();
        });

        m_sectionsGrid->addWidget(name, row, 0);
        m_sectionsGrid->addWidget(tab, row, 1);
        m_sectionsGrid->addWidget(card, row, 2);
        m_sectionsGrid->addWidget(up, row, 3);
        m_sectionsGrid->addWidget(down, row, 4);
        ++row;
    }
    m_sectionsGrid->setColumnStretch(0, 1);
}

QWidget *CmSettingsDialog::createLayoutGroup()
{
    auto *box = new QGroupBox(tr("Disposición y scripts"), this);
    auto *v = new QVBoxLayout(box);

    auto *form = new QFormLayout;
    auto *scriptRow = new QWidget(box);
    auto *scriptLayout = new QHBoxLayout(scriptRow);
    scriptLayout->setContentsMargins(0, 0, 0, 0);
    auto *scriptEdit = new QLineEdit(m_config->wallpaperScript(), scriptRow);
    auto *scriptBtn = new QPushButton(tr("Examinar…"), scriptRow);
    scriptLayout->addWidget(scriptEdit, 1);
    scriptLayout->addWidget(scriptBtn);
    scriptEdit->setToolTip(tr("Se corre con bash cuando se pide avanzar el fondo de todos "
                              "los monitores."));
    connect(scriptEdit, &QLineEdit::editingFinished, this,
            [this, scriptEdit] { m_config->setWallpaperScript(scriptEdit->text()); });
    connect(scriptBtn, &QPushButton::clicked, this, [this, scriptEdit] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Script de fondo"),
                                                          scriptEdit->text());
        if (!path.isEmpty()) {
            scriptEdit->setText(path);
            m_config->setWallpaperScript(path);
        }
    });
    form->addRow(tr("Script de wallpaper:"), scriptRow);
    v->addLayout(form);

    auto *buttons = new QHBoxLayout;
    auto *reset = new QPushButton(tr("Restablecer la disposición"), box);
    auto *exportBtn = new QPushButton(tr("Exportar…"), box);
    auto *importBtn = new QPushButton(tr("Importar…"), box);
    buttons->addWidget(reset);
    buttons->addStretch();
    buttons->addWidget(exportBtn);
    buttons->addWidget(importBtn);
    v->addLayout(buttons);

    connect(reset, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Restablecer la disposición"),
                                  tr("Las tarjetas de la solapa Principal vuelven a acomodarse "
                                     "solas. ¿Seguir?"))
            == QMessageBox::Yes)
            m_layout->resetAll();
    });
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Exportar disposición"),
                                                          QString(), tr("JSON (*.json)"));
        if (!path.isEmpty() && !m_layout->exportToFile(path))
            QMessageBox::warning(this, tr("Exportar"), tr("No se pudo escribir el archivo."));
    });
    connect(importBtn, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Importar disposición"),
                                                          QString(), tr("JSON (*.json)"));
        if (!path.isEmpty() && !m_layout->importFromFile(path))
            QMessageBox::warning(this, tr("Importar"), tr("El archivo no tiene una disposición "
                                                          "válida."));
    });

    return box;
}
