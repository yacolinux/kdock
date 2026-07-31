// Persistent settings of one preview strip (QSettings-backed), exposed to QML
// and to the previews settings panel.
//
// Storage mirrors DockConfig's scheme (src/dockconfig.cpp): a shared INI file
// with the opt-in screen list, plus one file per strip. The key set is much
// smaller — no icons, no widgets, no color themes — so this is a deliberate
// small cousin of DockConfig rather than a reuse of it.

#pragma once

#include <QColor>
#include <QObject>
#include <QSettings>
#include <QStringList>

class PreviewConfig : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int edge READ edge WRITE setEdge NOTIFY edgeChanged)
    Q_PROPERTY(int alignment READ alignment WRITE setAlignment NOTIFY alignmentChanged)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)
    Q_PROPERTY(QColor panelColor READ panelColor WRITE setPanelColor NOTIFY panelColorChanged)
    Q_PROPERTY(bool panelColorSet READ panelColorSet NOTIFY panelColorChanged)
    Q_PROPERTY(QStringList panelPresetColors READ panelPresetColors WRITE setPanelPresetColors NOTIFY panelPresetColorsChanged)
    Q_PROPERTY(int stripThickness READ stripThickness WRITE setStripThickness NOTIFY stripThicknessChanged)
    // Derived, read-only: the only two size formulas (see stripThicknessPx).
    Q_PROPERTY(int stripThicknessPx READ stripThicknessPx NOTIFY stripThicknessChanged)
    Q_PROPERTY(int cardWidthPx READ cardWidthPx NOTIFY stripThicknessChanged)
    Q_PROPERTY(int pad READ pad CONSTANT)
    Q_PROPERTY(int stripLength READ stripLength WRITE setStripLength NOTIFY stripLengthChanged)
    Q_PROPERTY(int screenMargin READ screenMargin WRITE setScreenMargin NOTIFY screenMarginChanged)
    Q_PROPERTY(bool reserveSpace READ reserveSpace WRITE setReserveSpace NOTIFY reserveSpaceChanged)
    Q_PROPERTY(bool autohide READ autohide WRITE setAutohide NOTIFY autohideChanged)
    Q_PROPERTY(bool showTitles READ showTitles WRITE setShowTitles NOTIFY showTitlesChanged)
    Q_PROPERTY(int cardSpacing READ cardSpacing WRITE setCardSpacing NOTIFY cardSpacingChanged)
    Q_PROPERTY(int captureMode READ captureMode WRITE setCaptureMode NOTIFY captureModeChanged)
    Q_PROPERTY(int refreshInterval READ refreshInterval WRITE setRefreshInterval NOTIFY refreshIntervalChanged)
    Q_PROPERTY(int activeRefreshInterval READ activeRefreshInterval WRITE setActiveRefreshInterval NOTIFY activeRefreshIntervalChanged)
    Q_PROPERTY(bool includeMinimized READ includeMinimized WRITE setIncludeMinimized NOTIFY filtersChanged)
    Q_PROPERTY(bool currentDesktopOnly READ currentDesktopOnly WRITE setCurrentDesktopOnly NOTIFY filtersChanged)
    Q_PROPERTY(bool thisMonitorOnly READ thisMonitorOnly WRITE setThisMonitorOnly NOTIFY filtersChanged)
    Q_PROPERTY(QString screenName READ screenName CONSTANT)

