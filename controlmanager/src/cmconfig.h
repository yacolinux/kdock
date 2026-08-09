// Persistent settings of the control panel, plus the card layout blob.
//
// Storage is ~/.local/share/kdock/controlmanager.conf, next to previews.conf and
// tilemenu.conf. There is one instance of this process for the whole session, so
// like TileConfig there is nothing per-monitor here: the panel the user arranges
// is the one every dock opens.
//
// Three notifiers instead of TileConfig's one, because they have three different
// costs: settingsChanged() repaints, windowChanged() re-commits the layer-shell
// surface (anchors, size, margins), and sectionsChanged() rebuilds the tab bar
// and the Principal grid.

#pragma once

#include <QColor>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QUrl>

class CmConfig : public QObject
{
    Q_OBJECT
    // --- window ---
    // 0 top, 1 bottom, 2 left, 3 right — the screen edge the panel hangs from.
    Q_PROPERTY(int edge READ edge WRITE setEdge NOTIFY windowChanged)
    // 0 start, 1 center, 2 end, along that edge.
    Q_PROPERTY(int alignment READ alignment WRITE setAlignment NOTIFY windowChanged)
    Q_PROPERTY(int panelWidth READ panelWidth WRITE setPanelWidth NOTIFY windowChanged)
    Q_PROPERTY(int panelHeight READ panelHeight WRITE setPanelHeight NOTIFY windowChanged)
    // 0 = use the pixel value above; 1..100 = that percentage of the screen.
    Q_PROPERTY(int panelWidthPercent READ panelWidthPercent WRITE setPanelWidthPercent NOTIFY windowChanged)
    Q_PROPERTY(int panelHeightPercent READ panelHeightPercent WRITE setPanelHeightPercent NOTIFY windowChanged)
    Q_PROPERTY(int screenMargin READ screenMargin WRITE setScreenMargin NOTIFY windowChanged)
    // "Ventana permanente": none of the automatic close paths fire.
    Q_PROPERTY(bool keepOpen READ keepOpen WRITE setKeepOpen NOTIFY settingsChanged)
    Q_PROPERTY(bool closeOnFocusLoss READ closeOnFocusLoss WRITE setCloseOnFocusLoss NOTIFY settingsChanged)
    Q_PROPERTY(bool closeOnLeave READ closeOnLeave WRITE setCloseOnLeave NOTIFY settingsChanged)

    // --- appearance ---
    Q_PROPERTY(int backgroundMode READ backgroundMode WRITE setBackgroundMode NOTIFY settingsChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY settingsChanged)
    // "Own color" with nothing picked yet must not paint the panel black.
    Q_PROPERTY(bool backgroundColorSet READ backgroundColorSet NOTIFY settingsChanged)
    Q_PROPERTY(qreal backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY settingsChanged)
    Q_PROPERTY(QString backgroundImage READ backgroundImage WRITE setBackgroundImage NOTIFY settingsChanged)
    Q_PROPERTY(QUrl backgroundImageUrl READ backgroundImageUrl NOTIFY settingsChanged)
    Q_PROPERTY(int cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY settingsChanged)
    Q_PROPERTY(bool labelBold READ labelBold WRITE setLabelBold NOTIFY settingsChanged)
    // Minimum size of the CmButtons inside the tabs, in px. 0 = the button's
    // own natural size (the historic behaviour). A button never draws smaller
    // than these even when the configured one is bigger than the natural one.
    Q_PROPERTY(int buttonWidth READ buttonWidth WRITE setButtonWidth NOTIFY settingsChanged)
    Q_PROPERTY(int buttonHeight READ buttonHeight WRITE setButtonHeight NOTIFY settingsChanged)
    // Every text of the panel, scaled by this factor. 0 = the historic fixed
    // sizes, which is what fontScale() reports as 1.0.
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY settingsChanged)
    Q_PROPERTY(qreal fontScale READ fontScale NOTIFY settingsChanged)
    // Icon set for every icon of the panel (tabs, cards, buttons). Empty =
    // follow the desktop's icon theme, adapted to the panel background (the
    // luminance test of CmWindow::iconSuffix).
    Q_PROPERTY(QString iconTheme READ iconTheme WRITE setIconTheme NOTIFY settingsChanged)
    // 0 tabs on top, 1 tabs at the bottom.
    Q_PROPERTY(int tabsPosition READ tabsPosition WRITE setTabsPosition NOTIFY settingsChanged)
    Q_PROPERTY(bool showTabIcons READ showTabIcons WRITE setShowTabIcons NOTIFY settingsChanged)
    Q_PROPERTY(bool showCardTitles READ showCardTitles WRITE setShowCardTitles NOTIFY settingsChanged)
    Q_PROPERTY(QStringList presetColors READ presetColors NOTIFY settingsChanged)

    // --- grid (the Principal tab) ---
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY settingsChanged)
    Q_PROPERTY(int cellSize READ cellSize WRITE setCellSize NOTIFY settingsChanged)
    Q_PROPERTY(bool cellStretch READ cellStretch WRITE setCellStretch NOTIFY settingsChanged)
    Q_PROPERTY(int cellMin READ cellMin WRITE setCellMin NOTIFY settingsChanged)
    Q_PROPERTY(int cellMax READ cellMax WRITE setCellMax NOTIFY settingsChanged)
    Q_PROPERTY(int cellSpacing READ cellSpacing WRITE setCellSpacing NOTIFY settingsChanged)

    // --- sections ---
    Q_PROPERTY(QStringList sectionOrder READ sectionOrder NOTIFY sectionsChanged)
    Q_PROPERTY(QStringList enabledSections READ enabledSections NOTIFY sectionsChanged)
    Q_PROPERTY(QStringList principalCards READ principalCards NOTIFY sectionsChanged)
    Q_PROPERTY(bool rememberTab READ rememberTab WRITE setRememberTab NOTIFY settingsChanged)
    Q_PROPERTY(QString lastTab READ lastTab WRITE setLastTab NOTIFY settingsChanged)

    // --- behaviour of individual sections ---
    Q_PROPERTY(QString wallpaperScript READ wallpaperScript WRITE setWallpaperScript NOTIFY settingsChanged)

