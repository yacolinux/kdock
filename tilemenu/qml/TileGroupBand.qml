// TileGroupBand.qml - the header of one group: its name, the collapse toggle and
// the reorder/delete actions.
//
// The band itself is not an item: a group owns a contiguous range of rows inside
// the single canvas matrix, and this header just sits above that range. Dropping
// a tile "into a group" is therefore ordinary drag arithmetic, not a separate
// drop target.

import QtQuick
import QtQuick.Controls.Basic

Item {
    id: band

    required property var bandData // { index, title, collapsed, startRow, rows }
    property var ui                // the TileMenu root
    property int bandCount: 1

    readonly property int index: bandData.index
    readonly property string title: bandData.title
    readonly property bool collapsed: bandData.collapsed

    height: ui ? ui.headerH : 34

    function rename() {
        var name = win.promptText(qsTr("Renombrar grupo"), qsTr("Nombre:"), band.title)
        if (name !== "")
            tileLayout.renameGroup(tiles.section, band.index, name)
    }

    HoverHandler { id: hover }

    // Declared first, so everything below stacks on top of it and keeps its own
    // clicks: this one only ever sees the empty part of the header.
    MouseArea {
        anchors.fill: parent
        onDoubleClicked: band.rename()
    }

    Row {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: band.collapsed ? "▸" : "▾"
            color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.7)
            font.pixelSize: 14
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                onClicked: tileLayout.setGroupCollapsed(tiles.section, band.index,
                                                        !band.collapsed)
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: band.title.length > 0 ? band.title : qsTr("Sin nombre")
            color: band.title.length > 0
                   ? theme.foreground
                   : Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.45)
            font.pixelSize: 14
            font.bold: true
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "(" + band.bandData.rows + ")"
            color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.35)
            font.pixelSize: 11
            visible: !band.collapsed
        }
    }

    // Actions appear on hover, so an untouched canvas stays quiet.
    Row {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        opacity: hover.hovered ? 1.0 : 0.0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 120 } }

        Repeater {
            model: [
                { ic: "go-up",       act: "up" },
                { ic: "go-down",     act: "down" },
                { ic: "edit-rename", act: "rename" },
                { ic: "edit-delete", act: "remove" }
            ]
            delegate: Item {
                id: actionBtn
                required property var modelData
                width: 24
                height: 24
                // Nowhere to move a lone band, and the last one cannot go: its
                // tiles would have no home.
                readonly property bool usable: {
                    if (actionBtn.modelData.act === "up") return band.index > 0
                    if (actionBtn.modelData.act === "down") return band.index < band.bandCount - 1
                    if (actionBtn.modelData.act === "remove") return band.bandCount > 1
                    return true
                }

                Rectangle {
                    anchors.fill: parent
                    radius: 4
                    color: btn.containsMouse
                           ? Qt.rgba(theme.highlight.r, theme.highlight.g,
                                     theme.highlight.b, 0.30)
                           : "transparent"
                }
                Image {
                    anchors.centerIn: parent
                    width: 14
                    height: 14
                    opacity: actionBtn.usable ? 1.0 : 0.3
                    source: "image://icon/" + actionBtn.modelData.ic + "@" + theme.revision
                    sourceSize: Qt.size(14 * Screen.devicePixelRatio,
                                        14 * Screen.devicePixelRatio)
                }
                MouseArea {
                    id: btn
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: actionBtn.usable
                    onClicked: {
                        var act = actionBtn.modelData.act
                        if (act === "up")
                            tileLayout.moveGroup(tiles.section, band.index, band.index - 1)
                        else if (act === "down")
                            tileLayout.moveGroup(tiles.section, band.index, band.index + 1)
                        else if (act === "rename")
                            band.rename()
                        else if (act === "remove"
                                 && win.confirm(qsTr("Quitar grupo"),
                                                qsTr("Los mosaicos de este grupo pasan al grupo vecino. ¿Seguir?")))
                            tileLayout.removeGroup(tiles.section, band.index)
                    }
                }
            }
        }
    }

    // Thin rule under the header, so a band reads as a band.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottomMargin: 3
        height: 1
        color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.15)
    }
}
