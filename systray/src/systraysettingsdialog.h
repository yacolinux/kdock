// Settings for the tray window: where it anchors, how big it is, and how the
// icons are laid out. Qt Widgets, like every other accessory's dialog.

#pragma once

#include <QDialog>

class SystrayConfig;

class SystraySettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SystraySettingsDialog(SystrayConfig *config, QWidget *parent = nullptr);

private:
    SystrayConfig *m_config;
};
