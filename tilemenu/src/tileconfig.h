// Persistent settings of the tile menu, plus the tile layout itself.
//
// Storage is ~/.local/share/kdock/tilemenu.conf, next to previews.conf. There is
// exactly one instance of this process for the whole session, so unlike
// DockConfig there is nothing per-monitor here and no shared/per-dock split: the
// layout the user builds is the same one every dock opens.
//
// Every appearance key shares a single settingsChanged() notifier. The menu
// repaints as a whole anyway, and ~20 separate signals for that would be noise.
// The layout has its own, finer-grained signal (see TileLayout).

#pragma once

#include <QColor>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QUrl>

class TileConfig : public QObject
{
    Q_OBJECT
    // --- grid ---
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY settingsChanged)
    Q_PROPERTY(int cellSize READ cellSize WRITE setCellSize NOTIFY settingsChanged)
    Q_PROPERTY(bool cellStretch READ cellStretch WRITE setCellStretch NOTIFY settingsChanged)
    Q_PROPERTY(int cellMin READ cellMin WRITE setCellMin NOTIFY settingsChanged)
    Q_PROPERTY(int cellMax READ cellMax WRITE setCellMax NOTIFY settingsChanged)
    Q_PROPERTY(int cellSpacing READ cellSpacing WRITE setCellSpacing NOTIFY settingsChanged)
    // --- appearance ---
    Q_PROPERTY(int sidebar READ sidebar WRITE setSidebar NOTIFY settingsChanged)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth WRITE setSidebarWidth NOTIFY settingsChanged)
    Q_PROPERTY(bool showIcons READ showIcons WRITE setShowIcons NOTIFY settingsChanged)
    Q_PROPERTY(bool showLabels READ showLabels WRITE setShowLabels NOTIFY settingsChanged)
    Q_PROPERTY(int iconScale READ iconScale WRITE setIconScale NOTIFY settingsChanged)
    Q_PROPERTY(int labelPosition READ labelPosition WRITE setLabelPosition NOTIFY settingsChanged)
    Q_PROPERTY(bool labelBold READ labelBold WRITE setLabelBold NOTIFY settingsChanged)
    Q_PROPERTY(int backgroundMode READ backgroundMode WRITE setBackgroundMode NOTIFY settingsChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY settingsChanged)
    // "Own color" with nothing picked yet must not paint the menu black.
    Q_PROPERTY(bool backgroundColorSet READ backgroundColorSet NOTIFY settingsChanged)
    Q_PROPERTY(qreal backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY settingsChanged)
    Q_PROPERTY(QString backgroundImage READ backgroundImage WRITE setBackgroundImage NOTIFY settingsChanged)
    Q_PROPERTY(QUrl backgroundImageUrl READ backgroundImageUrl NOTIFY settingsChanged)
    Q_PROPERTY(QStringList presetColors READ presetColors NOTIFY settingsChanged)
    // --- behavior ---
    Q_PROPERTY(bool showSearch READ showSearch WRITE setShowSearch NOTIFY settingsChanged)
    Q_PROPERTY(bool showPower READ showPower WRITE setShowPower NOTIFY settingsChanged)
    Q_PROPERTY(bool showLetterIndex READ showLetterIndex WRITE setShowLetterIndex NOTIFY settingsChanged)
    Q_PROPERTY(bool closeOnLaunch READ closeOnLaunch WRITE setCloseOnLaunch NOTIFY settingsChanged)
    Q_PROPERTY(bool closeOnFocusLoss READ closeOnFocusLoss WRITE setCloseOnFocusLoss NOTIFY settingsChanged)
    Q_PROPERTY(bool keepOpen READ keepOpen WRITE setKeepOpen NOTIFY settingsChanged)
    Q_PROPERTY(bool rememberSection READ rememberSection WRITE setRememberSection NOTIFY settingsChanged)
    Q_PROPERTY(QString lastSection READ lastSection WRITE setLastSection NOTIFY settingsChanged)

