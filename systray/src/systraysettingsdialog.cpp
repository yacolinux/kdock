#include "systraysettingsdialog.h"

#include "systrayconfig.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SystraySettingsDialog::SystraySettingsDialog(SystrayConfig *config, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
{
    setWindowTitle(tr("Bandeja del sistema"));

    auto *root = new QVBoxLayout(this);

    // --- Window ---
    auto *winBox = new QGroupBox(tr("Ventana"), this);
    auto *winForm = new QFormLayout(winBox);

    auto *edge = new QComboBox(winBox);
    edge->addItems({tr("Arriba"), tr("Abajo"), tr("Izquierda"), tr("Derecha")});
    edge->setCurrentIndex(m_config->edge());
    connect(edge, &QComboBox::currentIndexChanged, m_config, &SystrayConfig::setEdge);
    winForm->addRow(tr("Borde:"), edge);

    auto *align = new QComboBox(winBox);
    align->addItems({tr("Inicio"), tr("Centro"), tr("Fin")});
    align->setCurrentIndex(m_config->alignment());
    connect(align, &QComboBox::currentIndexChanged, m_config, &SystrayConfig::setAlignment);
    winForm->addRow(tr("Alineación:"), align);

    auto *w = new QSpinBox(winBox);
    w->setRange(120, 4000);
    w->setSuffix(QStringLiteral(" px"));
    w->setValue(m_config->windowWidth());
    connect(w, &QSpinBox::valueChanged, m_config, &SystrayConfig::setWindowWidth);
    winForm->addRow(tr("Ancho:"), w);

    auto *h = new QSpinBox(winBox);
    h->setRange(60, 4000);
    h->setSuffix(QStringLiteral(" px"));
    h->setValue(m_config->windowHeight());
    connect(h, &QSpinBox::valueChanged, m_config, &SystrayConfig::setWindowHeight);
    winForm->addRow(tr("Alto:"), h);

    auto *margin = new QSpinBox(winBox);
    margin->setRange(0, 400);
    margin->setSuffix(QStringLiteral(" px"));
    margin->setValue(m_config->screenMargin());
    connect(margin, &QSpinBox::valueChanged, m_config, &SystrayConfig::setScreenMargin);
    winForm->addRow(tr("Margen:"), margin);

    auto *closeFocus = new QCheckBox(tr("Cerrar al perder el foco"), winBox);
    closeFocus->setChecked(m_config->closeOnFocusLoss());
    connect(closeFocus, &QCheckBox::toggled, m_config, &SystrayConfig::setCloseOnFocusLoss);
    winForm->addRow(QString(), closeFocus);

    auto *keepOpen = new QCheckBox(tr("Ventana permanente"), winBox);
    keepOpen->setChecked(m_config->keepOpen());
    connect(keepOpen, &QCheckBox::toggled, m_config, &SystrayConfig::setKeepOpen);
    winForm->addRow(QString(), keepOpen);

    root->addWidget(winBox);

    // --- Icons ---
    auto *iconBox = new QGroupBox(tr("Íconos"), this);
    auto *iconForm = new QFormLayout(iconBox);

    auto *iconSize = new QSpinBox(iconBox);
    iconSize->setRange(12, 128);
    iconSize->setSuffix(QStringLiteral(" px"));
    iconSize->setValue(m_config->iconSize());
    connect(iconSize, &QSpinBox::valueChanged, m_config, &SystrayConfig::setIconSize);
    iconForm->addRow(tr("Tamaño:"), iconSize);

    auto *iconSpacing = new QSpinBox(iconBox);
    iconSpacing->setRange(0, 64);
    iconSpacing->setSuffix(QStringLiteral(" px"));
    iconSpacing->setValue(m_config->iconSpacing());
    connect(iconSpacing, &QSpinBox::valueChanged, m_config, &SystrayConfig::setIconSpacing);
    iconForm->addRow(tr("Separación:"), iconSpacing);

    auto *columns = new QSpinBox(iconBox);
    columns->setRange(0, 30);
    columns->setSpecialValueText(tr("Automático"));
    columns->setValue(m_config->columns());
    connect(columns, &QSpinBox::valueChanged, m_config, &SystrayConfig::setColumns);
    iconForm->addRow(tr("Columnas:"), columns);

    auto *tooltips = new QCheckBox(tr("Mostrar tooltips"), iconBox);
    tooltips->setChecked(m_config->showTooltips());
    connect(tooltips, &QCheckBox::toggled, m_config, &SystrayConfig::setShowTooltips);
    iconForm->addRow(QString(), tooltips);

    root->addWidget(iconBox);

    // --- Appearance ---
    auto *lookBox = new QGroupBox(tr("Apariencia"), this);
    auto *lookForm = new QFormLayout(lookBox);

    auto *opacity = new QDoubleSpinBox(lookBox);
    opacity->setRange(0.0, 1.0);
    opacity->setSingleStep(0.05);
    opacity->setDecimals(2);
    opacity->setValue(m_config->backgroundOpacity());
    connect(opacity, &QDoubleSpinBox::valueChanged, m_config, &SystrayConfig::setBackgroundOpacity);
    lookForm->addRow(tr("Opacidad del fondo:"), opacity);

    auto *radius = new QSpinBox(lookBox);
    radius->setRange(0, 40);
    radius->setSuffix(QStringLiteral(" px"));
    radius->setValue(m_config->cornerRadius());
    connect(radius, &QSpinBox::valueChanged, m_config, &SystrayConfig::setCornerRadius);
    lookForm->addRow(tr("Radio de esquina:"), radius);

    root->addWidget(lookBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    // A QDialogButtonBox standard button ships an untranslated label; every
    // instance needs its own setText(tr(...)) — it is not inherited.
    if (auto *close = buttons->button(QDialogButtonBox::Close))
        close->setText(tr("Cerrar"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::hide);
    root->addWidget(buttons);
}
