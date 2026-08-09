// Big clock. The one card-only section: a whole tab showing nothing but the
// time would be a tab wasted.
//
// The timer runs at 1 s but only while the panel is visible — a hidden panel
// still has its QML alive (it is hide(), never destroy()), and a clock ticking
// behind a surface nobody can see is a wakeup per second for nothing.

import QtQuick

Item {
    id: card

    property bool compact: false

    property date now: new Date()

    // The attached property has to be read *here*, on an Item. Reading it from
    // inside the Timer attaches Window to the Timer itself, which logs
    // "Window.window does only support types deriving from Item" and leaves the
    // clock frozen (caught by the Xvfb harness on the first run).
    readonly property bool panelVisible: Window.window ? Window.window.visible : true

    Timer {
        interval: 1000
        repeat: true
        running: card.panelVisible
        onTriggered: card.now = new Date()
    }

    Column {
        anchors.centerIn: parent
        spacing: card.compact ? 0 : 6

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatDateTime(card.now, "HH:mm")
            color: theme.foreground
            font.pixelSize: Math.max(7, Math.round((Math.max(20, Math.min(card.height * 0.42, card.width * 0.30))) * cmConfig.fontScale))
            font.bold: true
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatDateTime(card.now, "ddd d MMM yyyy")
            color: theme.foreground
            opacity: 0.7
            font.pixelSize: Math.max(7, Math.round((Math.max(10, Math.min(card.height * 0.13, card.width * 0.10))) * cmConfig.fontScale))
            font.bold: cmConfig.labelBold
        }
        Text {
            visible: !card.compact && card.height > 150
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatDateTime(card.now, "ss") + " s"
            color: theme.foreground
            opacity: 0.45
            font.pixelSize: Math.max(7, Math.round((12) * cmConfig.fontScale))
        }
    }
}
