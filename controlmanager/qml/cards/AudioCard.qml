// The mixer, backed by AudioControl (pactl) — the same backend behind kdock's
// Settings → Audio tab, so the two always agree.
//
// Compact: the default output, one slider, one mute button. Full: outputs,
// inputs and per-application streams, each with a default-device radio, a mute
// toggle and a slider up to the configured ceiling.

import QtQuick
import QtQuick.Controls.Basic
import ".."

Item {
    id: card

    property bool compact: false
    // Text colour, pushed in by CmSectionView: the card's contrast answer on
    // Principal, the panel's on a full tab. Every text of this file uses it.
    property color fg: theme.foreground

    // Re-read on every change of the backend. `rev` exists so the lists below
    // re-evaluate: outputList() is a plain function call, not a property.
    property int rev: 0
    Connections {
        target: audio
        function onChanged() { card.rev++ }
    }

    readonly property var outputs: { card.rev; return audio ? audio.outputList() : [] }
    readonly property var inputs: { card.rev; return audio ? audio.inputList() : [] }
    readonly property var streams: { card.rev; return audio ? audio.appList() : [] }
    readonly property real ceiling: { card.rev; return audio ? audio.ceiling() : 1.0 }

    readonly property var defaultOutput: {
        for (var i = 0; i < card.outputs.length; ++i) {
            if (card.outputs[i].isDefault)
                return card.outputs[i]
        }
        return card.outputs.length > 0 ? card.outputs[0] : null
    }

    function volumeIcon(dev) {
        if (!dev || dev.muted)
            return "audio-volume-muted"
        if (dev.volume < 0.34)
            return "audio-volume-low"
        if (dev.volume < 0.67)
            return "audio-volume-medium"
        return "audio-volume-high"
    }

    Text {
        anchors.centerIn: parent
        visible: !audio || !audio.available
        text: qsTr("pactl no responde")
        color: card.fg
        opacity: 0.6
        font.pixelSize: Math.max(7, Math.round((12) * cmConfig.fontScale))
    }

    // --- compact ------------------------------------------------------------
    Column {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 6
        visible: card.compact && audio && audio.available

        Text {
            width: parent.width
            text: card.defaultOutput ? card.defaultOutput.description : qsTr("Sin salida")
            color: card.fg
            elide: Text.ElideRight
            font.pixelSize: Math.max(7, Math.round((12) * cmConfig.fontScale))
            font.bold: cmConfig.labelBold
        }

        CmSlider {

            fg: card.fg
            width: parent.width
            compact: true
            icon: card.volumeIcon(card.defaultOutput)
            enabled: card.defaultOutput !== null
            maximum: card.ceiling
            value: card.defaultOutput ? card.defaultOutput.volume : 0
            onMoved: (v) => {
                if (card.defaultOutput)
                    audio.setVolume(0, card.defaultOutput.index, v)
            }
        }

        Row {
            spacing: 6
            CmButton {
                fg: card.fg
                compact: true
                icon: card.volumeIcon(card.defaultOutput)
                checked: card.defaultOutput ? card.defaultOutput.muted : false
                enabled: card.defaultOutput !== null
                tip: qsTr("Silenciar")
                onClicked: audio.toggleMute(0, card.defaultOutput.index)
            }
            CmButton {
                fg: card.fg
                compact: true
                icon: "audio-volume-high"
                label: qsTr("Mezclador")
                onClicked: win.currentTab = "audio"
            }
        }
    }

    // --- full ---------------------------------------------------------------
    Flickable {
        anchors.fill: parent
        visible: !card.compact && audio && audio.available
        contentWidth: width
        contentHeight: fullColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: fullColumn
            width: parent.width
            spacing: 10

            Row {
                spacing: 8
                CmButton {
                    fg: card.fg
                    label: qsTr("Volumen hasta 150 %")
                    icon: "audio-volume-high"
                    checked: audio ? audio.maxVolume : false
                    onClicked: audio.maxVolume = !audio.maxVolume
                }
            }

            Repeater {
                model: [
                    { title: qsTr("Salidas"),      list: card.outputs, type: 0, radio: true },
                    { title: qsTr("Entradas"),     list: card.inputs,  type: 1, radio: true },
                    { title: qsTr("Aplicaciones"), list: card.streams, type: 2, radio: false }
                ]
                delegate: Column {
                    id: group
                    required property var modelData
                    width: fullColumn.width
                    spacing: 4
                    visible: group.modelData.list.length > 0

                    Text {
                        text: group.modelData.title
                        color: card.fg
                        opacity: 0.6
                        font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                        font.bold: true
                    }

                    Repeater {
                        model: group.modelData.list
                        delegate: Item {
                            id: devRow
                            required property var modelData
                            width: group.width
                            height: 46

                            // The default-device marker doubles as the button
                            // that sets it; applications have no default, so the
                            // column collapses for them.
                            Rectangle {
                                id: marker
                                visible: group.modelData.radio
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: visible ? 14 : 0
                                height: 14
                                radius: 7
                                color: devRow.modelData.isDefault ? theme.highlight : "transparent"
                                border.width: 1
                                border.color: Qt.rgba(card.fg.r, card.fg.g, card.fg.b, 0.5)
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    onClicked: audio.setDefault(group.modelData.type,
                                                                devRow.modelData.name)
                                }
                            }

                            Text {
                                id: devName
                                anchors.left: marker.visible ? marker.right : parent.left
                                anchors.leftMargin: marker.visible ? 8 : 0
                                anchors.right: parent.right
                                anchors.top: parent.top
                                text: devRow.modelData.description
                                color: card.fg
                                elide: Text.ElideRight
                                font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                                font.bold: devRow.modelData.isDefault && cmConfig.labelBold
                            }

                            CmButton {

                                fg: card.fg
                                id: muteBtn
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                compact: true
                                icon: devRow.modelData.muted ? "audio-volume-muted"
                                                             : "audio-volume-high"
                                checked: devRow.modelData.muted
                                tip: qsTr("Silenciar")
                                onClicked: audio.toggleMute(group.modelData.type,
                                                            devRow.modelData.index)
                            }

                            CmSlider {

                                fg: card.fg
                                anchors.left: devName.left
                                anchors.right: muteBtn.left
                                anchors.rightMargin: 8
                                anchors.bottom: parent.bottom
                                compact: true
                                maximum: card.ceiling
                                value: devRow.modelData.volume
                                onMoved: (v) => audio.setVolume(group.modelData.type,
                                                                devRow.modelData.index, v)
                            }
                        }
                    }
                }
            }
        }
    }
}
