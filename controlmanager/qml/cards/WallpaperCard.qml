// Advance the KDE slideshow wallpaper: one button per connected monitor, plus
// one for all of them.
//
// The per-monitor path is WallpaperControl, the same backend behind the dock's
// `nextwallpaper` widget (the KDE global shortcut only ever advances the primary
// screen — that is why the backend exists). "All monitors" runs the user's own
// script, because that is the piece that already worked and is theirs to edit.

import QtQuick
import ".."

Item {
    id: card

    property bool compact: false
    // Text colour, pushed in by CmSectionView: the card's contrast answer on
    // Principal, the panel's on a full tab. Every text of this file uses it.
    property color fg: theme.foreground

    // Qt.application.screens is a list of {name, ...}: exactly the connector
    // names WallpaperControl expects.
    readonly property var screenList: Qt.application.screens

    Column {
        anchors.fill: parent
        anchors.margins: 2
        spacing: 6

        Text {
            visible: !card.compact
            text: qsTr("Siguiente imagen del fondo")
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
                    required property var modelData
                    compact: card.compact
                    icon: "preferences-desktop-wallpaper"
                    label: modelData.name
                    tip: qsTr("Avanza el fondo de %1").arg(modelData.name)
                    enabled: wallpaperControl ? wallpaperControl.available : false
                    onClicked: wallpaperControl.nextWallpaper(modelData.name)
                }
            }

            // No script configured is the common case, and leaving the button
            // greyed out there made "cambiar el fondo de todos los monitores"
            // look broken (reported 2026-08-10). The engine can do it on its
            // own — one advance per connected monitor — so the script is now
            // an override for whoever has one, not a requirement.
            CmButton {
                fg: card.fg
                compact: card.compact
                icon: "view-refresh"
                label: qsTr("Todos")
                tip: cmConfig.wallpaperScript.length > 0
                     ? qsTr("Corre %1").arg(cmConfig.wallpaperScript)
                     : qsTr("Avanza el fondo de todos los monitores conectados")
                enabled: cmConfig.wallpaperScript.length > 0
                         || (wallpaperControl ? wallpaperControl.available : false)
                onClicked: {
                    if (cmConfig.wallpaperScript.length > 0)
                        win.runScript(cmConfig.wallpaperScript)
                    else
                        wallpaperControl.nextWallpaperAll()
                }
            }
        }

        Text {
            visible: !card.compact
            width: parent.width
            text: qsTr("«Todos» avanza cada monitor conectado, o corre el script si "
                       + "configuraste uno.")
            color: card.fg
            opacity: 0.45
            wrapMode: Text.Wrap
            font.pixelSize: Math.max(7, Math.round((10) * cmConfig.fontScale))
        }
    }
}
