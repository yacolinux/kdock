// ColorAuto: build a KDE color scheme out of the current wallpaper, and keep it
// if it turned out well.
//
// Both buttons go through the dock over D-Bus (DockLink) instead of running the
// engine here. That is not indirection for its own sake: the engine keeps which
// of its two generated schemes is currently applied, because
// plasma-apply-colorscheme ignores a re-apply of the same name and they have to
// alternate. Two processes ping-ponging the same two names would step on each
// other, so the state lives in exactly one place — the dock's.
//
// Generating works whether or not ColorAuto is switched on: the tick box is the
// *automatic* mode, this is the manual one, and what it leaves behind is the
// user's to manage.

import QtQuick
import ".."

Item {
    id: card

    property bool compact: false
    // Text colour, pushed in by CmSectionView: the card's contrast answer on
    // Principal, the panel's on a full tab. Every text of this file uses it.
    property color fg: theme.foreground

    // kdock not being on the bus is normal (the panel outlives a dock restart),
    // so the buttons grey out instead of failing silently.
    readonly property bool haveDock: dock && dock.available

    // What the last press did, so neither button reads as inert. Cleared on the
    // next press rather than on a timer: the answer is worth keeping around.
    property string status: ""

    Column {
        anchors.fill: parent
        anchors.margins: 2
        spacing: 6

        Text {
            visible: !card.compact
            text: qsTr("Color desde el fondo de pantalla")
            color: card.fg
            opacity: 0.6
            font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
            font.bold: true
        }

        Flow {
            width: parent.width
            spacing: 6

            CmButton {
                fg: card.fg
                compact: card.compact
                icon: "color-management"
                label: qsTr("Generar")
                tip: card.haveDock
                     ? qsTr("Arma un esquema con el fondo actual y lo aplica al dock y al "
                            + "sistema. Repetirlo pasa al siguiente color del mismo fondo.")
                     : qsTr("El dock no responde")
                enabled: card.haveDock
                onClicked: {
                    dock.generateColorScheme()
                    card.status = qsTr("Generado del fondo actual.")
                }
            }

            CmButton {
                fg: card.fg
                compact: card.compact
                icon: "document-save"
                label: qsTr("Guardar")
                tip: card.haveDock
                     ? qsTr("Guarda el esquema puesto como uno permanente (kdock-1, "
                            + "kdock-2…) y lo aplica. A partir de ahí es tuyo.")
                     : qsTr("El dock no responde")
                enabled: card.haveDock
                onClicked: {
                    // The only blocking call of this card: there is nothing to
                    // show until the dock says which name it used.
                    var id = dock.saveColorScheme()
                    card.status = id.length > 0
                        ? qsTr("Guardado como %1.").arg(id)
                        : qsTr("Nada generado todavía: se generó uno, volvé a guardar.")
                }
            }
        }

        Text {
            visible: !card.compact && card.status.length > 0
            width: parent.width
            text: card.status
            color: card.fg
            opacity: 0.75
            wrapMode: Text.Wrap
            font.pixelSize: Math.max(7, Math.round((10) * cmConfig.fontScale))
        }

        Text {
            visible: !card.compact && card.status.length === 0
            width: parent.width
            text: qsTr("Funciona esté activado o no ColorAuto en la configuración del dock.")
            color: card.fg
            opacity: 0.45
            wrapMode: Text.Wrap
            font.pixelSize: Math.max(7, Math.round((10) * cmConfig.fontScale))
        }
    }
}
