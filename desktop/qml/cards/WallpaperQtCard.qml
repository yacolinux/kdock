// Fondo de Escritorio QT: capa LXQt del widget Avanzar Wallpaper QT.
// Idéntico código al del dock (WallpaperControl::nextWallpaper), pero aquí
// como card del panel de control: un botón por monitor conectado y uno para
// todos. Sin script override: en LXQt el wallpaper lo dibuja kdock y no hay
// slideshow de Plasma que alternar.

import QtQuick
import ".."

Item {
    id: card

    property bool compact: false
    property color fg: theme.foreground

    readonly property var screenList: Qt.application.screens

    Column {
        anchors.fill: parent
        anchors.margins: 2
        spacing: 6

        Text {
            visible: !card.compact
            text: qsTr("Fondo de escritorio QT (LXQt)")
            color: card.fg
            opacity: 0.6
            font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
            font.bold: true
        }

        Flow {
            width: parent.width
            spacing: 6

            Repeater {
                model: card.screenList
                delegate: CmButton {
                    fg: card.fg
                    required property var modelData
                    compact: card.compact
                    icon: "media-playlist-shuffle"
                    label: modelData.name
                    tip: qsTr("Avanza el fondo de %1 (LXQt)").arg(modelData.name)
                    enabled: wallpaperControl ? wallpaperControl.available : false
                    onClicked: wallpaperControl.nextWallpaper(modelData.name)
                }
            }

            CmButton {
                fg: card.fg
                compact: card.compact
                icon: "view-refresh"
                label: qsTr("Todos")
                tip: qsTr("Avanza el fondo de todos los monitores conectados (LXQt)")
                enabled: wallpaperControl ? wallpaperControl.available : false
                onClicked: wallpaperControl.nextWallpaperAll()
            }
        }

        Text {
            visible: !card.compact
            width: parent.width
            text: qsTr("LXQt: un botón por monitor y «Todos» para avanzar todos a la vez. Usa el mismo código que el widget del dock «Avanzar Wallpaper QT».")
            color: card.fg
            opacity: 0.45
            wrapMode: Text.Wrap
            font.pixelSize: Math.max(7, Math.round((10) * cmConfig.fontScale))
        }
    }
}
