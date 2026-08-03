// The tile menu's own settings panel (Qt Widgets), opened from the menu itself
// or from kdock's Settings → Menu → "Configurar…".
//
// Same split as the previews binary: kdock only knows whether to show the widget
// and which icon to give it; everything about how the menu looks and behaves is
// configured here, by the process that owns it.

#pragma once

#include <QDialog>

class TileConfig;
class TileLayout;

class TileSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    TileSettingsDialog(TileConfig *config, TileLayout *layout, QWidget *parent = nullptr);

private:
    QWidget *createGridGroup();
    QWidget *createAppearanceGroup();
    QWidget *createSidebarGroup();
    QWidget *createBehaviorGroup();
    QWidget *createLayoutGroup();

    TileConfig *m_config;
    TileLayout *m_layout;
};
