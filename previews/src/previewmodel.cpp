#include "previewmodel.h"

#include "kwinwindows.h"
#include "previewconfig.h"
#include "thumbnailcache.h"
#include "thumbnailsource.h"
#include "virtualdesktops.h"

#include "desktopentry.h"

#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

PreviewModel::PreviewModel(PreviewConfig *config, KWinWindows *windows,
                           VirtualDesktops *desktops, DesktopEntryIndex *apps,
                           ThumbnailCache *cache, ThumbnailSource *source, QObject *parent)
    : QAbstractListModel(parent)
    , m_config(config)
    , m_windows(windows)
    , m_desktops(desktops)
    , m_apps(apps)
    , m_cache(cache)
    , m_source(source)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &PreviewModel::tick);

    if (m_windows) {
        connect(m_windows, &KWinWindows::windowAdded, this, [this](KWinWindow *w) {
            watch(w);
            sync();
        });
        connect(m_windows, &KWinWindows::windowRemoved, this, &PreviewModel::sync);
        for (KWinWindow *w : m_windows->windows())
            watch(w);
    }
    if (m_config) {
        connect(m_config, &PreviewConfig::filtersChanged, this, &PreviewModel::sync);
        connect(m_config, &PreviewConfig::refreshIntervalChanged, this,
                &PreviewModel::restartTimer);
        connect(m_config, &PreviewConfig::activeRefreshIntervalChanged, this,
                &PreviewModel::restartTimer);
        // Switching to Periodic has to start the timer that OnceOnFocus leaves
        // stopped (and back).
        connect(m_config, &PreviewConfig::captureModeChanged, this,
                &PreviewModel::restartTimer);
        // Anything that changes the auto-fit formula or its floor re-derives
        // the card scale (the row list does too, inside sync()).
        connect(m_config, &PreviewConfig::edgeChanged, this, &PreviewModel::recomputeCardScale);
        connect(m_config, &PreviewConfig::stripThicknessChanged, this,
                &PreviewModel::recomputeCardScale);
        connect(m_config, &PreviewConfig::cardSpacingChanged, this,
                &PreviewModel::recomputeCardScale);
        connect(m_config, &PreviewConfig::showTitlesChanged, this,
                &PreviewModel::recomputeCardScale);
        connect(m_config, &PreviewConfig::autoFitCardsChanged, this,
                &PreviewModel::recomputeCardScale);
        connect(m_config, &PreviewConfig::fitMinCardWidthChanged, this,
                &PreviewModel::recomputeCardScale);
    }
    if (m_desktops)
        connect(m_desktops, &VirtualDesktops::currentChanged, this, &PreviewModel::sync);
    if (m_cache) {
        connect(m_cache, &ThumbnailCache::updated, this, [this](const QString &uuid, int) {
            for (int row = 0; row < m_rows.size(); ++row) {
                if (m_rows.at(row).window && m_rows.at(row).window->uuid() == uuid) {
                    const QModelIndex idx = index(row, 0);
                    emit dataChanged(idx, idx, {ThumbRevisionRole});
                    return;
                }
            }
        });
    }
    // A monitor coming or going changes which windows land on this one.
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &PreviewModel::sync);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &PreviewModel::sync);

    sync();
    restartTimer();
}

void PreviewModel::watch(KWinWindow *window)
{
    // Granular on purpose: geometry moves are frequent and must not cost a
    // membership recomputation unless the window actually changed monitor.
    connect(window, &KWinWindow::metadataChanged, this, [this, window] {
        emitRowChanged(window, {TitleRole, AppNameRole, IconNameRole});
    });
    connect(window, &KWinWindow::stateChanged, this, [this, window] {
        // Minimizing can add or drop the row (includeMinimized), so a membership
        // check has to run; when the row stays, sync() is a no-op and the
        // highlight is refreshed here.
        sync();
        emitRowChanged(window, {ActiveRole, MinimizedRole});
        // Coming to the foreground is *the* moment to capture: the window is
        // certainly being drawn. In OnceOnFocus that is the only capture it will
        // ever get; in Periodic it just refreshes a now-stale thumbnail.
        if (window->active())
            captureOnFocus(window);
    });
    connect(window, &KWinWindow::geometryChanged, this, [this, window] {
        sync();
        emitRowChanged(window, {AspectRole});
    });
    connect(window, &KWinWindow::desktopsChanged, this, &PreviewModel::sync);
}

int PreviewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> PreviewModel::roleNames() const
{
    return {
        {UuidRole, "uuid"},
        {ThumbIdRole, "thumbId"},
        {TitleRole, "title"},
        {AppNameRole, "appName"},
        {IconNameRole, "iconName"},
        {ThumbRevisionRole, "thumbRevision"},
        {AspectRole, "aspect"},
        {ActiveRole, "active"},
        {MinimizedRole, "minimized"},
    };
}

QVariant PreviewModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const KWinWindow *w = m_rows.at(index.row()).window;
    if (!w)
        return {};

    switch (role) {
    case UuidRole:
        return w->uuid();
    case ThumbIdRole:
        // Same normalization the cache does, so the URL QML builds and the key
        // the capture was stored under are the same string by construction.
        return ThumbnailCache::normalizeKey(w->uuid());
    case TitleRole:
        return w->title();
    case AppNameRole: {
        // The .desktop name reads better than the app_id ("Dolphin", not
        // "org.kde.dolphin"); forAppId() carries the Chromium/PWA heuristics.
        const DesktopEntry entry = m_apps ? m_apps->forAppId(w->appId()) : DesktopEntry();
        return entry.isValid() && !entry.name.isEmpty() ? entry.name : w->appId();
    }
    case IconNameRole: {
        const DesktopEntry entry = m_apps ? m_apps->forAppId(w->appId()) : DesktopEntry();
        if (entry.isValid() && !entry.icon.isEmpty())
            return entry.icon;
        // KWin's own themed name is the next best thing (Xwayland windows).
        if (!w->themedIconName().isEmpty())
            return w->themedIconName();
        return QStringLiteral("application-x-executable");
    }
    case ThumbRevisionRole:
        return m_cache ? m_cache->revision(w->uuid()) : 0;
    case AspectRole: {
        const QRect g = w->geometry();
        if (g.width() > 0 && g.height() > 0)
            return qreal(g.width()) / qreal(g.height());
        return qreal(16) / qreal(9); // nothing reported yet
    }
    case ActiveRole:
        return w->active();
    case MinimizedRole:
        return w->minimized();
    default:
        return {};
    }
}

bool PreviewModel::accepts(const KWinWindow *window) const
{
    if (!window || !window->ready())
        return false;
    if (window->skipTaskbar())
        return false;
    if (!m_config)
        return true;
    if (window->minimized() && !m_config->includeMinimized())
        return false;

    if (m_config->currentDesktopOnly() && m_desktops) {
        const QString current = m_desktops->current();
        // Empty means KWin never answered: filtering on nothing would empty the
        // strip, so let everything through instead.
        if (!current.isEmpty() && !window->onDesktop(current))
            return false;
    }

    if (m_config->thisMonitorOnly()) {
        const QRect g = window->geometry();
        // No geometry yet: keep it rather than hide a real window.
        if (!g.isEmpty()) {
            QScreen *own = nullptr;
            const auto screens = QGuiApplication::screens();
            for (QScreen *s : screens) {
                if (s->name() == m_config->screenName()) {
                    own = s;
                    break;
                }
            }
            if (own && !own->geometry().contains(g.center()))
                return false;
        }
    }
    return true;
}

int PreviewModel::rowOf(const KWinWindow *window) const
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).window.data() == window)
            return row;
    }
    return -1;
}

void PreviewModel::emitRowChanged(const KWinWindow *window, const QList<int> &roles)
{
    const int row = rowOf(window);
    if (row < 0)
        return;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, roles);
}

void PreviewModel::sync()
{
    if (!m_windows)
        return;

    QList<KWinWindow *> wanted;
    for (KWinWindow *w : m_windows->windows()) {
        if (accepts(w))
            wanted.append(w);
    }

    const int before = m_rows.size();

    // Drop rows that left (closed, minimized away, moved to another monitor or
    // desktop). Backwards so the indices stay valid.
    for (int row = m_rows.size() - 1; row >= 0; --row) {
        KWinWindow *w = m_rows.at(row).window;
        if (w && wanted.contains(w))
            continue;
        beginRemoveRows(QModelIndex(), row, row);
        m_rows.removeAt(row);
        endRemoveRows();
    }

    // Insert the new ones at their place in KWin's order. Reordering of existing
    // rows is deliberately not handled: KWin's announcement order is stable, so
    // the only way the sequence changes is by insertion or removal.
    for (int i = 0; i < wanted.size(); ++i) {
        KWinWindow *w = wanted.at(i);
        if (rowOf(w) >= 0)
            continue;
        const int row = qMin(i, m_rows.size());
        beginInsertRows(QModelIndex(), row, row);
        m_rows.insert(row, Row{w});
        endInsertRows();
        // A brand-new card has nothing to show. In Periodic mode ask right away
        // instead of waiting for its turn in the round-robin; in OnceOnFocus only
        // the window that is *already* in the foreground qualifies (the rest wait
        // until the user brings them up, showing their app icon meanwhile).
        if (periodicMode())
            requestCapture(row, false);
        else if (w->active())
            requestCapture(row, true);
    }

    if (m_rows.size() != before)
        emit countChanged();

    recomputeCardScale();
}

