// The control panel's own settings dialog (Qt Widgets), opened from the panel
// itself or from kdock's Settings → Widgets → "Configurar…".
//
// Same split as the other two accessories: kdock only knows whether to show the
// widget and which icon to give it; everything about how the panel looks and
// behaves is configured here, by the process that owns it.

#pragma once

#include <QDialog>

class CmConfig;
class CmLayout;
class QGridLayout;
class QScrollArea;

class CmSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    CmSettingsDialog(CmConfig *config, CmLayout *layout, QWidget *parent = nullptr);

protected:
    // Swallows the wheel on spin boxes and combos that do not have the focus —
    // see the comment where the filter is installed.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *createWindowGroup();
    QWidget *createAppearanceGroup();
    QWidget *createGridGroup();
    QWidget *createSectionsGroup();
    QWidget *createLayoutGroup();

    // The sections editor is a plain grid of rows, rebuilt on every edit. No
    // QListWidget with setItemWidget(): a row widget swallows the list's own
    // mouse handling, and the fix for that (WA_TransparentForMouseEvents) kills
    // the checkboxes inside it — both traps are documented in CLAUDE.md.
    void rebuildSections();

    CmConfig *m_config;
    CmLayout *m_layout;
    QScrollArea *m_scroll = nullptr;
    QGridLayout *m_sectionsGrid = nullptr;
    QWidget *m_sectionsHost = nullptr;
};
