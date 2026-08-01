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
