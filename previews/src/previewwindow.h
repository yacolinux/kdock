// One preview strip: a QQuickView flagged as a layer-shell surface.
//
// The layer-shell plumbing is the same as kdock's DockWindow (src/dockwindow.cpp)
// — anchors from the configured edge, the exclusive zone, the destroy/recreate
// dance when the surface has to move to another output, and the input-mask trick
// for autohide. It is mirrored rather than shared because the two windows have
// different configs and different content, and the pieces that matter are ~120
// lines of well-understood code.

#pragma once

#include <QQuickView>

class DesktopEntryIndex;
class PreviewConfig;
class PreviewManager;
class PreviewModel;
class ThumbnailCache;
class Theme;

class PreviewWindow : public QQuickView
{
    Q_OBJECT
public:
    PreviewWindow(PreviewConfig *config, Theme *theme, PreviewModel *model,
                  ThumbnailCache *cache, DesktopEntryIndex *apps, QObject *parent = nullptr);

    void setManager(PreviewManager *manager) { m_manager = manager; }

    // Called from QML when the autohide animation finishes: shrinks the input
    // region to a thin strip on the screen edge so clicks pass through to the
    // windows below, and stops the capture scheduler while hidden.
    Q_INVOKABLE void setHidden(bool hidden);

    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void quit();
    // Relaunch with the same CLI arguments, then quit this instance.
    Q_INVOKABLE void restart();

private:
    void applyLayerProperties();
    void applyScreen();
    void scheduleApplyScreen();
    // Cross-axis size of the strip: the single formula in PreviewConfig, so the
    // drawn strip and the reserved space cannot drift.
    int thickness() const;

    PreviewConfig *m_config;
    Theme *m_theme;
    PreviewModel *m_model;
    ThumbnailCache *m_cache;
    DesktopEntryIndex *m_apps;
    PreviewManager *m_manager = nullptr;
    bool m_hidden = false;
    bool m_screenChangePending = false;
    // wl_output the current layer surface is bound to (as a raw pointer value).
    // Tracked here because QWindow::screen() is unreliable right after a
    // layer-surface recreation and would otherwise wedge the strip on one output.
    quintptr m_boundOutput = 0;
};
