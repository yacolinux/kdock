// Persistent settings of the system-tray window.
//
// Storage is ~/.local/share/kdock/systray.conf, next to controlmanager.conf and
// the other accessories'. There is one instance of this process for the whole
// session, so there is nothing per-monitor here: the tray window the user sizes
// is the one every dock opens.
//
// Two notifiers, same split as CmConfig: settingsChanged() repaints and
// windowChanged() re-commits the layer-shell surface (anchors, size, margins).

#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

class SystrayConfig : public QObject
{
    Q_OBJECT
    // --- window ---
    // 0 top, 1 bottom, 2 left, 3 right — the screen edge the window hangs from.
    Q_PROPERTY(int edge READ edge WRITE setEdge NOTIFY windowChanged)
    // 0 start, 1 center, 2 end, along that edge.
    Q_PROPERTY(int alignment READ alignment WRITE setAlignment NOTIFY windowChanged)
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY windowChanged)
    Q_PROPERTY(int windowHeight READ windowHeight WRITE setWindowHeight NOTIFY windowChanged)
    // 0 = use the pixel value; 1..100 = that percentage of the screen.
    Q_PROPERTY(int windowWidthPercent READ windowWidthPercent WRITE setWindowWidthPercent NOTIFY windowChanged)
    Q_PROPERTY(int windowHeightPercent READ windowHeightPercent WRITE setWindowHeightPercent NOTIFY windowChanged)
    Q_PROPERTY(int screenMargin READ screenMargin WRITE setScreenMargin NOTIFY windowChanged)
    // "Ventana permanente": none of the automatic close paths fire.
    Q_PROPERTY(bool keepOpen READ keepOpen WRITE setKeepOpen NOTIFY settingsChanged)
    Q_PROPERTY(bool closeOnFocusLoss READ closeOnFocusLoss WRITE setCloseOnFocusLoss NOTIFY settingsChanged)

    // --- appearance ---
    Q_PROPERTY(int iconSize READ iconSize WRITE setIconSize NOTIFY settingsChanged)
    Q_PROPERTY(int iconSpacing READ iconSpacing WRITE setIconSpacing NOTIFY settingsChanged)
    // 0 = flow icons to fill the window; >0 = fixed number of columns/rows.
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY settingsChanged)
    Q_PROPERTY(qreal backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY settingsChanged)
    Q_PROPERTY(int cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY settingsChanged)
    Q_PROPERTY(bool showTooltips READ showTooltips WRITE setShowTooltips NOTIFY settingsChanged)

public:
    enum Edge { Top = 0, Bottom = 1, Left = 2, Right = 3 };
    Q_ENUM(Edge)
    enum Alignment { Start = 0, Center = 1, End = 2 };
    Q_ENUM(Alignment)

    // Same directory as kdock's own settings; "preload" lives here too so kdock
    // can read it without starting this process.
    static QString settingsFilePath();

    explicit SystrayConfig(QObject *parent = nullptr);

    int edge() const { return m_edge; }
    int alignment() const { return m_alignment; }
    int windowWidth() const { return m_windowWidth; }
    int windowHeight() const { return m_windowHeight; }
    int windowWidthPercent() const { return m_windowWidthPercent; }
    int windowHeightPercent() const { return m_windowHeightPercent; }
    int screenMargin() const { return m_screenMargin; }
    bool keepOpen() const { return m_keepOpen; }
    bool closeOnFocusLoss() const { return m_closeOnFocusLoss; }

    int iconSize() const { return m_iconSize; }
    int iconSpacing() const { return m_iconSpacing; }
    int columns() const { return m_columns; }
    qreal backgroundOpacity() const { return m_backgroundOpacity; }
    int cornerRadius() const { return m_cornerRadius; }
    bool showTooltips() const { return m_showTooltips; }

    // Services (SNI bus names) the user chose to hide from the tray.
    QStringList hiddenItems() const { return m_hiddenItems; }
    void setHiddenItems(const QStringList &items);

    // Resolved pixel size for a screen of this size (percentage wins when set).
    Q_INVOKABLE int windowWidthFor(int screenWidth) const;
    Q_INVOKABLE int windowHeightFor(int screenHeight) const;

    void setEdge(int edge);
    void setAlignment(int alignment);
    void setWindowWidth(int px);
    void setWindowHeight(int px);
    void setWindowWidthPercent(int percent);
    void setWindowHeightPercent(int percent);
    void setScreenMargin(int px);
    void setKeepOpen(bool on);
    void setCloseOnFocusLoss(bool on);
    void setIconSize(int px);
    void setIconSpacing(int px);
    void setColumns(int columns);
    void setBackgroundOpacity(qreal opacity);
    void setCornerRadius(int px);
    void setShowTooltips(bool on);

    // Re-reads the file after somebody else wrote it (kdock's launcher edits the
    // window size), then repaints. Not a watcher: this process rewrites the file
    // on every resize, so a watcher would fire constantly.
    void reloadFromDisk();

    // Whether kdock should start this process along with itself. Static because
    // kdock's launcher reads the same key without ever constructing a
    // SystrayConfig. The tray host must be resident to keep collecting items, so
    // this defaults to true (unlike the control panel's preload).
    static bool preload();
    static void setPreload(bool on);

signals:
    void settingsChanged();
    void windowChanged();
    void hiddenItemsChanged();

private:
    void load();
    void store(const QString &key, const QVariant &value);
    void storeWindow(const QString &key, const QVariant &value);

    QSettings m_settings;

    int m_edge = SystrayConfig::Bottom;
    int m_alignment = SystrayConfig::Center;
    int m_windowWidth = 320;
    int m_windowHeight = 96;
    int m_windowWidthPercent = 0;
    int m_windowHeightPercent = 0;
    int m_screenMargin = 8;
    bool m_keepOpen = false;
    bool m_closeOnFocusLoss = true;

    int m_iconSize = 24;
    int m_iconSpacing = 8;
    int m_columns = 0; // 0 = flow to fill
    qreal m_backgroundOpacity = 0.94;
    int m_cornerRadius = 12;
    bool m_showTooltips = true;

    QStringList m_hiddenItems;
};