void PreviewModel::activate(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    KWinWindow *w = m_rows.at(row).window;
    if (!w)
        return;
    // Toggle-minimize like a taskbar button: a click on the window that is
    // already in the foreground minimizes it instead of raising it again.
    // activate() already unminimizes first, so a minimized window is restored.
    if (w->active() && !w->minimized())
        w->minimize();
    else
        w->activate();
}

void PreviewModel::closeWindow(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    if (KWinWindow *w = m_rows.at(row).window)
        w->requestClose();
}

void PreviewModel::toggleMinimize(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    KWinWindow *w = m_rows.at(row).window;
    if (!w)
        return;
    if (w->minimized())
        w->unminimize();
    else
        w->minimize();
}

QSize PreviewModel::captureTarget(int row) const
{
    Q_UNUSED(row);
    // Scaled with the card: with auto-fit shrinking the cards down to fit many
    // windows, the stored thumbnails stay at (roughly) the size they are drawn,
    // instead of every card caching a full-size capture.
    const int cardW = m_config ? qRound(m_config->cardWidthPx() * m_cardScale) : 240;
    const int px = qMax(32, qRound(cardW * m_targetScale));
    // A box, not an exact size: KeepAspectRatio fits the window inside it, so
    // only the *cross* axis of the strip really constrains the result, and the
    // other one is left generous enough not to bite on extreme window shapes.
    const bool horizontal = m_config
        && (m_config->edge() == PreviewConfig::Bottom || m_config->edge() == PreviewConfig::Top);
    return horizontal ? QSize(px * 4, px) : QSize(px, px * 4);
}

bool PreviewModel::periodicMode() const
{
    return m_config && m_config->captureMode() == PreviewConfig::Periodic;
}

void PreviewModel::requestCapture(int row, bool onlyIfMissing)
{
    if (!m_source || row < 0 || row >= m_rows.size())
        return;
    KWinWindow *w = m_rows.at(row).window;
    if (!w)
        return;
    // A window whose capture simply failed keeps revision 0, so it is retried the
    // next time it comes to the foreground — bounded, unlike a timer retry.
    if (onlyIfMissing && m_cache && m_cache->revision(w->uuid()) > 0)
        return;
    // Stamp before asking: the scheduler must not pick this row again while the
    // capture is queued, and a failure must not turn into a retry loop either.
    if (m_cache)
        m_cache->markAttempt(w->uuid());
    m_source->request(w->uuid(), captureTarget(row));
}

void PreviewModel::captureOnFocus(KWinWindow *window)
{
    const bool periodic = periodicMode();
    QPointer<KWinWindow> guard(window);
    // KWin announces the activation before the window has finished repainting, so
    // capturing on this very tick can catch the frame it had while it was still in
    // the background.
    QTimer::singleShot(400, this, [this, guard, periodic] {
        if (!guard)
            return;
        const int row = rowOf(guard);
        // Focus may have moved on again in those 400 ms; in OnceOnFocus the
        // promise is a capture of the window while it *is* in front.
        if (row < 0 || (!periodic && !guard->active()))
            return;
        requestCapture(row, !periodic);
    });
}

void PreviewModel::refreshNow(int row)
{
    // Hover only jumps the queue in Periodic mode; see the header.
    if (!periodicMode())
        return;
    requestCapture(row, false);
}

void PreviewModel::setVisibleRange(int first, int last)
{
    m_firstVisible = qMax(0, first);
    m_lastVisible = last;
}

void PreviewModel::setStripVisible(bool visible)
{
    if (m_stripVisible == visible)
        return;
    m_stripVisible = visible;
    // Coming back from autohide: in Periodic mode whatever is on screen is stale.
    // In OnceOnFocus there is nothing to do — the thumbnails are deliberately
    // frozen at the moment each window was last in front.
    if (visible && periodicMode()) {
        const int last = m_lastVisible < 0 ? m_rows.size() - 1 : m_lastVisible;
        for (int row = m_firstVisible; row <= qMin(last, m_rows.size() - 1); ++row)
            requestCapture(row, false);
    }
    restartTimer();
}

void PreviewModel::setTargetScale(qreal scale)
{
    m_targetScale = scale > 0 ? scale : 1.0;
}

