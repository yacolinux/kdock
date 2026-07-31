// ScriptRunnerWidget.qml - Icon widget for a Script Runner in the main dock.
// Clicking it runs the configured shell script via the manager.

import QtQuick
import QtQuick.Controls.Basic

Item {
    id: scriptRunnerWidget

    required property var scriptRunner   // ScriptRunnerConfig
    required property int iconSize
    required property var theme
    property var scriptRunners: null     // ScriptRunnersManager
    property string screenName: ""        // dock's monitor connector (KDOCK_SCREEN)

    width: iconSize
    height: iconSize

    Image {
        id: icon
        anchors.centerIn: parent
        width: Math.round(parent.width * 0.85)
        height: width
        source: (scriptRunner && scriptRunner.iconName)
                ? "image://icon/" + scriptRunner.iconName + "@" + theme.revision
                : "image://icon/utilities-terminal@" + theme.revision
        sourceSize: Qt.size(iconSize * Screen.devicePixelRatio, iconSize * Screen.devicePixelRatio)
        scale: mouseArea.pressed ? 0.9 : (mouseArea.containsMouse ? 1.12 : 1.0)
        Behavior on scale { NumberAnimation { duration: 120 } }
    }

    ToolTip {
        popupType: Popup.Window
        visible: mouseArea.containsMouse
        delay: 400
        text: scriptRunner ? scriptRunner.title : ""
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onClicked: {
            if (scriptRunners && scriptRunner)
                scriptRunners.run(scriptRunner.id, screenName)
        }
    }
}
