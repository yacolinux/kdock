// ColorAuto: build a color scheme out of the current wallpaper, keep it if it
// turned out well, and switch the automatic mode on or off.
//
// Everything goes through the dock over D-Bus (DockLink) instead of running the
// engine here. That is not indirection for its own sake: the engine keeps which
// of its two generated schemes is currently applied, because
// plasma-apply-colorscheme ignores a re-apply of the same name and they have to
// alternate. Two processes ping-ponging the same two names would step on each
// other, so the state lives in exactly one place — the dock's. The same goes for
// the switch: turning it on captures the defaults and applies straight away, and
// dark mode can refuse, none of which happens by writing a config key from here.
//
// Generating and saving work whether or not ColorAuto is switched on: the switch
// is the *automatic* mode, those two are the manual one, and what they leave
// behind is the user's to manage.
//
// **The card asks whether there is a wallpaper to read, and does not assume.**
// Under LXQt kdock reads the wallpapers off its own engine (see
// AutoColorScheme::setWallpaperSource), so with that engine switched off there
// is nothing to sample and every button here is a silent no-op. This card used
// to answer an asynchronous generate with "Generado del fondo actual." no matter
// what happened, which in that case was simply false.

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
    // Whether the dock on the bus answers about ColorAuto's switch at all. A
    // panel from this build can outlive a restart into an older kdock, and
    // there "no" and "never heard of it" have to read differently.
    readonly property bool known: card.haveDock && dock.colorAutoKnown
    // Whether a generation would find a wallpaper. Asked of the dock rather than
    // guessed: only that process knows which wallpaper engine is running. With
    // a dock that cannot answer, assume yes — that is how this card behaved
    // before the question existed, and greying the buttons over an unanswered
    // question would be worse than letting the press through.
    readonly property bool canRead: card.haveDock && (!card.known || dock.colorAutoCanRead)
    readonly property bool autoOn: card.known && dock.colorAutoEnabled

    // What the last press did, so no button reads as inert. Cleared on the next
    // press rather than on a timer: the answer is worth keeping around.
    property string status: ""

    // The answer goes stale on its own — a slideshow step, a monitor unplugged,
    // the wallpaper engine switched off in the dock's settings — and none of
    // those has a signal to hang off. Re-asked when the card becomes visible,
    // which is the moment before the user can press anything.
    onVisibleChanged: if (visible && card.haveDock) dock.refreshColorAuto()

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
                tip: !card.haveDock ? qsTr("El dock no responde")
                     : !card.canRead ? qsTr("No hay ningún fondo que leer: kdock no está "
                                            + "dibujando los fondos de pantalla.")
                     : qsTr("Arma un esquema con el fondo actual y lo aplica al dock y al "
                            + "sistema. Repetirlo pasa al siguiente color del mismo fondo.")
                enabled: card.canRead
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
                tip: !card.haveDock ? qsTr("El dock no responde")
                     : qsTr("Guarda el esquema puesto como uno permanente (kdock-1, "
                            + "kdock-2…) y lo aplica. A partir de ahí es tuyo.")
                enabled: card.haveDock
                onClicked: {
                    // The only blocking call of this card: there is nothing to
                    // show until the dock says which name it used.
                    var id = dock.saveColorScheme()
                    card.status = id.length > 0
                        ? qsTr("Guardado como %1.").arg(id)
                        : card.canRead
                          ? qsTr("Nada generado todavía: se generó uno, volvé a guardar.")
                          : qsTr("No se pudo generar: no hay ningún fondo que leer.")
                }
            }

            // The automatic mode. `checked` is driven by the dock and never by
            // the press: setEnabled() is a no-op while dark mode owns the
            // appearance, so a switch that flipped itself would claim a state
            // the dock never reached.
            CmButton {
                fg: card.fg
                compact: card.compact
                icon: card.autoOn ? "media-playback-pause" : "media-playback-start"
                label: card.autoOn ? qsTr("Desactivar") : qsTr("Activar")
                checked: card.autoOn
                tip: !card.haveDock ? qsTr("El dock no responde")
                     : card.autoOn
                       ? qsTr("Deja de regenerar solo. Restaura el esquema y el iconset que "
                              + "estaban guardados antes de activarlo.")
                       : qsTr("Regenera el esquema solo, cada vez que cambia el fondo. La "
                              + "primera vez guarda el esquema actual para poder volver. "
                              + "Con el modo oscuro puesto no hace nada: ese manda.")
                // The only control that needs the dock to *know* the question:
                // pressing it against an older kdock would do nothing at all.
                enabled: card.known
                onClicked: {
                    dock.toggleColorAuto()
                    card.status = card.autoOn
                        ? qsTr("Modo automático desactivado.")
                        : qsTr("Modo automático activado.")
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
            // The two things worth saying, and which one depends on the state:
            // that there is nothing to read beats explaining the manual mode.
            text: !card.haveDock
                  ? qsTr("El dock no responde.")
                  : !card.known
                    ? qsTr("Este dock es más viejo que el modo automático: reiniciálo para "
                           + "manejarlo desde acá.")
                    : !card.canRead
                      ? qsTr("No hay ningún fondo que leer: prendé «Wallpapers» en la "
                             + "configuración del dock.")
                      : qsTr("Generar y Guardar funcionan esté activado o no el modo automático.")
            color: card.fg
            opacity: 0.45
            wrapMode: Text.Wrap
            font.pixelSize: Math.max(7, Math.round((10) * cmConfig.fontScale))
        }
    }
}
