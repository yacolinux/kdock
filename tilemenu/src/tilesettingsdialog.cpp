#include "tilesettingsdialog.h"

#include "tileconfig.h"
#include "tilelayout.h"

#include "appmenu.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QCoreApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QWheelEvent>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

TileSettingsDialog::TileSettingsDialog(TileConfig *config, TileLayout *layout, AppMenu *menu,
                                       QWidget *parent)
    : QDialog(parent)
    , m_config(config)
    , m_layout(layout)
    , m_menu(menu)
{
    setWindowTitle(tr("Menú de mosaicos"));

    auto *content = new QWidget;
    auto *inner = new QVBoxLayout(content);
    inner->addWidget(createGridGroup());
    inner->addWidget(createAppearanceGroup());
    inner->addWidget(createSidebarGroup());
    inner->addWidget(createBehaviorGroup());
    inner->addWidget(createGroupsGroup());
    inner->addWidget(createLayoutGroup());
    inner->addStretch();

    // The panel is taller than a small screen, same reason SettingsDialog wraps
    // each of its tabs in one of these.
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    // Never scroll sideways: the widest row is a checkbox label, and a
    // horizontal scrollbar just hides the end of it.
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
    // Reaching the Grupos box at the bottom would silently change the settings
    // it passed over on the way (measured: two turns of the wheel flipped
    // "Fondo" to "Color propio"). Only widgets that were clicked into keep the
    // wheel; for the rest it goes to the scroll area.
    for (QWidget *w : content->findChildren<QWidget *>()) {
        if (qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QComboBox *>(w)) {
            w->setFocusPolicy(Qt::StrongFocus);
            w->installEventFilter(this);
        }
    }

    // Wide enough for the longest label, and clamped to the screen so the Close
    // button stays reachable on a small one (same reasoning as SettingsDialog).
    const int wanted = content->sizeHint().width() + 60;
    const QRect avail = screen() ? screen()->availableGeometry() : QRect(0, 0, 1280, 800);
    resize(qMin(qMax(620, wanted), avail.width() - 80),
           qMin(720, avail.height() - 80));
}

bool TileSettingsDialog::eventFilter(QObject *watched, QEvent *event)
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

    auto *bold = new QCheckBox(tr("Nombres en negrita"), box);
    bold->setChecked(m_config->labelBold());
    connect(bold, &QCheckBox::toggled, m_config, &TileConfig::setLabelBold);
    form->addRow(tr("Negrita:"), bold);

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

QString TileSettingsDialog::currentGroupSection() const
{
    return m_groupSection ? m_groupSection->currentData().toString() : QString();
}

void TileSettingsDialog::reloadGroups(int selectRow)
{
    if (!m_groupList)
        return;
    const QString section = currentGroupSection();
    const QVariantList tabs = m_layout->groups(section);

    QSignalBlocker blocker(m_groupList);
    m_groupList->clear();
    for (const QVariant &v : tabs) {
        const QVariantMap tab = v.toMap();
        const QString title = tab.value(QStringLiteral("title")).toString();
        m_groupList->addItem(tr("%1  —  %2 mosaico(s)")
                                 .arg(title.isEmpty()
                                          ? tr("Grupo %1").arg(m_groupList->count() + 1)
                                          : title)
                                 .arg(tab.value(QStringLiteral("tiles")).toInt()));
    }
    if (selectRow >= 0 && selectRow < m_groupList->count())
        m_groupList->setCurrentRow(selectRow);
}

