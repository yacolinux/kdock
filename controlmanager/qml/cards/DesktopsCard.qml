// Los escritorios virtuales de KWin: cuál es el actual y saltar a otro.
//
// El backend es el mismo `VirtualDesktops` que alimenta el widget paginador del
// dock (posiciones **1-based**, como los números que ve el usuario). `count === 0`
// significa "KWin no contesta" —X11, otro compositor, o el arnés de Xvfb—, y ahí
// la tarjeta lo dice en vez de dibujar una fila vacía que se lee como un bug.

import QtQuick
import QtQuick.Controls.Basic
import ".."

Item {
    id: card

    property bool compact: false
    // Text colour, pushed in by CmSectionView: the card's contrast answer on
    // Principal, the panel's on a full tab. Every text of this file uses it.
    property color fg: theme.foreground

    readonly property bool available: typeof virtualDesktops !== "undefined"
                                      && virtualDesktops && virtualDesktops.count > 0

    Text {
        visible: !card.available
        anchors.centerIn: parent
        width: parent.width * 0.85
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: qsTr("KWin no informa escritorios virtuales.")
        color: card.fg
        opacity: 0.55
        font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
    }

    Flickable {
        anchors.fill: parent
        visible: card.available
        contentWidth: width
        contentHeight: grid.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Flow {
            id: grid
            width: parent.width
            spacing: 6

            Repeater {
                // `count` y `names` cambian juntos (la señal countChanged cubre
                // las dos), así que alcanza con el modelo entero.
                model: card.available ? virtualDesktops.names : []

                delegate: CmButton {
                    required property int index
                    required property string modelData

                    fg: card.fg
                    compact: card.compact
                    // El número siempre; el nombre solo si el usuario le puso
                    // uno distinto del "Escritorio N" de KWin, que repetiría el
                    // número en cada botón.
                    label: {
                        const n = index + 1
                        const generic = modelData.length === 0
                                        || modelData === qsTr("Escritorio %1").arg(n)
                                        || /^(Desktop|Escritorio)\s+\d+$/.test(modelData)
                        return generic ? String(n) : n + " · " + modelData
                    }
                    tip: modelData
                    checked: virtualDesktops.current === index + 1
                    onClicked: virtualDesktops.switchTo(index + 1)
                }
            }
        }
    }
}
