// TileMenuTile.qml - one tile: icon, name, its own colors, and the drag that
// moves it around the matrix.
//
// The tile never decides where it lands. It reports the slot it was dropped on
// and TileLayout answers; the model then re-asserts the x/y bindings below and
// the tile animates to wherever the engine actually put it (which, for a refused
// drop, is exactly where it started).

import QtQuick
import QtQuick.Controls.Basic

Item {
    id: tile

    // --- model roles ---
    required property string tileId
    required property string name
    required property string comment
    required property string icon
    required property bool favorite
    required property int group
    required property int col
    required property int row
    required property int absRow
    required property int span
    required property int vspan
    required property string background
    required property string image
    required property bool showIcon
    required property bool showLabel

    // --- wiring from TileMenu.qml ---
    property Item canvasItem
    property var ui // the TileMenu root: pitch, gap, cell

    property bool dragging: false
    readonly property bool selected: canvasItem && canvasItem.selectedId === tileId

    // `ui` is assigned by the Repeater while the delegate is being built, so the
    // guard is not paranoia: without it the first evaluation logs an error.
    width: ui ? span * ui.pitch - ui.gap : 0
    height: ui ? vspan * ui.pitch - ui.gap : 0
    z: dragging ? 100 : (selected ? 2 : 1)

    // While dragging, the MouseArea owns x/y; the moment it lets go these take
    // over again and the Behaviors animate the tile into its final slot.
    Binding {
        target: tile
        property: "x"
        when: !tile.dragging
        restoreMode: Binding.RestoreNone
        value: tile.canvasItem ? tile.canvasItem.xOffset + tile.col * tile.ui.pitch : 0
    }
    Binding {
        target: tile
        property: "y"
        when: !tile.dragging
        restoreMode: Binding.RestoreNone
        value: tile.canvasItem ? tile.canvasItem.tileY(tile.absRow, tile.group) : 0
    }

    Behavior on x {
        enabled: !tile.dragging
        NumberAnimation { duration: 130; easing.type: Easing.OutCubic }
    }
    Behavior on y {
        enabled: !tile.dragging
        NumberAnimation { duration: 130; easing.type: Easing.OutCubic }
    }

    // A name beside the icon only makes sense once the tile is wider than tall.
    readonly property bool sideLabel: tileConfig.labelPosition === 1 && span >= 2 && showLabel
    readonly property int iconPx: {
        var base = Math.min(tile.width, tile.height) * tileConfig.iconScale / 100
        // Leave room for the name under the icon, or a 1x1 tile clips it.
        if (tile.showLabel && !tile.sideLabel)
            base = Math.min(base, tile.height - 22)
        return Math.max(16, Math.round(base))
    }

    readonly property color tileColor: background.length > 0
        ? background
        : Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.10)
    // Same perceptual test the dock uses to pick its clock color: a custom tile
    // color can be anything, and the theme foreground would vanish on half of
    // them.
    readonly property color textColor: background.length > 0
        ? ((0.299 * tileColor.r + 0.587 * tileColor.g + 0.114 * tileColor.b) > 0.5
           ? "#141414" : "#F2F2F2")
        : theme.foreground

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: 8
        color: tile.tileColor
        clip: true

        Image {
            anchors.fill: parent
            visible: tile.image.length > 0
            source: tile.image.length > 0 ? "file://" + tile.image : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: tile.dragging || mouse.containsMouse || tile.selected ? 2 : 0
            border.color: theme.highlight
        }
    }

    // --- icon over name (the default) ---
    Column {
        anchors.centerIn: parent
        visible: !tile.sideLabel
        spacing: 3
        Image {
            visible: tile.showIcon
            anchors.horizontalCenter: parent.horizontalCenter
            width: tile.iconPx
            height: tile.iconPx
            source: "image://icon/" + tile.icon + "@" + theme.revision
            sourceSize: Qt.size(tile.iconPx * Screen.devicePixelRatio,
                                tile.iconPx * Screen.devicePixelRatio)
        }
        Text {
            visible: tile.showLabel
            width: tile.width - 10
            text: tile.name
            color: tile.textColor
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.Wrap
            font.pixelSize: 12
            font.bold: tileConfig.labelBold
        }
    }

    // --- icon beside name (wide tiles, when configured) ---
    Row {
        anchors.fill: parent
        anchors.margins: 10
        visible: tile.sideLabel
        spacing: 10
        Image {
            visible: tile.showIcon
            anchors.verticalCenter: parent.verticalCenter
            width: tile.iconPx
            height: tile.iconPx
            source: "image://icon/" + tile.icon + "@" + theme.revision
            sourceSize: Qt.size(tile.iconPx * Screen.devicePixelRatio,
                                tile.iconPx * Screen.devicePixelRatio)
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - (tile.showIcon ? tile.iconPx + parent.spacing : 0)
            text: tile.name
            color: tile.textColor
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.Wrap
            font.pixelSize: 13
            font.bold: tileConfig.labelBold
        }
    }

    // Favorite marker, top-right. Visible always when set, on hover otherwise.
    Image {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 4
        width: 14
        height: 14
        opacity: tile.favorite ? 1.0 : (mouse.containsMouse ? 0.45 : 0.0)
        source: "image://icon/" + (tile.favorite ? "starred-symbolic" : "non-starred-symbolic")
                + "@" + theme.revision
        sourceSize: Qt.size(14 * Screen.devicePixelRatio, 14 * Screen.devicePixelRatio)
    }

    // The *attached* ToolTip, not an inline one: the attached API shares a single
    // tooltip instance for the whole window, and a section like "All
    // Applications" has 450 tiles. An inline ToolTip per tile is 450 popups.
    ToolTip.text: tile.comment.length > 0 ? tile.name + " — " + tile.comment : tile.name
    ToolTip.delay: 700
    ToolTip.visible: mouse.containsMouse && !tile.dragging
                     && (tile.comment.length > 0 || !tile.showLabel)

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // Search results have no section to persist a position into, so they are
        // a hit list, not an arrangement.
        drag.target: tiles.searching ? null : tile
        drag.threshold: 8

        onPressed: if (tile.canvasItem) tile.canvasItem.selectedId = tile.tileId

        // MouseArea suppresses clicked() when a drag happened, so a dropped tile
        // never also launches its app.
        onClicked: (event) => {
            if (event.button === Qt.RightButton)
                ui.openTileMenu(tile)
            else
                win.launch(tile.tileId)
        }

        drag.onActiveChanged: {
            if (!tile.canvasItem)
                return
            if (drag.active) {
                tile.dragging = true
                tile.canvasItem.dragTile = tile
            } else {
                tile.canvasItem.dropTile()
                tile.canvasItem.dragTile = null
                tile.dragging = false
            }
        }
    }
}
