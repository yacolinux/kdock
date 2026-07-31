#include "taskmanager.h"

#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>

WlrWindow::WlrWindow(struct ::zwlr_foreign_toplevel_handle_v1 *object, QObject *parent)
    : AbstractWindow(parent)
    , QtWayland::zwlr_foreign_toplevel_handle_v1(object)
{
}

WlrWindow::~WlrWindow()
{
    if (QtWayland::zwlr_foreign_toplevel_handle_v1::object())
        destroy();
}

void WlrWindow::activate()
{
    auto *wl = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (wl && wl->seat())
        zwlr_foreign_toplevel_handle_v1::activate(wl->seat());
}

void WlrWindow::zwlr_foreign_toplevel_handle_v1_title(const QString &t)
{
    title = t;
}

void WlrWindow::zwlr_foreign_toplevel_handle_v1_app_id(const QString &id)
{
    appId = id;
}

void WlrWindow::zwlr_foreign_toplevel_handle_v1_state(wl_array *state)
{
    activated = minimized = false;
    const auto *begin = static_cast<const uint32_t *>(state->data);
    const auto *end = begin + state->size / sizeof(uint32_t);
    for (const uint32_t *it = begin; it != end; ++it) {
        switch (*it) {
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED: minimized = true; break;
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED: activated = true; break;
        }
    }
}

void WlrWindow::zwlr_foreign_toplevel_handle_v1_done()
{
    if (!ready) {
        ready = true;
        emit firstDone();
    } else {
        emit changed();
    }
}

void WlrWindow::zwlr_foreign_toplevel_handle_v1_closed()
{
    emit windowClosed();
}

// ---------------------------------------------------------------------------

WlrTaskManager::WlrTaskManager(WindowMonitor *owner)
    : QWaylandClientExtensionTemplate<WlrTaskManager>(3)
    , m_owner(owner)
{
    initialize();
}

WlrTaskManager::~WlrTaskManager()
{
    if (isActive())
        stop();
}

void WlrTaskManager::zwlr_foreign_toplevel_manager_v1_toplevel(struct ::zwlr_foreign_toplevel_handle_v1 *toplevel)
{
    auto *w = new WlrWindow(toplevel, m_owner);
    // State is atomic on "done"; expose the window only once complete
    connect(w, &WlrWindow::firstDone, m_owner, [this, w] { m_owner->registerWindow(w); });
}

WlrBackend::WlrBackend(QObject *parent)
    : WindowMonitor(parent)
    , m_extension(this)
{
}

void WlrBackend::toggleShowDesktop()
{
    if (!m_showDesktopActive) {
        m_minimizedByUs.clear();
        for (AbstractWindow *w : std::as_const(windows)) {
            if (!w->minimized && !w->skipTaskbar) {
                w->minimize();
                m_minimizedByUs.append(w);
            }
        }
        m_showDesktopActive = true;
    } else {
        for (const QPointer<AbstractWindow> &w : std::as_const(m_minimizedByUs)) {
            if (w)
                w->unminimize();
        }
        m_minimizedByUs.clear();
        m_showDesktopActive = false;
    }
    emit showDesktopChanged();
}
