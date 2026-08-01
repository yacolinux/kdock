// The cards of one strip: the windows that belong to this monitor, in KWin's
// announcement order, each with its thumbnail revision.
//
// It also owns the refresh policy, because everything the policy needs (which
// rows exist, which are visible, which one is active, when each was last
// captured) lives right here. The capture *queue* is global instead: every strip
// shares one ThumbnailSource so only one window is ever being captured at a time.

#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>

#include "previewconfig.h"

class DesktopEntryIndex;
class KWinWindow;
class KWinWindows;
class PreviewConfig;
class ThumbnailCache;
class ThumbnailSource;
class VirtualDesktops;
class QTimer;

class PreviewModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // Auto-fit: 1.0 = cards at the configured size; < 1.0 when so many windows
    // are open that they no longer fit in the strip's length and the cards
    // shrink so they all do (PreviewStrip multiplies crossSize by this). Driven
    // by PreviewConfig::autoFitCards / fitMinCardWidth.
    Q_PROPERTY(qreal cardScale READ cardScale NOTIFY cardScaleChanged)
    // The strip's cross-axis size the cards actually need: the shrunk card
    // width plus the padding. PreviewWindow uses it for the exclusive zone and
    // PreviewStrip for the surface, so the panel hugs the thumbnails instead of
    // leaving dead bands when the auto-fit shrinks them (bug 2026-07-31). Equals
    // stripThicknessPx while the cards are full size.
    Q_PROPERTY(int effectiveThicknessPx READ effectiveThicknessPx NOTIFY effectiveThicknessChanged)
    // Main-axis extent of the cards (see contentLengthPx below).
    Q_PROPERTY(int contentLengthPx READ contentLengthPx NOTIFY contentLengthChanged)
public:
    enum Roles {
        UuidRole = Qt::UserRole + 1,
        // The uuid with its braces stripped, for building the image:// URL. QUrl
        // would percent-encode braces and the provider id would stop matching the
        // cache key (bug 2026-07-30). Never use `uuid` in a URL.
        ThumbIdRole,
        TitleRole,
        AppNameRole,
        IconNameRole,
        ThumbRevisionRole,
        AspectRole,   // width / height of the window, for the card's shape
        ActiveRole,
        MinimizedRole,
    };

    PreviewModel(PreviewConfig *config, KWinWindows *windows, VirtualDesktops *desktops,
                 DesktopEntryIndex *apps, ThumbnailCache *cache, ThumbnailSource *source,
                 QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    qreal cardScale() const { return m_cardScale; }
    // Main-axis extent of every card at the current scale, spacing included.
    // QML aligns the content with it instead of reading the ListView's own
    // contentWidth/contentHeight: those *include* the Flickable margin the
    // alignment writes, which closed a binding loop (bug 2026-07-31).
    int contentLengthPx() const { return m_contentLength; }
    int effectiveThicknessPx() const
    {
        const int cardW = m_config ? qRound(m_config->cardWidthPx() * m_cardScale) : 260;
        return cardW + (m_config ? 2 * m_config->pad() : 16);
    }

    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void closeWindow(int row);
    Q_INVOKABLE void toggleMinimize(int row);
    // Hover: jump the queue for this one card. No-op in OnceOnFocus mode — a
    // window that is not in the foreground is exactly the one whose capture is
    // not trustworthy, and that mode promises one capture per window.
    Q_INVOKABLE void refreshNow(int row);

    // Rows the ListView currently has on screen (inclusive). Rows outside the
    // range are never captured. Called from QML on scroll.
    Q_INVOKABLE void setVisibleRange(int first, int last);

    // The strip's usable length along its main axis, in px (the ListView's own
    // width/height, already excluding the padding). The auto-fit shrinks the
    // cards until their total extent fits inside this. Reported from QML on any
    // size or orientation change.
    Q_INVOKABLE void setAvailableLength(int px);

    // Whole strip hidden by autohide: no captures at all while it is.
    void setStripVisible(bool visible);
    // devicePixelRatio of the strip's monitor, so captures are requested at the
    // resolution the card is actually drawn at.
    void setTargetScale(qreal scale);

public slots:
    // Recompute which windows belong here and apply the difference. Public
    // because PreviewManager re-runs it a few times right after startup, while
    // KWin's initial burst of windows is still draining.
    void sync();

signals:
    void countChanged();
    void cardScaleChanged();
    void effectiveThicknessChanged();
    void contentLengthChanged();

private:
    struct Row {
        QPointer<KWinWindow> window;
    };

    bool accepts(const KWinWindow *window) const;
    int rowOf(const KWinWindow *window) const;
    void watch(KWinWindow *window);
    void emitRowChanged(const KWinWindow *window, const QList<int> &roles);
    // Ask for a capture of one row. `onlyIfMissing` skips windows that already
    // have one, which is what makes OnceOnFocus a one-shot.
    void requestCapture(int row, bool onlyIfMissing);
    // OnceOnFocus: capture a window shortly after it comes to the foreground.
    // The small delay is on purpose — KWin marks a window active before it has
    // finished repainting, and capturing on the same tick can catch the old
    // frame.
    void captureOnFocus(KWinWindow *window);
    bool periodicMode() const;
    // Capture box for one row, in device pixels.
    QSize captureTarget(int row) const;
    // One tick: pick the visible row most overdue for a capture and ask for it.
    void tick();
    void restartTimer();

    // ---- Auto-fit ----------------------------------------------------------
    // Re-derive m_cardScale from the current rows, the config and the reported
    // available length. Called whenever any of its inputs change; cheap enough
    // to run inside sync().
    void recomputeCardScale();
    // Total main-axis extent the cards would occupy at `crossSize` (the card's
    // cross-axis size), using the same floors PreviewCard.qml applies so the
    // computed fit is exact, not an approximation.
    int mainAxisNeeded(int crossSize) const;

    PreviewConfig *m_config;
    KWinWindows *m_windows;
    VirtualDesktops *m_desktops;
    DesktopEntryIndex *m_apps;
    ThumbnailCache *m_cache;
    ThumbnailSource *m_source;

    QList<Row> m_rows;
    QTimer *m_timer;
    int m_firstVisible = 0;
    int m_lastVisible = -1; // -1 = QML has not reported a range yet: assume all
    bool m_stripVisible = true;
    qreal m_targetScale = 1.0;
    qreal m_cardScale = 1.0;
    int m_effectiveThickness = -1; // sentinel: force the first emit
    int m_contentLength = -1;      // idem
    int m_availableLength = 0; // px; 0 until QML reports the strip's length
};