public:
    // Same numbering as DockConfig::Edge / ::Alignment so the QML idioms copied
    // from Dock.qml keep meaning what they say.
    enum Edge { Bottom = 0, Top = 1, Left = 2, Right = 3 };
    Q_ENUM(Edge)
    enum Alignment { Start = 0, Center = 1, End = 2 };
    Q_ENUM(Alignment)

    // How often a window's thumbnail is taken.
    //   OnceOnFocus (default): exactly one capture per window, the first time it
    //     comes to the foreground while the strip is running, and never again.
    //     A window nobody has focused yet shows its app icon. This is the cheap,
    //     predictable mode: one capture per window for the whole session, and the
    //     window is captured at the one moment it is certainly being drawn.
    //   Periodic: the round-robin refresh (visible cards only, the active window
    //     more often). Off by default; kept for the live-preview work.
    enum CaptureMode { OnceOnFocus = 0, Periodic = 1 };
    Q_ENUM(CaptureMode)

    // ---- Shared settings file (~/.local/share/kdock/previews.conf) ----------
    static QString settingsFilePath();
    // Per-strip file (~/.local/share/kdock/previews-<screen>.conf).
    static QString instanceSettingsFilePath(const QString &screenName);

    // Master switch, written by kdock's "Activar Dock Preview" checkbox and
    // read here so the binary can also be started on its own.
    static bool previewsEnabled();
    static void setPreviewsEnabled(bool enabled);

    // Monitors the user opted in to, persisted under "enabledScreens".
    static QStringList enabledScreens();
    static void setScreenEnabled(const QString &screenName, bool enabled);
    // Monitors ever seen, so the settings panel can still offer an unplugged
    // one. Idempotent.
    static QStringList knownScreens();
    static void addKnownScreen(const QString &screenName);

    explicit PreviewConfig(const QString &screenName, QObject *parent = nullptr);

    QString screenName() const { return m_screenName; }

    int edge() const { return m_edge; }
    int alignment() const { return m_alignment; }
    qreal opacity() const { return m_opacity; }
    QColor panelColor() const { return m_panelColor; }
    bool panelColorSet() const { return m_panelColor.isValid(); }
    QStringList panelPresetColors() const { return m_panelPresetColors; }

    // Cross-axis size of the strip, in px. Single source of truth: QML reads it
    // through stripThicknessPx and PreviewWindow::thickness() returns it for the
    // layer-shell exclusive zone, so the drawn strip and the reserved space can
    // never drift (same rule as DockConfig::dockThickness, see CLAUDE.md).
    int stripThickness() const { return m_stripThickness; }
    int stripThicknessPx() const { return m_stripThickness; }
    // The card fills the strip minus the padding on both sides.
    int cardWidthPx() const { return qMax(32, m_stripThickness - 2 * pad()); }
    int pad() const { return 8; }

    // Length along the anchored edge, as a percentage of the screen edge.
    // 0 = stretch the whole edge (panel mode).
    int stripLength() const { return m_stripLength; }
    int screenMargin() const { return m_screenMargin; }
    bool reserveSpace() const { return m_reserveSpace; }
    bool autohide() const { return m_autohide; }
    bool showTitles() const { return m_showTitles; }
    int cardSpacing() const { return m_cardSpacing; }
    int captureMode() const { return m_captureMode; }
    int refreshInterval() const { return m_refreshInterval; }
    int activeRefreshInterval() const { return m_activeRefreshInterval; }
    bool includeMinimized() const { return m_includeMinimized; }
    bool currentDesktopOnly() const { return m_currentDesktopOnly; }
    bool thisMonitorOnly() const { return m_thisMonitorOnly; }

    void setEdge(int edge);
    void setAlignment(int alignment);
    void setOpacity(qreal opacity);
    void setPanelColor(const QColor &color);
    Q_INVOKABLE void resetPanelColor(); // back to the theme background
    void setPanelPresetColors(const QStringList &colors);
    void setStripThickness(int px);
    void setStripLength(int percent);
    void setScreenMargin(int margin);
    void setReserveSpace(bool reserve);
    void setAutohide(bool autohide);
    void setShowTitles(bool show);
    void setCardSpacing(int spacing);
    void setCaptureMode(int mode);
    void setRefreshInterval(int ms);
    void setActiveRefreshInterval(int ms);
    void setIncludeMinimized(bool include);
    void setCurrentDesktopOnly(bool only);
    void setThisMonitorOnly(bool only);

signals:
    void edgeChanged();
    void alignmentChanged();
    void opacityChanged();
    void panelColorChanged();
    void panelPresetColorsChanged();
    void stripThicknessChanged();
    void stripLengthChanged();
    void screenMarginChanged();
    void reserveSpaceChanged();
    void autohideChanged();
    void showTitlesChanged();
    void cardSpacingChanged();
    void captureModeChanged();
    void refreshIntervalChanged();
    void activeRefreshIntervalChanged();
    // One signal for the three window filters: they all mean "re-evaluate which
    // windows belong in this strip", and the model reacts the same way to each.
    void filtersChanged();

private:
    void load();

    QSettings m_settings;
    QString m_screenName;
    int m_edge = Left;
    int m_alignment = Center;
    qreal m_opacity = 0.85;
    QColor m_panelColor; // invalid = inherit the theme background
    QStringList m_panelPresetColors;
    int m_stripThickness = 260;
    int m_stripLength = 0; // % of the screen edge; 0 = whole edge
    int m_screenMargin = 4;
    bool m_reserveSpace = true;
    bool m_autohide = false;
    bool m_showTitles = true;
    int m_cardSpacing = 10;
    int m_captureMode = OnceOnFocus;
    int m_refreshInterval = 4000;
    int m_activeRefreshInterval = 1500;
    bool m_includeMinimized = true;
    bool m_currentDesktopOnly = true;
    bool m_thisMonitorOnly = true;
};
