// A month calendar, drawn with QtQuick.Controls' MonthGrid.
//
// It does *not* embed kdock-calendar: that binary is Qt Widgets and cannot live
// inside a QQuickView. The button opens it as the separate window it is.

import QtQuick
import QtQuick.Controls.Basic
import ".."

Item {
    id: card

    property bool compact: false

    readonly property date today: new Date()
    property int shownMonth: today.getMonth()
    property int shownYear: today.getFullYear()

    function step(delta) {
        var m = card.shownMonth + delta
        var y = card.shownYear
        while (m < 0) { m += 12; y -= 1 }
        while (m > 11) { m -= 12; y += 1 }
        card.shownMonth = m
        card.shownYear = y
    }

    Column {
        anchors.fill: parent
        spacing: 4

        // --- header: month, year and the two arrows ---
        Item {
            width: parent.width
            height: 24

            CmButton {
                id: prev
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                compact: true
                icon: "go-previous"
                onClicked: card.step(-1)
            }
            Text {
                anchors.centerIn: parent
                text: Qt.locale().standaloneMonthName(card.shownMonth) + " " + card.shownYear
                color: theme.foreground
                font.pixelSize: Math.max(7, Math.round((card.compact ? 12 : 14) * cmConfig.fontScale))
                font.bold: true
            }
            CmButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                compact: true
                icon: "go-next"
                onClicked: card.step(1)
            }
        }

        DayOfWeekRow {
            width: parent.width
            height: 16
            locale: Qt.locale()
            delegate: Text {
                required property string shortName
                text: shortName
                color: theme.foreground
                opacity: 0.5
                font.pixelSize: Math.max(7, Math.round((10) * cmConfig.fontScale))
                horizontalAlignment: Text.AlignHCenter
            }
        }

        MonthGrid {
            id: grid
            width: parent.width
            height: parent.height - y
            month: card.shownMonth
            year: card.shownYear
            locale: Qt.locale()
            spacing: 1

            delegate: Item {
                id: cell
                required property var model
                readonly property bool isToday: cell.model.today
                readonly property bool thisMonth: cell.model.month === grid.month

                Rectangle {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height) - 2
                    height: width
                    radius: width / 2
                    visible: cell.isToday
                    color: theme.highlight
                    opacity: 0.85
                }
                Text {
                    anchors.centerIn: parent
                    text: cell.model.day
                    color: cell.isToday ? theme.background : theme.foreground
                    opacity: cell.thisMonth ? 1.0 : 0.30
                    font.pixelSize: Math.max(7, Math.round((card.compact ? 10 : 12) * cmConfig.fontScale))
                    font.bold: cell.isToday
                }
            }
        }
    }

    // Only in the full tab: a card this small has no room for it, and the whole
    // point of the compact one is the dates.
    CmButton {
        visible: !card.compact
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        compact: true
        icon: "office-calendar"
        label: qsTr("Abrir calendario")
        tip: qsTr("Abre kdock-calendar, que es una ventana aparte")
        onClicked: win.runCommand("kdock-calendar")
    }
}
