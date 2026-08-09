// The horizontal tab bar: Principal first and always, then one tab per enabled
// section, in the order the settings panel defines.
//
// Same look as the tile menu's group tabs (a rounded pill per tab, the current
// one filled with the theme highlight), because they are the same gesture.

import QtQuick
import QtQuick.Controls.Basic

Item {
    id: tabs

    // [{ id, label, icon }]; the Principal entry has an empty id.
    property var model: []
    property string currentId: ""

    signal picked(string id)

    implicitHeight: Math.round(34 * cmConfig.fontScale)

    // With eight sections the bar runs past the corner controls and the last tab
    // is simply not there (measured: "Sistema" fell off a 900 px panel). Rather
    // than rely on the Flickable — nobody discovers a bar they can drag — the
    // tabs drop their labels and keep their icons, all but the current one.
    //
    // The decision cannot read the visible row: that row's width is what it
    // decides, which is a binding loop. It reads the hidden row below instead,
    // which always draws full labels and so never depends on the answer. A
    // guessed threshold was tried first and was wrong in both directions (at
    // 96 px the bar still overflowed; at 120 it collapsed labels that fitted).
    readonly property bool crowded: measureRow.implicitWidth > tabs.width

    Row {
        id: measureRow
        visible: false
        spacing: 4
        Repeater {
            model: tabs.model
            delegate: Item {
                required property var modelData
                width: Math.max(64, measureLabel.implicitWidth + 26)
                height: 1
                Row {
                    id: measureLabel
                    spacing: 6
                    Item { width: 15; height: 1 }
                    Text {
                        text: modelData.label
                        font.pixelSize: Math.max(7, Math.round((12) * cmConfig.fontScale))
                        font.bold: cmConfig.labelBold
                    }
                }
            }
        }
    }

    Flickable {
        anchors.fill: parent
        contentWidth: row.width
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: row
            height: parent.height
            spacing: 4

            Repeater {
                model: tabs.model

                delegate: Item {
                    id: tab
                    required property var modelData
                    readonly property bool current: tabs.currentId === tab.modelData.id
                    // The current tab always spells its name out: the bar has to
                    // say where you are even when it is packed.
                    readonly property bool showLabel: !tabs.crowded || tab.current

                    width: tab.showLabel ? Math.max(64, label.implicitWidth + 26) : 34
                    height: Math.round(28 * cmConfig.fontScale)
                    anchors.verticalCenter: parent.verticalCenter

                    ToolTip.text: tab.modelData.label
                    ToolTip.visible: !tab.showLabel && tabMouse.containsMouse
                    ToolTip.delay: 500

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: tab.current
                               ? Qt.rgba(theme.highlight.r, theme.highlight.g,
                                         theme.highlight.b, 0.45)
                               : (tabMouse.containsMouse
                                  ? Qt.rgba(theme.foreground.r, theme.foreground.g,
                                            theme.foreground.b, 0.12)
                                  : Qt.rgba(theme.foreground.r, theme.foreground.g,
                                            theme.foreground.b, 0.05))
                    }

                    Row {
                        id: label
                        anchors.centerIn: parent
                        spacing: 6
                        Image {
                            // With the labels dropped the icon is the only thing
                            // left, so it stays even when icons are switched off.
                            visible: (cmConfig.showTabIcons || !tab.showLabel)
                                     && tab.modelData.icon.length > 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: 15
                            height: 15
                            source: tab.modelData.icon.length > 0
                                    ? "image://icon/" + tab.modelData.icon + win.iconSuffix
                                    : ""
                            sourceSize: Qt.size(15 * Screen.devicePixelRatio,
                                                15 * Screen.devicePixelRatio)
                        }
                        Text {
                            visible: tab.showLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: tab.modelData.label
                            color: theme.foreground
                            font.pixelSize: Math.max(7, Math.round((12) * cmConfig.fontScale))
                            font.bold: cmConfig.labelBold
                        }
                    }

                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: tabs.picked(tab.modelData.id)
                    }
                }
            }
        }
    }
}
