// One resize handle at a corner of the panel: grab it and drag to resize the
// window like a standard one. There is no compositor resize for layer-shell
// surfaces, so the handle does the whole job: it maps the pointer delta to a
// new width/height (the sign flips with the corner) and calls
// win.setPanelSize(), which stores pixels — clearing any configured
// percentage, because a drag writes the size the user is looking at.

import QtQuick
import QtQuick.Controls.Basic

Item {
    id: grip

    // 0 bottom-right, 1 bottom-left, 2 top-left (top-right is covered by the
    // corner controls of the panel, so it has no handle).
    property int corner: 0
    // The current panel size, from the root: the drag starts from these.
    property int currentW: 0
    property int currentH: 0

    width: 18
    height: 18
    z: 60

    // Not named left/top: those are Item's FINAL anchor-line properties and
    // overriding them fails the whole component load (mordió 2026-08-09: el
    // panel quedó en blanco y el diálogo de configuración inalcanzable debajo
    // de la superficie vacía).
    readonly property bool isLeft: corner === 1 || corner === 2
    readonly property bool isTop: corner === 2

    Canvas {
        id: gripCanvas
        anchors.fill: parent
        antialiasing: true
        opacity: gripMouse.containsMouse ? 0.9 : 0.4
        onPaint: {
            const c = getContext("2d")
            c.clearRect(0, 0, width, height)
            c.strokeStyle = theme.foreground
            c.lineWidth = 2
            c.beginPath()
            for (let i = 0; i < 3; ++i) {
                // Three short parallel lines fanning from the corner.
                const o = 4 + i * 4
                const x1 = grip.isLeft ? o : width - o
                const y1 = grip.isTop ? 0 : height
                const x2 = grip.isLeft ? 0 : width
                const y2 = grip.isTop ? o : height - o
                c.moveTo(x1, y1)
                c.lineTo(x2, y2)
            }
            c.stroke()
        }
        Connections {
            target: theme
            function onRevisionChanged() { gripCanvas.requestPaint() }
        }
    }

    MouseArea {
        id: gripMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: grip.isLeft === grip.isTop ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor

        // The size at the press, and the size that was last applied: the drag is
        // absolute (every event recomputes from the press), so a delta that got
        // clamped by setPanelSize does not accumulate.
        property int startW: 0
        property int startH: 0
        property int appliedW: 0
        property int appliedH: 0
        // The press point in SCENE coordinates, i.e. relative to the surface,
        // not to this grip. Measuring against the grip is what made the drag
        // erratic: the grip is anchored to the corner it resizes, so it moves
        // with every resize it causes and the next delta reads the panel's own
        // motion as pointer motion — the size ran away to the minimum or the
        // maximum in a handful of events.
        property point startPos: Qt.point(0, 0)

        onPressed: (m) => {
            gripMouse.startW = grip.currentW
            gripMouse.startH = grip.currentH
            gripMouse.appliedW = grip.currentW
            gripMouse.appliedH = grip.currentH
            gripMouse.startPos = gripMouse.mapToItem(null, m.x, m.y)
        }
        onPositionChanged: (m) => {
            if (!gripMouse.pressed)
                return
            const p = gripMouse.mapToItem(null, m.x, m.y)
            // Surface-local motion is the pointer's screen motion MINUS however
            // far the compositor moved the surface while resizing it, and that
            // second part is exactly originShift * (size applied so far). A
            // layer-shell client is never told where its surface is, but this it
            // can reconstruct, so the pointer delta comes back exact.
            const dx = (p.x - gripMouse.startPos.x)
                       + win.originShiftX * (gripMouse.appliedW - gripMouse.startW)
            const dy = (p.y - gripMouse.startPos.y)
                       + win.originShiftY * (gripMouse.appliedH - gripMouse.startH)
            // Keep the grabbed corner under the pointer: the edge being dragged
            // travels (originShift + 1) px per px of growth on the right/bottom
            // side and originShift on the left/top one. When that factor is ~0
            // the edge is the pinned one and simply cannot follow the pointer —
            // there the panel grows away from it, one pixel per pixel, which is
            // the old 1:1 behaviour.
            const kx = win.originShiftX + (grip.isLeft ? 0 : 1)
            const ky = win.originShiftY + (grip.isTop ? 0 : 1)
            const w = gripMouse.startW
                      + (Math.abs(kx) < 0.25 ? (grip.isLeft ? -dx : dx) : dx / kx)
            const h = gripMouse.startH
                      + (Math.abs(ky) < 0.25 ? (grip.isTop ? -dy : dy) : dy / ky)
            win.setPanelSize(Math.round(w), Math.round(h))
            // What setPanelSize actually stored, which is clamped: the next
            // event has to compensate for the size the panel really has.
            gripMouse.appliedW = cmConfig.panelWidth
            gripMouse.appliedH = cmConfig.panelHeight
        }
    }
}
