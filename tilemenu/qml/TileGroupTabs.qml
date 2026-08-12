// TileGroupTabs.qml - one tab per group of the current section, along whichever
// edge of the canvas the config asks for.
//
// The bar is what replaced the stacked bands: a section still owns a list of
// groups, but only one is on screen at a time. It appears at all only when there
// is more than one group — a single tab says nothing and eats the space.
//
// It doubles as a drop target: a tile dragged over a tab moves to that group,
// which is the only drag route left now that the other groups are off screen.

import QtQuick
import QtQuick.Controls

Item {
    id: tabs

    // tiles.groups: [{ index, title, tiles, rows }]
    property var model: []
    property int currentIndex: 0
    // Tab the dragged tile is currently over, or -1. Driven from TileMenu.
    property int dropTarget: -1
    readonly property bool vertical: tabs.side >= 2
    // 0 arriba, 1 abajo, 2 izquierda, 3 derecha.
    property int side: 0

    signal picked(int index)
    signal addRequested()
    signal renameRequested(int index)
    signal removeRequested(int index)
    signal moveRequested(int index, int to)

    implicitHeight: vertical ? 0 : 36
    implicitWidth: vertical ? 170 : 0

    // Index of the tab under (x, y) in this item's coordinates, or -1. Used for
    // the drag: the dragged tile is clipped by the canvas, but its logical
    // position keeps moving, so this answers correctly even off screen.
    function tabAt(x, y) {
        if (x < 0 || y < 0 || x > tabs.width || y > tabs.height)
            return -1
        for (var i = 0; i < repeater.count; ++i) {
            var item = repeater.itemAt(i)
            if (!item)
                continue
            var p = tabs.mapToItem(item, x, y)
            if (p.x >= 0 && p.y >= 0 && p.x <= item.width && p.y <= item.height)
                return item.tabIndex
        }
        return -1
    }

    Flickable {
        anchors.fill: parent
        contentWidth: flow.width
        contentHeight: flow.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Grid {
            id: flow
            columns: tabs.vertical ? 1 : repeater.count + 1
            rows: tabs.vertical ? repeater.count + 1 : 1
            spacing: 4

            Repeater {
                id: repeater
                model: tabs.model

                delegate: Item {
                    id: tab
                    required property var modelData
                    readonly property int tabIndex: modelData.index
                    readonly property bool current: tabs.currentIndex === tab.tabIndex
                    readonly property bool dropping: tabs.dropTarget === tab.tabIndex

                    width: tabs.vertical ? tabs.width - 8
                                         : Math.max(70, label.implicitWidth + 34)
                    height: 30

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: tab.dropping
                               ? Qt.rgba(theme.highlight.r, theme.highlight.g,
                                         theme.highlight.b, 0.75)
                               : tab.current
                                 ? Qt.rgba(theme.highlight.r, theme.highlight.g,
                                           theme.highlight.b, 0.45)
                                 : (tabMouse.containsMouse
                                    ? Qt.rgba(theme.foreground.r, theme.foreground.g,
                                              theme.foreground.b, 0.12)
                                    : Qt.rgba(theme.foreground.r, theme.foreground.g,
                                              theme.foreground.b, 0.05))
                        border.width: tab.dropping ? 2 : 0
                        border.color: theme.foreground
                    }

                    Row {
                        id: label
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: tab.modelData.title.length > 0
                                  ? tab.modelData.title
                                  : qsTr("Grupo %1").arg(tab.tabIndex + 1)
                            color: theme.foreground
                            font.pixelSize: 13
                            font.bold: tileConfig.labelBold
                            elide: Text.ElideRight
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: tab.modelData.tiles
                            color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                           theme.foreground.b, 0.45)
                            font.pixelSize: 11
                        }
                    }

                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: (event) => {
                            if (event.button === Qt.RightButton)
                                tabMenu.popup()
                            else
                                tabs.picked(tab.tabIndex)
                        }
                        onDoubleClicked: tabs.renameRequested(tab.tabIndex)
                    }

                    Menu {
                        id: tabMenu
                        popupType: Popup.Window
                        width: Math.max(implicitWidth + 16, 200)
                        IconMenuItem {
                            text: qsTr("Renombrar…")
                            iconName: "edit-rename"
                            onTriggered: tabs.renameRequested(tab.tabIndex)
                        }
                        IconMenuItem {
                            text: tabs.vertical ? qsTr("Subir") : qsTr("Mover a la izquierda")
                            iconName: tabs.vertical ? "go-up" : "go-previous"
                            enabled: tab.tabIndex > 0
                            onTriggered: tabs.moveRequested(tab.tabIndex, tab.tabIndex - 1)
                        }
                        IconMenuItem {
                            text: tabs.vertical ? qsTr("Bajar") : qsTr("Mover a la derecha")
                            iconName: tabs.vertical ? "go-down" : "go-next"
                            enabled: tab.tabIndex < repeater.count - 1
                            onTriggered: tabs.moveRequested(tab.tabIndex, tab.tabIndex + 1)
                        }
                        MenuSeparator {}
                        IconMenuItem {
                            text: qsTr("Nuevo grupo…")
                            iconName: "list-add"
                            onTriggered: tabs.addRequested()
                        }
                        IconMenuItem {
                            text: qsTr("Quitar grupo")
                            iconName: "edit-delete"
                            // The last one has to stay: its tiles would have
                            // nowhere to live.
                            enabled: repeater.count > 1
                            onTriggered: tabs.removeRequested(tab.tabIndex)
                        }
                    }
                }
            }

            // "+" — the discoverable way to add a group, next to the tabs.
            Item {
                width: tabs.vertical ? tabs.width - 8 : 30
                height: 30
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: addMouse.containsMouse
                           ? Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.3)
                           : "transparent"
                }
                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                   theme.foreground.b, 0.7)
                    font.pixelSize: 17
                }
                ToolTip.text: qsTr("Nuevo grupo")
                ToolTip.visible: addMouse.containsMouse
                ToolTip.delay: 500
                MouseArea {
                    id: addMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: tabs.addRequested()
                }
            }
        }
    }
}
