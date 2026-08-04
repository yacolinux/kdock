// The tile menu's own settings panel (Qt Widgets), opened from the menu itself
// or from kdock's Settings → Menu → "Configurar…".
//
// Same split as the previews binary: kdock only knows whether to show the widget
// and which icon to give it; everything about how the menu looks and behaves is
// configured here, by the process that owns it.

#pragma once

#include <QDialog>

class AppMenu;
class QComboBox;
class QScrollArea;
class QListWidget;
class TileConfig;
class TileLayout;

class TileSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    TileSettingsDialog(TileConfig *config, TileLayout *layout, AppMenu *menu,
                       QWidget *parent = nullptr);

protected:
    // Swallows the wheel on spin boxes and combos that do not have the focus —
    // see the comment where the filter is installed.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *createGridGroup();
    QWidget *createAppearanceGroup();
    QWidget *createSidebarGroup();
    QWidget *createBehaviorGroup();
    QWidget *createGroupsGroup();
    QWidget *createLayoutGroup();

    // Key of the section the groups editor is pointed at.
    QString currentGroupSection() const;
    // Rebuild the list and put the selection back: the list is rebuilt after
    // every edit, and without this the second click on "Bajar" does nothing
    // (same trap the dock's Layout tab hit).
    void reloadGroups(int selectRow = -1);

    TileConfig *m_config;
    TileLayout *m_layout;
    AppMenu *m_menu;
    QScrollArea *m_scroll = nullptr;
    QComboBox *m_groupSection = nullptr;
    QListWidget *m_groupList = nullptr;
};
