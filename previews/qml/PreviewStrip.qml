// One strip of window previews on a screen edge.
//
// Sizing follows the same contract as kdock's Dock.qml: the cross axis is
// config.stripThicknessPx (computed in C++, shared with the layer-shell
// exclusive zone) and the main axis either follows Window.width/height (the
// compositor stretched the surface across the whole edge) or a percentage of the
// screen. The autohide slide is the same construction too — the surface stays
// where it is and the content translates out of it, and only when the animation
// finishes does C++ shrink the input mask.

import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root

    readonly property bool horizontal: config.edge === 0 || config.edge === 1
    // The panel hugs the cards: the auto-fit shrinks the cross size along with
    // the cards, so the strip never shows dead bands on either side of the
    // thumbnails (bug 2026-07-31). At full size this equals config.stripThicknessPx.
    readonly property int thickness: previews ? previews.effectiveThicknessPx
                                              : config.stripThicknessPx
    readonly property int pad: config.pad
    readonly property color baseColor: config.panelColorSet ? config.panelColor : theme.background

    readonly property int fixedLength: {
        if (config.stripLength <= 0)
            return 0
        const screenMain = horizontal ? Screen.width : Screen.height
        return Math.round(screenMain * config.stripLength / 100)
    }

    // With stripLength = 0 the surface is anchored to both side edges, so the
    // compositor stretches it across the whole screen edge and cannot align it:
    // the alignment has to move the *cards* inside the strip instead (same split
    // as Dock.qml, which aligns the row itself in panel mode). It is applied as
    // the ListView's leading margin below. With a fixed length the layer-shell
    // anchors already place the surface and this only centers what is left over
    // inside it.
    //
    // The cards' extent comes from the model, deliberately *not* from
    // list.contentWidth/contentHeight: those include the very margin the
    // alignment writes, which closes a binding loop (bug 2026-07-31 — invisible
    // under the Xvfb harness, where there are no windows and so no content).
    readonly property int contentLength: previews ? previews.contentLengthPx : 0
    readonly property int availableLength: horizontal ? list.width : list.height
    readonly property bool overflowing: contentLength > availableLength

    readonly property int alignOffset: {
        if (config.alignment === 0) // Start: the ListView's natural origin.
            return 0
        const slack = availableLength - contentLength
        if (slack <= 0) // Overflowing: it scrolls, there is nothing to align.
            return 0
        return config.alignment === 2 ? slack : Math.round(slack / 2)
    }

    property bool menuOpen: false
    readonly property bool revealed: !config.autohide || stripHover.hovered || menuOpen
    onRevealedChanged: if (revealed) previewWindow.setHidden(false)
    // With autohide already on at startup the slide below is the *initial* value
    // of the binding, not an animation, so onRunningChanged never fires and the
    // input region would stay full-size: the strip would be off-screen but still
    // swallow every click over its area.
    Component.onCompleted: if (!revealed) previewWindow.setHidden(true)

    width: horizontal
           ? (config.stripLength > 0 ? Math.max(fixedLength, thickness)
                                     : Math.max(Window.width, thickness))
           : thickness
    height: horizontal
            ? thickness
            : (config.stripLength > 0 ? Math.max(fixedLength, thickness)
                                      : Math.max(Window.height, thickness))

    HoverHandler {
        id: stripHover
    }

    Item {
        id: slider
        width: root.width
        height: root.height

        readonly property int hideDistance: root.thickness + config.screenMargin

        x: {
            if (root.revealed || root.horizontal)
                return 0
            return config.edge === 2 ? -hideDistance : hideDistance
        }
        y: {
            if (root.revealed || !root.horizontal)
                return 0
            return config.edge === 1 ? -hideDistance : hideDistance
        }

        Behavior on x {
            NumberAnimation {
                duration: 200
                easing.type: Easing.InOutQuad
                onRunningChanged: if (!running && !root.revealed) previewWindow.setHidden(true)
            }
        }
        Behavior on y {
            NumberAnimation {
                duration: 200
                easing.type: Easing.InOutQuad
                onRunningChanged: if (!running && !root.revealed) previewWindow.setHidden(true)
            }
        }

        Rectangle {
            id: background
            anchors.fill: parent
            // Rounded only when the strip does not span the whole edge, same rule
            // as the dock's panel mode.
            radius: config.stripLength > 0 ? 12 : 0
            color: Qt.rgba(root.baseColor.r, root.baseColor.g, root.baseColor.b, config.opacity)
            border.width: config.stripLength > 0 ? 1 : 0
            border.color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.18)
        }

        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: root.pad
            orientation: root.horizontal ? ListView.Horizontal : ListView.Vertical
            spacing: config.cardSpacing
            leftMargin: root.horizontal ? root.alignOffset : 0
            topMargin: root.horizontal ? 0 : root.alignOffset
            clip: true
            model: previews
            // Cards are large: flicking with a wheel or a drag both make sense.
            boundsBehavior: Flickable.StopAtBounds
            // Drags glide further and a hard flick goes faster.
            flickDeceleration: 800
            maximumFlickVelocity: 6000

            delegate: Item {
                // ListView only injects the model roles into the delegate *root*,
                // so the wrapper must receive them and hand them on to the card —
                // a bare nested PreviewCard leaves its required properties
                // uninitialized and every delegate fails to be created (the strip
                // then shows just the empty panel, bug 2026-07-31).
                required property int index
                required property string uuid
                required property string thumbId
                required property string title
                required property string appName
                required property string iconName
                required property int thumbRevision
                required property real aspect
                required property bool active
                required property bool minimized

                // The cross axis fills the viewport so a card shrunk by the
                // auto-fit stays centered in the strip instead of hugging an
                // edge; the card's own extent drives the main axis.
                height: root.horizontal ? list.height : card.height
                width: root.horizontal ? card.width : list.width

                PreviewCard {
                    id: card
                    anchors.centerIn: parent
                    crossSize: Math.round(config.cardWidthPx * previews.cardScale)
                    horizontal: root.horizontal
                    showTitle: config.showTitles

                    index: parent.index
                    uuid: parent.uuid
                    thumbId: parent.thumbId
                    title: parent.title
                    appName: parent.appName
                    iconName: parent.iconName
                    thumbRevision: parent.thumbRevision
                    aspect: parent.aspect
                    active: parent.active
                    minimized: parent.minimized
                }
            }

            // The auto-fit shrinks the cards until their total main-axis extent
            // fits in the viewport, so it needs to know how much room there is.
            function reportAvailable() {
                if (previews)
                    previews.setAvailableLength(root.horizontal ? width : height)
            }

            onOrientationChanged: reportAvailable()

            // Only the cards actually on screen are worth capturing; the model's
            // scheduler skips everything outside this range.
            function reportRange() {
                if (!previews)
                    return
                if (count === 0) {
                    previews.setVisibleRange(0, -1)
                    return
                }
                const midX = contentX + width / 2
                const midY = contentY + height / 2
                let first = root.horizontal ? indexAt(contentX + 1, midY)
                                            : indexAt(midX, contentY + 1)
                let last = root.horizontal ? indexAt(contentX + width - 1, midY)
                                           : indexAt(midX, contentY + height - 1)
                if (first < 0)
                    first = 0
                if (last < 0)
                    last = count - 1
                previews.setVisibleRange(first, last)
            }

            onContentXChanged: reportRange()
            onContentYChanged: reportRange()
            onCountChanged: reportRange()
            onWidthChanged: { reportRange(); reportAvailable() }
            onHeightChanged: { reportRange(); reportAvailable() }
            Component.onCompleted: {
                reportRange()
                reportAvailable()
            }
        }

        // Thin scroll indicator, opt-in (config.showScrollBar), on the strip's
        // *inner* edge — the side opposite the screen edge — so it never sits
        // where autohide's hover strip is. A plain attached ScrollBar cannot
        // be placed there: QQuickFlickableScrollBar pins it to the bottom/right
        // of the viewport, where it would cover the cards. This bar lives in
        // the strip's padding band and mirrors list.visibleArea.
        Rectangle {
            id: scrollTrackH
            visible: root.horizontal && config.showScrollBar && root.overflowing
            anchors.top: config.edge === 0 ? background.top : undefined
            anchors.bottom: config.edge === 1 ? background.bottom : undefined
            anchors.left: background.left
            anchors.right: background.right
            anchors.leftMargin: root.pad
            anchors.rightMargin: root.pad
            height: 3
            color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.12)
            radius: height / 2

            Rectangle {
                id: handleH
                x: parent.width * list.visibleArea.xPosition
                width: Math.max(24, parent.width * list.visibleArea.widthRatio)
                height: parent.height
                color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.5)
                radius: height / 2
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                function apply(mx) {
                    const avail = parent.width - handleH.width
                    if (avail <= 0)
                        return
                    const frac = Math.max(0, Math.min(1, (mx - handleH.width / 2) / avail))
                    list.contentX = frac * (list.contentWidth - list.width)
                }
                onPressed: (m) => apply(m.x)
                onPositionChanged: (m) => { if (pressed) apply(m.x) }
            }
        }

        Rectangle {
            id: scrollTrackV
            visible: !root.horizontal && config.showScrollBar && root.overflowing
            anchors.left: config.edge === 3 ? background.left : undefined
            anchors.right: config.edge === 2 ? background.right : undefined
            anchors.top: background.top
            anchors.bottom: background.bottom
            anchors.topMargin: root.pad
            anchors.bottomMargin: root.pad
            width: 3
            color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.12)
            radius: width / 2

            Rectangle {
                id: handleV
                y: parent.height * list.visibleArea.yPosition
                width: parent.width
                height: Math.max(24, parent.height * list.visibleArea.heightRatio)
                color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.5)
                radius: width / 2
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                function apply(my) {
                    const avail = parent.height - handleV.height
                    if (avail <= 0)
                        return
                    const frac = Math.max(0, Math.min(1, (my - handleV.height / 2) / avail))
                    list.contentY = frac * (list.contentHeight - list.height)
                }
                onPressed: (m) => apply(m.y)
                onPositionChanged: (m) => { if (pressed) apply(m.y) }
            }
        }

        // Qt 6's Flickable never crosses axes: a horizontal ListView only
        // scrolls on angleDelta().x, and a plain mouse wheel is vertical, so a
        // top/bottom strip could not be wheel-scrolled at all (bug 2026-07-31).
        // A sibling WheelHandler does not fix it — the ListView is consulted
        // before its parent's handlers and eats the event — so the wheel is
        // caught *above* the list instead: this MouseArea is declared last, so
        // it is topmost and sees the wheel first. acceptedButtons: NoButton
        // lets presses fall through (card clicks, drag-to-flick, the
        // right-click menu) and hoverEnabled stays off, so autohide's
        // HoverHandler is untouched.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton

            // Wheel notches land faster than the glide, so the target
            // accumulates instead of restarting from the current contentX.
            property real scrollTarget: 0

            NumberAnimation {
                id: wheelGlide
                target: list
                property: "contentX"
                duration: 150
                easing.type: Easing.OutQuad
            }

            onWheel: (wheel) => {
                // Vertical strips flick natively, with Flickable's own
                // timeline: let the event through untouched.
                if (!root.horizontal || !root.overflowing) {
                    wheel.accepted = false
                    return
                }
                const delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y
                                                       : wheel.angleDelta.x
                if (delta === 0) {
                    wheel.accepted = false
                    return
                }
                // Writing contentX by hand bypasses boundsBehavior, hence the
                // explicit clamp. Wheel up == earlier cards, the same direction
                // as the vertical strips.
                const from = wheelGlide.running ? scrollTarget : list.contentX
                const step = Math.max(60, list.width * 0.3) * (delta / 120)
                scrollTarget = Math.max(0, Math.min(list.contentWidth - list.width,
                                                    from - step))
                wheelGlide.stop()
                wheelGlide.from = list.contentX
                wheelGlide.to = scrollTarget
                wheelGlide.start()
                wheel.accepted = true
            }
        }

        // Nothing open: say so instead of showing an empty rectangle.
        Text {
            anchors.centerIn: parent
            visible: list.count === 0
            width: parent.width - 2 * root.pad
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Sin ventanas")
            color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.5)
            font.pixelSize: 12
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: (event) => {
                stripMenu.x = event.position.x
                stripMenu.y = event.position.y
                stripMenu.open()
            }
        }

        StripMenu {
            id: stripMenu
            onOpened: root.menuOpen = true
            onClosed: root.menuOpen = false
        }
    }
}