QWidget *TileSettingsDialog::createGroupsGroup()
{
    auto *box = new QGroupBox(tr("Grupos"), this);
    auto *layout = new QVBoxLayout(box);

    auto *info = new QLabel(
        tr("Los grupos agrupan mosaicos dentro de una sección: son propios de cada "
           "sección del menú, y se muestran como solapas. Acá se crean, se renombran y se "
           "ordenan; para pasar un mosaico de un grupo a otro, arrastralo sobre la solapa "
           "destino o usá su menú contextual → «Mover a grupo»."),
        box);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *form = new QFormLayout;
    m_groupSection = new QComboBox(box);
    // Every section, not just the arranged ones: picking an untouched one and
    // naming its first group is a perfectly good way to start.
    const QVariantList sections = m_menu ? m_menu->sections() : QVariantList();
    for (const QVariant &v : sections) {
        const QVariantMap s = v.toMap();
        const int depth = s.value(QStringLiteral("depth")).toInt();
        m_groupSection->addItem(QString(depth * 4, QLatin1Char(' '))
                                    + s.value(QStringLiteral("label")).toString(),
                                s.value(QStringLiteral("key")));
    }
    form->addRow(tr("Sección:"), m_groupSection);

    auto *tabsPos = new QComboBox(box);
    tabsPos->addItem(tr("Arriba"));
    tabsPos->addItem(tr("Abajo"));
    tabsPos->addItem(tr("A la izquierda"));
    tabsPos->addItem(tr("A la derecha"));
    tabsPos->setCurrentIndex(m_config->groupTabs());
    tabsPos->setToolTip(tr("La barra aparece solo cuando la sección tiene más de un grupo."));
    connect(tabsPos, &QComboBox::currentIndexChanged, m_config, &TileConfig::setGroupTabs);
    form->addRow(tr("Solapas:"), tabsPos);
    layout->addLayout(form);

    m_groupList = new QListWidget(box);
    m_groupList->setMaximumHeight(150);
    layout->addWidget(m_groupList);

    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Agregar…"), box);
    auto *rename = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-rename")),
                                   tr("Renombrar…"), box);
    auto *remove = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), tr("Quitar"),
                                   box);
    auto *up = new QPushButton(QIcon::fromTheme(QStringLiteral("go-up")), tr("Subir"), box);
    auto *down = new QPushButton(QIcon::fromTheme(QStringLiteral("go-down")), tr("Bajar"), box);
    row->addWidget(add);
    row->addWidget(rename);
    row->addWidget(remove);
    row->addStretch();
    row->addWidget(up);
    row->addWidget(down);
    layout->addLayout(row);

    const auto syncButtons = [this, rename, remove, up, down] {
        const int i = m_groupList->currentRow();
        const int n = m_groupList->count();
        rename->setEnabled(i >= 0);
        // The last band has to stay: its tiles would have nowhere to live.
        remove->setEnabled(i >= 0 && n > 1);
        up->setEnabled(i > 0);
        down->setEnabled(i >= 0 && i < n - 1);
    };

    // Select the first band on every section change: leaving nothing selected
    // means every button below is dead until the user thinks to click a row.
    connect(m_groupSection, &QComboBox::currentIndexChanged, this, [this, syncButtons] {
        reloadGroups(0);
        syncButtons();
    });
    connect(m_groupList, &QListWidget::currentRowChanged, box, syncButtons);

    connect(add, &QPushButton::clicked, this, [this, syncButtons] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Nuevo grupo"), tr("Nombre:"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (!ok)
            return;
        const int i = m_layout->addGroup(currentGroupSection(), name);
        reloadGroups(i);
        syncButtons();
    });
    connect(rename, &QPushButton::clicked, this, [this, syncButtons] {
        const int i = m_groupList->currentRow();
        if (i < 0)
            return;
        const QString section = currentGroupSection();
        const QVariantList tabs = m_layout->groups(section);
        const QString current = i < tabs.size()
                                    ? tabs.at(i).toMap().value(QStringLiteral("title")).toString()
                                    : QString();
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Renombrar grupo"), tr("Nombre:"),
                                                   QLineEdit::Normal, current, &ok);
        if (!ok)
            return;
        m_layout->renameGroup(section, i, name);
        reloadGroups(i);
        syncButtons();
    });
    connect(remove, &QPushButton::clicked, this, [this, syncButtons] {
        const int i = m_groupList->currentRow();
        if (i < 0)
            return;
        if (QMessageBox::question(this, tr("Quitar grupo"),
                                  tr("Los mosaicos de este grupo pasan al grupo vecino. "
                                     "¿Seguir?"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
            return;
        m_layout->removeGroup(currentGroupSection(), i);
        reloadGroups(qMax(0, i - 1));
        syncButtons();
    });
    connect(up, &QPushButton::clicked, this, [this, syncButtons] {
        const int i = m_groupList->currentRow();
        if (i <= 0)
            return;
        m_layout->moveGroup(currentGroupSection(), i, i - 1);
        reloadGroups(i - 1);
        syncButtons();
    });
    connect(down, &QPushButton::clicked, this, [this, syncButtons] {
        const int i = m_groupList->currentRow();
        if (i < 0 || i >= m_groupList->count() - 1)
            return;
        m_layout->moveGroup(currentGroupSection(), i, i + 1);
        reloadGroups(i + 1);
        syncButtons();
    });

    // Keep the list honest when the tabs are edited from the menu instead.
    connect(m_layout, &TileLayout::changed, this, [this, syncButtons](const QString &section) {
        if (section.isEmpty() || section == currentGroupSection()) {
            const int keep = qMax(0, m_groupList->currentRow());
            reloadGroups(keep);
            syncButtons();
        }
    });

    // Start on whatever section the menu is showing, which is the one the user
    // was just looking at.
    const int idx = m_groupSection->findData(m_config->lastSection());
    if (idx >= 0)
        m_groupSection->setCurrentIndex(idx);
    reloadGroups(0);
    syncButtons();

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
