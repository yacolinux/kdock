#include "tilesettingsdialog.h"

#include "tileconfig.h"
#include "tilelayout.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

TileSettingsDialog::TileSettingsDialog(TileConfig *config, TileLayout *layout, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
    , m_layout(layout)
{
    setWindowTitle(tr("Menú de mosaicos"));

    auto *content = new QWidget;
    auto *inner = new QVBoxLayout(content);
    inner->addWidget(createGridGroup());
    inner->addWidget(createAppearanceGroup());
    inner->addWidget(createSidebarGroup());
    inner->addWidget(createBehaviorGroup());
    inner->addWidget(createLayoutGroup());
    inner->addStretch();

    // The panel is taller than a small screen, same reason SettingsDialog wraps
    // each of its tabs in one of these.
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    // Never scroll sideways: the widest row is a checkbox label, and a
    // horizontal scrollbar just hides the end of it.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *root = new QVBoxLayout(this);
    root->addWidget(scroll);
    root->addWidget(buttons);

    // Wide enough for the longest label, and clamped to the screen so the Close
    // button stays reachable on a small one (same reasoning as SettingsDialog).
    const int wanted = content->sizeHint().width() + 60;
    const QRect avail = screen() ? screen()->availableGeometry() : QRect(0, 0, 1280, 800);
    resize(qMin(qMax(620, wanted), avail.width() - 80),
           qMin(720, avail.height() - 80));
}

QWidget *TileSettingsDialog::createGridGroup()
{
    auto *box = new QGroupBox(tr("Grilla"), this);
    auto *form = new QFormLayout(box);

    auto *columns = new QSpinBox(box);
    columns->setRange(0, 40);
    columns->setSpecialValueText(tr("Automáticas (según el ancho)"));
    columns->setValue(m_config->columns());
    columns->setToolTip(tr("Con un número fijo, las posiciones que guardes valen igual en "
                           "cualquier monitor: lo que se adapta es el tamaño de la celda, no "
                           "la matriz."));
    connect(columns, &QSpinBox::valueChanged, m_config, &TileConfig::setColumns);
    form->addRow(tr("Columnas:"), columns);

    auto *stretch = new QCheckBox(tr("Estirar las celdas para llenar el ancho"), box);
    stretch->setChecked(m_config->cellStretch());
    connect(stretch, &QCheckBox::toggled, m_config, &TileConfig::setCellStretch);
    form->addRow(tr("Ajuste:"), stretch);

    auto *cell = new QSpinBox(box);
    cell->setRange(32, 400);
    cell->setSingleStep(4);
    cell->setSuffix(tr(" px"));
    cell->setValue(m_config->cellSize());
    connect(cell, &QSpinBox::valueChanged, m_config, &TileConfig::setCellSize);
    form->addRow(tr("Tamaño de celda:"), cell);

    auto *cellMin = new QSpinBox(box);
    cellMin->setRange(32, 400);
    cellMin->setSuffix(tr(" px"));
    cellMin->setValue(m_config->cellMin());
    form->addRow(tr("Celda mínima:"), cellMin);

    auto *cellMax = new QSpinBox(box);
    cellMax->setRange(32, 600);
    cellMax->setSuffix(tr(" px"));
    cellMax->setValue(m_config->cellMax());
    form->addRow(tr("Celda máxima:"), cellMax);

    // The two bounds of the stretch cannot cross, or the clamp would flip.
    connect(cellMin, &QSpinBox::valueChanged, this, [this, cellMax](int v) {
        m_config->setCellMin(v);
        if (cellMax->value() < v)
            cellMax->setValue(v);
    });
    connect(cellMax, &QSpinBox::valueChanged, this, [this, cellMin](int v) {
        m_config->setCellMax(v);
        if (cellMin->value() > v)
            cellMin->setValue(v);
    });

    const auto syncStretch = [cell, cellMin, cellMax, stretch] {
        cell->setEnabled(!stretch->isChecked());
        cellMin->setEnabled(stretch->isChecked());
        cellMax->setEnabled(stretch->isChecked());
    };
    syncStretch();
    connect(stretch, &QCheckBox::toggled, box, syncStretch);

    auto *spacing = new QSpinBox(box);
    spacing->setRange(0, 48);
    spacing->setSuffix(tr(" px"));
    spacing->setValue(m_config->cellSpacing());
    connect(spacing, &QSpinBox::valueChanged, m_config, &TileConfig::setCellSpacing);
    form->addRow(tr("Separación:"), spacing);

    return box;
}

QWidget *TileSettingsDialog::createAppearanceGroup()
{
    auto *box = new QGroupBox(tr("Apariencia"), this);
    auto *form = new QFormLayout(box);

    auto *mode = new QComboBox(box);
    mode->addItem(tr("Color del tema"));
    mode->addItem(tr("Color propio"));
    mode->setCurrentIndex(m_config->backgroundMode());
    connect(mode, &QComboBox::currentIndexChanged, m_config, &TileConfig::setBackgroundMode);
    form->addRow(tr("Fondo:"), mode);

    auto *colorBtn = new QPushButton(box);
    const auto refreshColor = [this, colorBtn] {
        const QColor c = m_config->backgroundColor();
        colorBtn->setText(c.isValid() ? c.name() : tr("(sin elegir)"));
        QPixmap swatch(20, 20);
        swatch.fill(c.isValid() ? c : palette().window().color());
        colorBtn->setIcon(QIcon(swatch));
    };
    refreshColor();
    connect(colorBtn, &QPushButton::clicked, this, [this, refreshColor] {
        const QColor c = QColorDialog::getColor(m_config->backgroundColor(), this,
                                                tr("Color de fondo del menú"));
        if (c.isValid()) {
            m_config->setBackgroundColor(c);
            refreshColor();
        }
    });
    form->addRow(tr("Color de fondo:"), colorBtn);
    const auto syncColorEnabled = [mode, colorBtn] { colorBtn->setEnabled(mode->currentIndex() == 1); };
    syncColorEnabled();
    connect(mode, &QComboBox::currentIndexChanged, box, syncColorEnabled);

    auto *opacity = new QDoubleSpinBox(box);
    opacity->setRange(0.10, 1.0);
    opacity->setSingleStep(0.05);
    opacity->setDecimals(2);
    opacity->setValue(m_config->backgroundOpacity());
    connect(opacity, &QDoubleSpinBox::valueChanged, m_config, &TileConfig::setBackgroundOpacity);
    form->addRow(tr("Opacidad:"), opacity);

    auto *imageRow = new QWidget(box);
    auto *imageLayout = new QHBoxLayout(imageRow);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    auto *imageBtn = new QPushButton(imageRow);
    auto *imageClear = new QPushButton(tr("Quitar"), imageRow);
    imageLayout->addWidget(imageBtn, 1);
    imageLayout->addWidget(imageClear);
    const auto refreshImage = [this, imageBtn, imageClear] {
        const QString p = m_config->backgroundImage();
        imageBtn->setText(p.isEmpty() ? tr("Elegir imagen…") : p.section(QLatin1Char('/'), -1));
        imageClear->setEnabled(!p.isEmpty());
    };
    refreshImage();
    connect(imageBtn, &QPushButton::clicked, this, [this, refreshImage] {
        const QString p = QFileDialog::getOpenFileName(
            this, tr("Imagen de fondo"), m_config->backgroundImage(),
            tr("Imágenes (*.png *.jpg *.jpeg *.webp *.bmp *.svg);;Todos los archivos (*)"));
        if (!p.isEmpty()) {
            m_config->setBackgroundImage(p);
            refreshImage();
        }
    });
    connect(imageClear, &QPushButton::clicked, this, [this, refreshImage] {
        m_config->setBackgroundImage(QString());
        refreshImage();
    });
    form->addRow(tr("Imagen de fondo:"), imageRow);

    auto *icons = new QCheckBox(tr("Mostrar el ícono de cada aplicación"), box);
    icons->setChecked(m_config->showIcons());
    connect(icons, &QCheckBox::toggled, m_config, &TileConfig::setShowIcons);
    form->addRow(tr("Íconos:"), icons);

    auto *labels = new QCheckBox(tr("Mostrar el nombre de cada aplicación"), box);
    labels->setChecked(m_config->showLabels());
    labels->setToolTip(tr("Los dos son globales; cada mosaico puede llevarle la contra desde "
                          "su menú contextual."));
    connect(labels, &QCheckBox::toggled, m_config, &TileConfig::setShowLabels);
    form->addRow(tr("Nombres:"), labels);

    auto *iconScale = new QSpinBox(box);
    iconScale->setRange(20, 100);
    iconScale->setSuffix(tr(" %"));
    iconScale->setValue(m_config->iconScale());
    iconScale->setToolTip(tr("Porcentaje del lado corto de la celda que ocupa el ícono."));
    connect(iconScale, &QSpinBox::valueChanged, m_config, &TileConfig::setIconScale);
    form->addRow(tr("Escala del ícono:"), iconScale);

    auto *labelPos = new QComboBox(box);
    labelPos->addItem(tr("Debajo del ícono"));
    labelPos->addItem(tr("Al lado (en mosaicos anchos)"));
    labelPos->setCurrentIndex(m_config->labelPosition());
    connect(labelPos, &QComboBox::currentIndexChanged, m_config, &TileConfig::setLabelPosition);
    form->addRow(tr("Posición del nombre:"), labelPos);

    return box;
}

QWidget *TileSettingsDialog::createSidebarGroup()
{
    auto *box = new QGroupBox(tr("Barra lateral"), this);
    auto *form = new QFormLayout(box);

    auto *side = new QComboBox(box);
    side->addItem(tr("A la izquierda"));
    side->addItem(tr("A la derecha"));
    side->addItem(tr("Oculta"));
    side->setCurrentIndex(m_config->sidebar());
    connect(side, &QComboBox::currentIndexChanged, m_config, &TileConfig::setSidebar);
    form->addRow(tr("Posición:"), side);

    auto *width = new QSpinBox(box);
    width->setRange(120, 480);
    width->setSuffix(tr(" px"));
    width->setValue(m_config->sidebarWidth());
    connect(width, &QSpinBox::valueChanged, m_config, &TileConfig::setSidebarWidth);
    form->addRow(tr("Ancho:"), width);
    const auto syncWidth = [side, width] { width->setEnabled(side->currentIndex() != 2); };
    syncWidth();
    connect(side, &QComboBox::currentIndexChanged, box, syncWidth);

    return box;
}

QWidget *TileSettingsDialog::createBehaviorGroup()
{
    auto *box = new QGroupBox(tr("Comportamiento"), this);
    auto *form = new QFormLayout(box);

    const auto addCheck = [this, box, form](const QString &label, const QString &text,
                                            bool checked, void (TileConfig::*setter)(bool),
                                            const QString &tip = QString()) {
        auto *check = new QCheckBox(text, box);
        check->setChecked(checked);
        if (!tip.isEmpty())
            check->setToolTip(tip);
        connect(check, &QCheckBox::toggled, m_config, setter);
        form->addRow(label, check);
        return check;
    };

    addCheck(tr("Buscador:"), tr("Campo de búsqueda arriba"), m_config->showSearch(),
             &TileConfig::setShowSearch);
    addCheck(tr("Apagado:"), tr("Fila de sesión / apagado abajo"), m_config->showPower(),
             &TileConfig::setShowPower);
    addCheck(tr("Índice A-Z:"), tr("Barra de letras al costado"), m_config->showLetterIndex(),
             &TileConfig::setShowLetterIndex,
             tr("Solo aparece en las secciones que no acomodaste a mano: con los mosaicos "
                "puestos por vos, \"saltar a la K\" no significa nada."));
    addCheck(tr("Al lanzar:"), tr("Cerrar el menú al abrir una aplicación"),
             m_config->closeOnLaunch(), &TileConfig::setCloseOnLaunch);
    addCheck(tr("Al perder foco:"), tr("Cerrar el menú cuando pasás a otra ventana"),
             m_config->closeOnFocusLoss(), &TileConfig::setCloseOnFocusLoss);
    addCheck(tr("Mantener abierto:"), tr("No cerrar nunca solo (queda como una ventana más)"),
             m_config->keepOpen(), &TileConfig::setKeepOpen,
             tr("Es el mismo interruptor que el casillero de la esquina del menú."));
    addCheck(tr("Sección:"), tr("Reabrir en la última sección usada"),
             m_config->rememberSection(), &TileConfig::setRememberSection);

    return box;
}

QWidget *TileSettingsDialog::createLayoutGroup()
{
    auto *box = new QGroupBox(tr("Disposición"), this);
    auto *layout = new QVBoxLayout(box);

    auto *info = new QLabel(tr("La disposición de los mosaicos es una sola para toda la sesión: "
                               "la armás una vez y la ves igual desde cualquier dock."),
                            box);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *row = new QHBoxLayout;
    auto *exportBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")),
                                      tr("Exportar…"), box);
    auto *importBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")),
                                      tr("Importar…"), box);
    auto *resetBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")),
                                     tr("Restablecer todo"), box);
    row->addWidget(exportBtn);
    row->addWidget(importBtn);
    row->addWidget(resetBtn);
    row->addStretch();
    layout->addLayout(row);

    connect(exportBtn, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Exportar disposición"),
                                                          QStringLiteral("tilemenu-layout.json"),
                                                          tr("JSON (*.json)"));
        if (path.isEmpty())
            return;
        if (!m_layout->exportToFile(path))
            QMessageBox::warning(this, tr("Exportar disposición"),
                                 tr("No se pudo escribir %1.").arg(path));
    });
    connect(importBtn, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Importar disposición"),
                                                          QString(), tr("JSON (*.json)"));
        if (path.isEmpty())
            return;
        if (!m_layout->importFromFile(path))
            QMessageBox::warning(this, tr("Importar disposición"),
                                 tr("%1 no es una disposición válida.").arg(path));
    });
    connect(resetBtn, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Restablecer todo"),
                                  tr("Se pierde la disposición de todas las secciones y los "
                                     "mosaicos vuelven a acomodarse solos. ¿Seguir?"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            == QMessageBox::Yes) {
            m_layout->resetAll();
        }
    });

    return box;
}
