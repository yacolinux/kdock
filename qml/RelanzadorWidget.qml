// RelanzadorWidget.qml - Icon widget for a Relanzador in the main dock.
// Shows an icon that opens a popup with launchers when clicked.

import QtQuick
import QtQuick.Controls.Basic

Item {
    id: relanzadorWidget

    required property var relanzador  // RelanzadorConfig
    required property int iconSize
    required property int spacing
    required property bool horizontal
    required property var theme
    required property var config
    required property var apps  // DesktopEntryIndex
    property var relanzadores: null  // RelanzadoresManager

    width: iconSize
    height: iconSize

    Image {
        id: icon
        // Fill the whole cell, matching the app launchers' icon size.
        anchors.fill: parent
        source: (relanzador && relanzador.iconName) ? "image://icon/" + relanzador.iconName + "@" + theme.revision : ""
        sourceSize: Qt.size(iconSize * Screen.devicePixelRatio, iconSize * Screen.devicePixelRatio)
        scale: mouseArea.containsMouse ? 1.12 : 1.0
        Behavior on scale { NumberAnimation { duration: 120 } }
    }

    ToolTip {
        popupType: Popup.Window
        visible: mouseArea.containsMouse && !popup.visible
        delay: 400
        text: relanzador ? relanzador.title : ""
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                popup.visible ? popup.close() : popup.open()
            } else if (mouse.button === Qt.RightButton) {
                // Context menu for relanzador could be added here
            }
        }
    }

    // Popup with the relanzador's launchers
    RelanzadorPopup {
        id: popup
        relanzador: relanzadorWidget.relanzador
        iconSize: relanzadorWidget.iconSize
        iconSpacing: relanzadorWidget.spacing
        horizontal: relanzadorWidget.horizontal
        theme: relanzadorWidget.theme
        config: relanzadorWidget.config
        apps: relanzadorWidget.apps
        relanzadores: relanzadorWidget.relanzadores
        parent: relanzadorWidget.parent  // dock's content area
        x: calculatePopupX()
        y: calculatePopupY()
    }

    function calculatePopupX() {
        if (!config) return 0
        if (horizontal) {
            // Position centered on the icon horizontally
            var iconCenter = relanzadorWidget.x + relanzadorWidget.width / 2
            return iconCenter - popup.width / 2
        } else {
            // Vertical dock: popup opens to the side
            if (config.edge === 2) { // Left edge
                return relanzadorWidget.x + relanzadorWidget.width + spacing
            } else { // Right edge
                return relanzadorWidget.x - popup.width - spacing
            }
        }
    }

    function calculatePopupY() {
        if (!config) return 0
        if (horizontal) {
            // Horizontal dock: popup opens above or below
            if (config.edge === 0) { // Bottom edge
                return relanzadorWidget.y - popup.height - spacing
            } else { // Top edge
                return relanzadorWidget.y + relanzadorWidget.height + spacing
            }
        } else {
            // Vertical dock: position centered on the icon vertically
            var iconCenter = relanzadorWidget.y + relanzadorWidget.height / 2
            return iconCenter - popup.height / 2
        }
    }
}