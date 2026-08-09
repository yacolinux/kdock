// One card on the Principal grid: frame, title, its section's content, and the
// drag that moves it around the matrix.
//
// The card never decides where it lands. It reports the slot it was dropped on
// and CmLayout answers; the model then re-asserts the x/y bindings below and the
// card animates to wherever the engine actually put it (which, for a refused
// drop, is exactly where it started). Same contract as TileMenuTile.

import QtQuick
import QtQuick.Controls.Basic

Item {
    id: card

    // --- model roles ---
    required property string cardId
    required property string name
    required property string icon
    required property int col
    required property int row
    required property int span
    required property int vspan
    required property string background
    required property bool showTitle

    // --- wiring from ControlManager.qml ---
    property Item canvasItem
    property var ui // the root: pitch, gap, cell

    property bool dragging: false

    // `ui` is assigned by the Repeater while the delegate is being built, so the
    // guard is not paranoia: without it the first evaluation logs an error.
    width: ui ? span * ui.pitchW - ui.gap : 0
    height: ui ? vspan * ui.pitchH - ui.gap : 0
    z: dragging ? 100 : 1

    // While dragging, the MouseArea owns x/y; the moment it lets go these take
    // over again and the Behaviors animate the card into its final slot.
    Binding {
        target: card
        property: "x"
        when: !card.dragging
        restoreMode: Binding.RestoreNone
        value: card.canvasItem ? card.canvasItem.xOffset + card.col * card.ui.pitchW : 0
    }
    Binding {
        target: card
        property: "y"
        when: !card.dragging
        restoreMode: Binding.RestoreNone
        value: card.canvasItem ? card.row * card.ui.pitchH : 0
    }

    Behavior on x {
        enabled: !card.dragging
        NumberAnimation { duration: 130; easing.type: Easing.OutCubic }
    }
    Behavior on y {
        enabled: !card.dragging
        NumberAnimation { duration: 130; easing.type: Easing.OutCubic }
    }

    readonly property color cardColor: background.length > 0
        ? background
        : Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.08)

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: 10
        color: card.cardColor
        clip: true

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: card.dragging || headerMouse.containsMouse ? 2 : 0
            border.color: theme.highlight
        }
    }

    // --- header: the grab handle, and the only thing that drags -------------
    // Dragging from anywhere would fight the sliders and buttons inside the
    // card; the title bar is the handle, which is also where a user reaches for
    // it.
    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: card.showTitle ? Math.round(22 * cmConfig.fontScale) : 10

        Image {
            id: headerIcon
            visible: card.showTitle
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 13
            height: 13
            source: "image://icon/" + card.icon + win.iconSuffix
            sourceSize: Qt.size(13 * Screen.devicePixelRatio, 13 * Screen.devicePixelRatio)
            opacity: 0.75
        }
        Text {
            visible: card.showTitle
            anchors.left: headerIcon.right
            anchors.leftMargin: 6
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: card.name
            color: theme.foreground
            opacity: 0.75
            elide: Text.ElideRight
            font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
            font.bold: cmConfig.labelBold
        }

        MouseArea {
            id: headerMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            drag.target: card
            drag.threshold: 6
            cursorShape: card.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor

            // Where the pointer is, in root coordinates. Only used by the ghost,
            // which follows the grab point rather than the card's centre.
            onPositionChanged: (event) => {
                if (card.dragging && card.ui)
                    card.ui.dragPointer = card.mapToItem(card.ui, event.x, event.y)
            }

            onClicked: (event) => {
                if (event.button === Qt.RightButton)
                    card.ui.openCardMenu(card)
            }
            onDoubleClicked: win.currentTab = card.cardId

            drag.onActiveChanged: {
                if (!card.canvasItem)
                    return
                if (drag.active) {
                    card.dragging = true
                    card.canvasItem.dragCard = card
                } else {
                    card.canvasItem.dropCard()
                    card.canvasItem.dragCard = null
                    card.dragging = false
                }
            }
        }
    }

    // --- content ------------------------------------------------------------
    CmSectionView {
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.bottomMargin: 8
        clip: true
        sectionId: card.cardId
        compact: true
    }
}
