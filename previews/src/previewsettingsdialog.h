// The previews' own administration panel, opened by `kdock-previews --settings`
// or by kdock's "Configurar" button. A plain Qt Widgets dialog shown as a normal
// xdg-shell window (the layer-shell integration only claims windows that ask for
// it), same approach as kdock's SettingsDialog.
//
// One monitor at a time: the selector at the top picks the strip, everything
// below edits that strip's PreviewConfig live.

#pragma once

#include <QDialog>
#include <QString>

class PreviewConfig;
class PreviewManager;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QVBoxLayout;

class PreviewSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreviewSettingsDialog(PreviewManager *manager, QWidget *parent = nullptr);

private:
    void reloadScreens();
    // Point every control at the selected monitor's config and refresh them.
    void selectScreen(const QString &screenName);
    void buildControls();
    // The refresh intervals only mean something in Periodic mode.
    void updateThumbControls();
    // The strip's cross axis is a height or a width depending on the edge.
    void updateThicknessLabel();
    void updateColorButton();
    void pickColor();

    PreviewManager *m_manager;
    PreviewConfig *m_config = nullptr; // the strip being edited
    QString m_screenName;

    QComboBox *m_screenSelector = nullptr;
    QCheckBox *m_masterEnabled = nullptr; // same switch as kdock's checkbox
    QCheckBox *m_enabled = nullptr;       // this monitor

    QWidget *m_controls = nullptr;
    QVBoxLayout *m_controlsLayout = nullptr;

    QComboBox *m_edge = nullptr;
    QComboBox *m_alignment = nullptr;
    QSpinBox *m_thickness = nullptr;
    QSpinBox *m_length = nullptr;
    QSpinBox *m_margin = nullptr;
    QSlider *m_opacity = nullptr;
    QPushButton *m_colorButton = nullptr;
    QPushButton *m_colorReset = nullptr;
    QCheckBox *m_reserveSpace = nullptr;
    QCheckBox *m_autohide = nullptr;
    QCheckBox *m_showTitles = nullptr;
    QCheckBox *m_showScrollBar = nullptr;
    QSpinBox *m_cardSpacing = nullptr;
    QCheckBox *m_autoFit = nullptr;
    QSpinBox *m_fitMin = nullptr;
    QComboBox *m_captureMode = nullptr;
    QSpinBox *m_refresh = nullptr;
    QSpinBox *m_activeRefresh = nullptr;
    QCheckBox *m_includeMinimized = nullptr;
    QCheckBox *m_currentDesktopOnly = nullptr;
    QCheckBox *m_thisMonitorOnly = nullptr;
    QLabel *m_alignmentNote = nullptr;
    QLabel *m_thicknessLabel = nullptr;
};
