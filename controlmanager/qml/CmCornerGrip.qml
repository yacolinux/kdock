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

    readonly property bool left: corner === 1 || corner === 2
    readonly property bool top: corner === 2

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
                const x1 = grip.left ? o : width - o
                const y1 = grip.top ? 0 : height
                const x2 = grip.left ? 0 : width
                const y2 = grip.top ? o : height - o
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
        cursorShape: grip.left === grip.top ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor

        property int startW: 0
        property int startH: 0
        property point startPos: Qt.point(0, 0)

        onPressed: (m) => {
            gripMouse.startW = grip.currentW
            gripMouse.startH = grip.currentH
            gripMouse.startPos = Qt.point(m.x, m.y)
        }
        onPositionChanged: (m) => {
            if (!gripMouse.pressed)
                return
            const dx = m.x - gripMouse.startPos.x
            const dy = m.y - gripMouse.startPos.y
            // The sign of each axis follows the corner being dragged: pulling a
            // right-side corner right grows the width, pulling a top corner up
            // grows the height, and so on.
            win.setPanelSize(grip.left ? gripMouse.startW - dx : gripMouse.startW + dx,
                             grip.top ? gripMouse.startH - dy : gripMouse.startH + dy)
        }
    }
}