public:
    enum Edge { Top = 0, Bottom = 1, Left = 2, Right = 3 };
    Q_ENUM(Edge)
    enum Alignment { Start = 0, Center = 1, End = 2 };
    Q_ENUM(Alignment)

    // Same directory as kdock's own settings; "preload" lives here too so kdock
    // can read it without starting this process.
    static QString settingsFilePath();

    explicit CmConfig(QObject *parent = nullptr);

    int edge() const { return m_edge; }
    int alignment() const { return m_alignment; }
    int panelWidth() const { return m_panelWidth; }
    int panelHeight() const { return m_panelHeight; }
    int panelWidthPercent() const { return m_panelWidthPercent; }
    int panelHeightPercent() const { return m_panelHeightPercent; }
    int screenMargin() const { return m_screenMargin; }
    bool keepOpen() const { return m_keepOpen; }
    bool closeOnFocusLoss() const { return m_closeOnFocusLoss; }
    bool closeOnLeave() const { return m_closeOnLeave; }

    int backgroundMode() const { return m_backgroundMode; }
    QColor backgroundColor() const { return m_backgroundColor; }
    bool backgroundColorSet() const { return m_backgroundColor.isValid(); }
    qreal backgroundOpacity() const { return m_backgroundOpacity; }
    QString backgroundImage() const { return m_backgroundImage; }
    QUrl backgroundImageUrl() const;
    int cornerRadius() const { return m_cornerRadius; }
    bool labelBold() const { return m_labelBold; }
    int buttonWidth() const { return m_buttonWidth; }
    int buttonHeight() const { return m_buttonHeight; }
    int fontSize() const { return m_fontSize; }
    qreal fontScale() const { return m_fontSize > 0 ? m_fontSize / 12.0 : 1.0; }
    QString iconTheme() const { return m_iconTheme; }
    int tabsPosition() const { return m_tabsPosition; }
    bool showTabIcons() const { return m_showTabIcons; }
    bool showCardTitles() const { return m_showCardTitles; }
    // The eight quick colors the dock offers in its right-click submenu, read
    // from kdock's shared settings file so the card color menu offers the same
    // palette. Read-only here: they are edited from the dock's settings dialog.
    QStringList presetColors() const;

    int columns() const { return m_columns; }
    int cellSize() const { return m_cellSize; }
    bool cellStretch() const { return m_cellStretch; }
    int cellMin() const { return m_cellMin; }
    int cellMax() const { return m_cellMax; }
    int cellSpacing() const { return m_cellSpacing; }

    QStringList sectionOrder() const { return m_sectionOrder; }
    QStringList enabledSections() const { return m_enabledSections; }
    QStringList principalCards() const { return m_principalCards; }
    bool rememberTab() const { return m_rememberTab; }
    QString lastTab() const { return m_lastTab; }
    QString wallpaperScript() const { return m_wallpaperScript; }

    // Resolved pixel size for a screen of this size (percentage wins when set).
    Q_INVOKABLE int panelWidthFor(int screenWidth) const;
    Q_INVOKABLE int panelHeightFor(int screenHeight) const;

    Q_INVOKABLE bool sectionEnabled(const QString &id) const;
    Q_INVOKABLE bool cardEnabled(const QString &id) const;
    Q_INVOKABLE void setSectionEnabled(const QString &id, bool on);
    Q_INVOKABLE void setCardEnabled(const QString &id, bool on);
    Q_INVOKABLE void moveSection(int from, int to);
    // Tab ids in display order: the enabled sections that carry a tab. The
    // Principal tab is not in here — the QML puts it first, always.
    Q_INVOKABLE QStringList visibleTabs() const;

    void setEdge(int edge);
    void setAlignment(int alignment);
    void setPanelWidth(int px);
    void setPanelHeight(int px);
    void setPanelWidthPercent(int percent);
    void setPanelHeightPercent(int percent);
    void setScreenMargin(int px);
    void setKeepOpen(bool on);
    void setCloseOnFocusLoss(bool on);
    void setCloseOnLeave(bool on);
    void setBackgroundMode(int mode);
    void setBackgroundColor(const QColor &color);
    void setBackgroundOpacity(qreal opacity);
    void setBackgroundImage(const QString &path);
    void setCornerRadius(int px);
    void setLabelBold(bool on);
    void setButtonWidth(int px);
    void setButtonHeight(int px);
    void setFontSize(int px);
    void setIconTheme(const QString &id);
    void setTabsPosition(int position);
    void setShowTabIcons(bool on);
    void setShowCardTitles(bool on);
    void setColumns(int columns);
    void setCellSize(int px);
    void setCellStretch(bool on);
    void setCellMin(int px);
    void setCellMax(int px);
    void setCellSpacing(int px);
    void setRememberTab(bool on);
    void setLastTab(const QString &tab);
    void setWallpaperScript(const QString &path);

    // Whether kdock should start this process along with itself. Static because
    // kdock's launcher reads the same key without ever constructing a CmConfig.
    static bool preload();
    static void setPreload(bool on);

    // The whole card layout as a compact JSON document, same reasoning as
    // TileConfig: card ids are safe, but one blob keeps the .conf readable.
    QString layoutJson() const;
    void setLayoutJson(const QString &json);

