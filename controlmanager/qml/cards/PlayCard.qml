// What is playing, and its transport controls (MPRIS2, see MprisControl).
//
// The position poll only runs while this card is on screen: it is one D-Bus
// round trip per second, and a hidden panel keeps its QML alive.

import QtQuick
import QtQuick.Controls.Basic
import ".."

Item {
    id: card

    property bool compact: false
    // Text colour, pushed in by CmSectionView: the card's contrast answer on
    // Principal, the panel's on a full tab. Every text of this file uses it.
    property color fg: theme.foreground

    readonly property bool have: mpris && mpris.available
    // Microseconds to m:ss.
    function fmt(us) {
        if (us <= 0)
            return "0:00"
        var total = Math.floor(us / 1000000)
        var m = Math.floor(total / 60)
        var s = total % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    // Ask for the poll while visible, and give it back when the card goes away
    // (a tab switch destroys the Loader's item).
    Component.onCompleted: if (mpris) mpris.setMonitoring(true)
    Component.onDestruction: if (mpris) mpris.setMonitoring(false)

    Text {
        anchors.centerIn: parent
        visible: !card.have
        text: qsTr("No hay nada reproduciéndose")
        color: card.fg
        opacity: 0.55
        font.pixelSize: Math.max(7, Math.round((12) * cmConfig.fontScale))
    }

    Column {
        anchors.fill: parent
        anchors.margins: 2
        spacing: card.compact ? 4 : 8
        visible: card.have

        // --- cover + titles ---
        Item {
            width: parent.width
            height: card.compact ? Math.max(34, card.height * 0.34) : Math.max(56, card.height * 0.32)

            Image {
                id: art
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: visible ? height : 0
                height: parent.height
                visible: mpris && mpris.artUrl.length > 0
                source: mpris ? mpris.artUrl : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }

            Column {
                anchors.left: art.visible ? art.right : parent.left
                anchors.leftMargin: art.visible ? 10 : 0
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Text {
                    width: parent.width
                    text: mpris && mpris.title.length > 0 ? mpris.title
                                                          : qsTr("Nada reproduciéndose")
                    color: card.fg
                    elide: Text.ElideRight
                    font.pixelSize: Math.max(7, Math.round((card.compact ? 12 : 15) * cmConfig.fontScale))
                    font.bold: true
                }
                Text {
                    width: parent.width
                    visible: mpris && mpris.artist.length > 0
                    text: mpris ? mpris.artist : ""
                    color: card.fg
                    opacity: 0.7
                    elide: Text.ElideRight
                    font.pixelSize: Math.max(7, Math.round((card.compact ? 10 : 12) * cmConfig.fontScale))
                }
                Text {
                    width: parent.width
                    visible: !card.compact && mpris && mpris.album.length > 0
                    text: mpris ? mpris.album : ""
                    color: card.fg
                    opacity: 0.5
                    elide: Text.ElideRight
                    font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                }
            }
        }

        // --- transport ---
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 6

            CmButton {
                fg: card.fg
                compact: card.compact
                icon: "media-skip-backward"
                enabled: mpris ? mpris.canGoPrevious : false
                tip: qsTr("Anterior")
                onClicked: mpris.previous()
            }
            CmButton {
                fg: card.fg
                compact: card.compact
                icon: mpris && mpris.playing ? "media-playback-pause" : "media-playback-start"
                tip: mpris && mpris.playing ? qsTr("Pausa") : qsTr("Reproducir")
                onClicked: mpris.playPause()
            }
            CmButton {
                fg: card.fg
                compact: card.compact
                icon: "media-playback-stop"
                tip: qsTr("Detener")
                onClicked: mpris.stop()
            }
            CmButton {
                fg: card.fg
                compact: card.compact
                icon: "media-skip-forward"
                enabled: mpris ? mpris.canGoNext : false
                tip: qsTr("Siguiente")
                onClicked: mpris.next()
            }
        }

        // --- progress ---
        Item {
            width: parent.width
            height: 18
            visible: mpris && mpris.length > 0

            Text {
                id: pos
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: card.fmt(mpris ? mpris.position : 0)
                color: card.fg
                opacity: 0.6
                font.pixelSize: Math.max(7, Math.round((10) * cmConfig.fontScale))
            }
            Text {
                id: len
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: card.fmt(mpris ? mpris.length : 0)
                color: card.fg
                opacity: 0.6
                font.pixelSize: Math.max(7, Math.round((10) * cmConfig.fontScale))
            }
            CmSlider {
                fg: card.fg
                anchors.left: pos.right
                anchors.right: len.left
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                compact: true
                enabled: mpris ? mpris.canSeek : false
                valueText: " "
                value: mpris && mpris.length > 0 ? mpris.position / mpris.length : 0
                onMoved: (v) => mpris.seekToRatio(v)
            }
        }

        // --- player picker, only when there is more than one ---
        Flow {
            width: parent.width
            spacing: 6
            visible: !card.compact && mpris && mpris.players().length > 1

            Repeater {
                model: mpris ? mpris.players() : []
                delegate: CmButton {
                    fg: card.fg
                    required property var modelData
                    compact: true
                    label: modelData.identity
                    checked: mpris.playerId === modelData.id
                    tip: modelData.status
                    onClicked: mpris.setPlayer(modelData.id)
                }
            }
        }
    }
}