public:
    // Same directory as kdock's own settings; "enabled"/"preload" live here too
    // so kdock can read them without starting this process.
    static QString settingsFilePath();

    explicit TileConfig(QObject *parent = nullptr);

    int columns() const { return m_columns; }
    int cellSize() const { return m_cellSize; }
    bool cellStretch() const { return m_cellStretch; }
    int cellMin() const { return m_cellMin; }
    int cellMax() const { return m_cellMax; }
    int cellSpacing() const { return m_cellSpacing; }
    int sidebar() const { return m_sidebar; }
    int sidebarWidth() const { return m_sidebarWidth; }
    bool showIcons() const { return m_showIcons; }
    bool showLabels() const { return m_showLabels; }
    int iconScale() const { return m_iconScale; }
    int labelPosition() const { return m_labelPosition; }
    bool labelBold() const { return m_labelBold; }
    int backgroundMode() const { return m_backgroundMode; }
    QColor backgroundColor() const { return m_backgroundColor; }
    bool backgroundColorSet() const { return m_backgroundColor.isValid(); }
    qreal backgroundOpacity() const { return m_backgroundOpacity; }
    QString backgroundImage() const { return m_backgroundImage; }
    QUrl backgroundImageUrl() const;
    // The eight quick colors the dock offers in its right-click submenu, read
    // from kdock's shared settings file so the tile color menu offers the same
    // palette. Read-only here: they are edited from the dock's settings dialog.
    QStringList presetColors() const;
    bool showSearch() const { return m_showSearch; }
    bool showPower() const { return m_showPower; }
    bool showLetterIndex() const { return m_showLetterIndex; }
    bool closeOnLaunch() const { return m_closeOnLaunch; }
    bool closeOnFocusLoss() const { return m_closeOnFocusLoss; }
    bool keepOpen() const { return m_keepOpen; }
    bool rememberSection() const { return m_rememberSection; }
    QString lastSection() const { return m_lastSection; }

    void setColumns(int columns);
    void setCellSize(int px);
    void setCellStretch(bool on);
    void setCellMin(int px);
    void setCellMax(int px);
    void setCellSpacing(int px);
    void setSidebar(int mode);
    void setSidebarWidth(int px);
    void setShowIcons(bool on);
    void setShowLabels(bool on);
    void setIconScale(int percent);
    void setLabelPosition(int position);
    void setLabelBold(bool on);
    void setBackgroundMode(int mode);
    void setBackgroundColor(const QColor &color);
    void setBackgroundOpacity(qreal opacity);
    void setBackgroundImage(const QString &path);
    void setShowSearch(bool on);
    void setShowPower(bool on);
    void setShowLetterIndex(bool on);
    void setCloseOnLaunch(bool on);
    void setCloseOnFocusLoss(bool on);
    void setKeepOpen(bool on);
    void setRememberSection(bool on);
    void setLastSection(const QString &section);

    // The whole tile layout as a compact JSON document. One blob rather than a
    // key per section because section keys carry '/' and ':', which QSettings
    // reads as group separators.
    QString layoutJson() const;
    void setLayoutJson(const QString &json);

signals:
    void settingsChanged();

private:
    void load();
    // Writes the key and emits settingsChanged() once. Every setter funnels
    // through here so a value can never be persisted without a repaint.
    void store(const QString &key, const QVariant &value);

    QSettings m_settings;

    int m_columns = 10;
    int m_cellSize = 96;
    bool m_cellStretch = true;
    int m_cellMin = 48;
    int m_cellMax = 220;
    int m_cellSpacing = 8;
    int m_sidebar = 0;
    int m_sidebarWidth = 200;
    bool m_showIcons = true;
    bool m_showLabels = true;
    int m_iconScale = 55;
    int m_labelPosition = 0;
    bool m_labelBold = true;
    int m_backgroundMode = 0;
    QColor m_backgroundColor;
    qreal m_backgroundOpacity = 0.92;
    QString m_backgroundImage;
    bool m_showSearch = true;
    bool m_showPower = true;
    bool m_showLetterIndex = true;
    bool m_closeOnLaunch = true;
    bool m_closeOnFocusLoss = true;
    bool m_keepOpen = false;
    bool m_rememberSection = true;
    QString m_lastSection;
};