signals:
    void settingsChanged();
    void windowChanged();
    void sectionsChanged();

private:
    void load();
    // Drops unknown ids and appends sections added by a newer version, so a
    // .conf written before a section existed picks it up instead of hiding it
    // forever (the same reconciliation DockConfig does for widget tokens).
    void reconcileSections();
    void store(const QString &key, const QVariant &value);
    void storeWindow(const QString &key, const QVariant &value);
    void storeSections();

    QSettings m_settings;

    int m_edge = CmConfig::Top;
    int m_alignment = CmConfig::Center;
    int m_panelWidth = 900;
    int m_panelHeight = 420;
    int m_panelWidthPercent = 0;
    int m_panelHeightPercent = 0;
    int m_screenMargin = 8;
    bool m_keepOpen = false;
    bool m_closeOnFocusLoss = true;
    bool m_closeOnLeave = false;

    int m_backgroundMode = 0;
    QColor m_backgroundColor;
    qreal m_backgroundOpacity = 0.94;
    QString m_backgroundImage;
    int m_cornerRadius = 12;
    bool m_labelBold = true;
    int m_buttonWidth = 0;  // 0 = natural size (historic behaviour)
    int m_buttonHeight = 0;
    int m_fontSize = 0;     // 0 = the historic fixed sizes (fontScale 1.0)
    QString m_iconTheme;    // empty = follow the desktop's theme
    int m_tabsPosition = 0;
    bool m_showTabIcons = true;
    bool m_showCardTitles = true;

    int m_columns = 6;
    int m_cellSize = 96;
    bool m_cellStretch = true;
    int m_cellMin = 48;
    int m_cellMax = 220;
    int m_cellSpacing = 8;

    QStringList m_sectionOrder;
    QStringList m_enabledSections;
    QStringList m_principalCards;
    bool m_rememberTab = true;
    QString m_lastTab;
    QString m_wallpaperScript = QStringLiteral("/usr/local/bin/next-wall.sh");
};