void PreviewModel::setAvailableLength(int px)
{
    if (m_availableLength == px)
        return;
    m_availableLength = px;
    recomputeCardScale();
}

void PreviewModel::recomputeCardScale()
{
    const qreal previous = m_cardScale;
    m_cardScale = 1.0;

    // Few windows, no fit, or no length reported yet: full size (the strip
    // scrolls, exactly like before the feature existed).
    if (m_config && m_config->autoFitCards() && m_rows.size() > 1
        && m_availableLength > 0) {
        const int base = m_config->cardWidthPx();
        int cross = base;
        // Fixed-point fit, the same shape as Dock.qml's fitScale: the needed
        // length is affine in `cross` (each card's main axis = aspect * cross
        // plus small floors), so `cross * avail / need` converges in a few
        // passes. A little under-shoot (×0.99) keeps the last card from kissing
        // the edge.
        for (int i = 0; i < 10; ++i) {
            const int need = mainAxisNeeded(cross);
            if (need <= m_availableLength)
                break;
            cross = qMax(1, int(qreal(cross) * m_availableLength * 0.99 / need));
        }
        cross = qBound(m_config->fitMinCardWidth(), cross, base);
        m_cardScale = qreal(cross) / base;
    }

    if (!qFuzzyCompare(m_cardScale, previous))
        emit cardScaleChanged();
    const int effective = effectiveThicknessPx();
    if (effective != m_effectiveThickness) {
        m_effectiveThickness = effective;
        emit effectiveThicknessChanged();
    }
}

int PreviewModel::mainAxisNeeded(int crossSize) const
{
    // Mirror of PreviewCard.qml's geometry, floors included: on a horizontal
    // strip the main axis is the width = (crossSize - titleHeight) * aspect
    // (32 px floor); on a vertical one it is the height = crossSize / aspect +
    // titleHeight (24 px floor, aspect floored at 0.2). The titleHeight is the
    // same 16 px the card hardcodes.
    const bool horizontal = m_config
        && (m_config->edge() == PreviewConfig::Bottom || m_config->edge() == PreviewConfig::Top);
    const int titleH = m_config && m_config->showTitles() ? 16 : 0;

    int total = 0;
    for (const Row &row : m_rows) {
        KWinWindow *w = row.window;
        qreal aspect = qreal(16) / qreal(9); // nothing reported yet
        if (w) {
            const QRect g = w->geometry();
            if (g.width() > 0 && g.height() > 0)
                aspect = qreal(g.width()) / qreal(g.height());
        }
        if (horizontal)
            total += qMax(32, qRound((crossSize - titleH) * aspect));
        else
            total += qMax(24, qRound(crossSize / qMax(0.2, aspect))) + titleH;
    }
    if (m_config && m_rows.size() > 1)
        total += (m_rows.size() - 1) * m_config->cardSpacing();
    return total;
}

void PreviewModel::restartTimer()
{
    if (!m_config)
        return;
    // OnceOnFocus never polls: captures are driven purely by windows coming to
    // the foreground.
    if (!periodicMode()) {
        m_timer->stop();
        return;
    }
    // Tick faster than the shortest interval so a row that falls due is picked
    // up promptly, but never faster than 250 ms.
    const int shortest = qMin(m_config->refreshInterval(), m_config->activeRefreshInterval());
    m_timer->setInterval(qMax(250, shortest / 2));
    if (m_stripVisible)
        m_timer->start();
    else
        m_timer->stop();
}

void PreviewModel::tick()
{
    if (!periodicMode() || !m_stripVisible || m_rows.isEmpty() || !m_source || !m_config)
        return;

    const qint64 now = ThumbnailCache::nowMs();
    const int last = m_lastVisible < 0 ? m_rows.size() - 1 : qMin(m_lastVisible, m_rows.size() - 1);

    // The most overdue visible row wins; one capture per tick keeps the load
    // predictable no matter how many windows are open.
    int best = -1;
    qint64 bestOverdue = 0;
    for (int row = m_firstVisible; row <= last; ++row) {
        KWinWindow *w = m_rows.at(row).window;
        if (!w || !m_cache)
            continue;
        const int interval =
            w->active() ? m_config->activeRefreshInterval() : m_config->refreshInterval();
        const qint64 lastAttempt = m_cache->lastAttempt(w->uuid());
        const qint64 overdue = now - (lastAttempt + interval);
        if (overdue < 0)
            continue;
        if (best < 0 || overdue > bestOverdue) {
            best = row;
            bestOverdue = overdue;
        }
    }
    if (best >= 0)
        requestCapture(best, false);
}
