#include "layershell.h"

#include <QtWaylandClient/private/qwaylandshellintegrationfactory_p.h>
#include <QtWaylandClient/private/qwaylandshellintegrationplugin_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>

#include <QEvent>
#include <QDynamicPropertyChangeEvent>
#include <QMargins>
#include <QScreen>
#include <QWindow>

struct xdg_popup; // opaque; only passed through to get_popup

using QtWaylandClient::QWaylandDisplay;
using QtWaylandClient::QWaylandShellSurface;
using QtWaylandClient::QWaylandWindow;

LayerShellIntegration::LayerShellIntegration()
    : QtWaylandClient::QWaylandShellIntegrationTemplate<LayerShellIntegration>(4)
{
}

LayerShellIntegration::~LayerShellIntegration()
{
    if (object() && zwlr_layer_shell_v1_get_version(object()) >= ZWLR_LAYER_SHELL_V1_DESTROY_SINCE_VERSION)
        destroy();
}

bool LayerShellIntegration::initialize(QWaylandDisplay *display)
{
    QWaylandShellIntegrationTemplate::initialize(display);

    m_xdgFallback.reset(
        QtWaylandClient::QWaylandShellIntegrationFactory::create(QStringLiteral("xdg-shell"), display));

    if (!isActive())
        qWarning("kdock: compositor does not support zwlr_layer_shell_v1, "
                 "all windows fall back to xdg-shell (no panel behavior)");

    return isActive() || m_xdgFallback != nullptr;
}

QWaylandShellSurface *LayerShellIntegration::createShellSurface(QWaylandWindow *window)
{
    QWindow *w = window->window();
    if (isActive() && w && w->property("kdock.layershell").toBool())
        return new LayerSurface(this, window);
    if (m_xdgFallback)
        return m_xdgFallback->createShellSurface(window);
    return nullptr;
}

// ---------------------------------------------------------------------------

LayerSurface::LayerSurface(LayerShellIntegration *shell, QWaylandWindow *window)
    : QWaylandShellSurface(window)
{
    QWindow *w = window->window();

    bool ok = false;
    m_layer = w->property("kdock.layer").toUInt(&ok);
    if (!ok)
        m_layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;

    // Pin the surface to a specific output; a null output lets the compositor
    // choose. DockWindow resolves the target monitor's wl_output and stashes it
    // as "kdock.output" right before show(); that value is authoritative and
    // avoids binding to the wrong monitor. Fall back to QWindow::screen() and
    // then window->waylandScreen() if the property is absent (e.g. non-dock
    // surfaces), both of which can lag behind a destroy()+setScreen()+show().
    ::wl_output *output = nullptr;
    bool okOutput = false;
    const quintptr outputPtr = w->property("kdock.output").toULongLong(&okOutput);
    if (okOutput && outputPtr)
        output = reinterpret_cast<::wl_output *>(outputPtr);
    if (!output) {
        if (QScreen *qs = w->screen()) {
            if (auto *handle = dynamic_cast<QtWaylandClient::QWaylandScreen *>(qs->handle()))
                output = handle->output();
        }
    }
    if (!output) {
        if (auto *screen = window->waylandScreen())
            output = screen->output();
    }

    init(shell->get_layer_surface(window->wlSurface(), output, m_layer,
                                  QStringLiteral("dock")));

    applyProperty("kdock.anchors");
    applyProperty("kdock.exclusiveZone");
    applyProperty("kdock.margins");
    applyProperty("kdock.keyboardInteractivity");

    sendSize(w->geometry().size());

    // React to live property changes (autohide toggling exclusive zone,
    // moving the dock to another edge, etc.)
    w->installEventFilter(this);
}

LayerSurface::~LayerSurface()
{
    destroy();
}

void LayerSurface::applyProperty(const QByteArray &name)
{
    QWindow *w = window()->window();
    const QVariant value = w->property(name.constData());

    if (name == "kdock.anchors") {
        set_anchor(value.toUInt());
        // Stretched dimensions depend on the anchor mask
        sendSize(w->geometry().size());
    } else if (name == "kdock.exclusiveZone") {
        set_exclusive_zone(value.toInt());
    } else if (name == "kdock.margins") {
        const QMargins m = value.value<QMargins>();
        set_margin(m.top(), m.right(), m.bottom(), m.left());
    } else if (name == "kdock.keyboardInteractivity") {
        set_keyboard_interactivity(value.toUInt());
    } else if (name == "kdock.layer") {
        bool ok = false;
        const uint layer = value.toUInt(&ok);
        if (ok && layer != m_layer && version() >= ZWLR_LAYER_SURFACE_V1_SET_LAYER_SINCE_VERSION) {
            m_layer = layer;
            set_layer(layer);
        }
    }
}

bool LayerSurface::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::DynamicPropertyChange) {
        auto *pe = static_cast<QDynamicPropertyChangeEvent *>(event);
        if (pe->propertyName().startsWith("kdock.")) {
            applyProperty(pe->propertyName());
            // Double-buffered state: needs a commit, so schedule a frame.
            window()->window()->requestUpdate();
        }
    }
    return QWaylandShellSurface::eventFilter(watched, event);
}

void LayerSurface::setWindowGeometry(const QRect &rect)
{
    sendSize(rect.size());
}

void LayerSurface::sendSize(const QSize &size)
{
    // When anchored to two opposite edges the compositor dictates that
    // dimension (edge-to-edge panels); the protocol wants 0 there.
    const uint anchors = window()->window()->property("kdock.anchors").toUInt();
    const bool stretchH = (anchors & 12u) == 12u; // left|right
    const bool stretchV = (anchors & 3u) == 3u;   // top|bottom
    set_size(stretchH ? 0 : uint32_t(qMax(1, size.width())),
             stretchV ? 0 : uint32_t(qMax(1, size.height())));
}

void LayerSurface::zwlr_layer_surface_v1_configure(uint32_t serial, uint32_t width, uint32_t height)
{
    ack_configure(serial);
    m_pendingSize = QSize(int(width), int(height));

    if (!m_configured) {
        m_configured = true;
        applyConfigure();
        window()->updateExposure();
    } else {
        applyConfigureWhenPossible();
    }
}

void LayerSurface::zwlr_layer_surface_v1_closed()
{
    window()->window()->close();
}

void LayerSurface::applyConfigure()
{
    if (m_pendingSize.isValid() && !m_pendingSize.isEmpty())
        resizeFromApplyConfigure(m_pendingSize);
}

std::any LayerSurface::surfaceRole() const
{
    return QtWayland::zwlr_layer_surface_v1::object();
}

void LayerSurface::attachPopup(QWaylandShellSurface *popup)
{
    // Qt's xdg-shell integration creates parent-less xdg_popups for windows
    // whose transient parent has a non-xdg role, then hands them to us here.
    std::any role = popup->surfaceRole();
    if (auto **p = std::any_cast<::xdg_popup *>(&role))
        get_popup(*p);
}

// ---------------------------------------------------------------------------
// Static plugin so QT_WAYLAND_SHELL_INTEGRATION=kdock-layershell resolves to
// this integration without installing anything into Qt's plugin dirs.

class KDockLayerShellPlugin : public QtWaylandClient::QWaylandShellIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QWaylandShellIntegrationFactoryInterface_iid FILE "kdock-layershell.json")
public:
    QtWaylandClient::QWaylandShellIntegration *create(const QString &key, const QStringList &paramList) override
    {
        Q_UNUSED(key);
        Q_UNUSED(paramList);
        return new LayerShellIntegration;
    }
};

#include "layershell.moc"
