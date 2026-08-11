// Iconset y esquema de color **del escritorio**, no del dock.
//
// El backend es el mismo `AppearanceControl` que usan los dos widgets del dock y
// los selectores del diálogo, así que los favoritos, el orden y "mantener el
// selector abierto" son los mismos en todos lados. Lo que esta tarjeta
// deliberadamente NO toca es nada de kdock: ni el iconset propio del dock, ni el
// de sus widgets, ni sus colores — eso vive en la configuración del dock y
// duplicarlo acá sería una segunda fuente de verdad.
//
// Aplicar es fire-and-forget (las herramientas de Plasma son startDetached): el
// tilde se mueve solo cuando KDE contesta, vía la señal `changed`.

import QtQuick
import QtQuick.Controls.Basic
import ".."

Item {
    id: card

    property bool compact: false
    // Text colour, pushed in by CmSectionView: the card's contrast answer on
    // Principal, the panel's on a full tab. Every text of this file uses it.
    property color fg: theme.foreground

    readonly property bool available: typeof appearance !== "undefined" && appearance

    // 0 = iconos, 1 = colores. Las dos listas son largas (~450 esquemas en una
    // instalación completa), así que se muestra una por vez y no dos columnas
    // que no entran en una tarjeta de 2x2.
    property int mode: 0
    readonly property bool iconsMode: mode === 0

    // Se bombea cuando KDE contesta o cuando alguien marca un favorito: las dos
    // listas son llamadas a función, no properties, así que no se re-evalúan
    // solas.
    property int rev: 0

    readonly property var entries: {
        card.rev
        if (!card.available)
            return []
        return card.iconsMode ? appearance.iconThemes() : appearance.colorSchemes()
    }

    Connections {
        target: card.available ? appearance : null
        function onChanged() { card.rev++ }
        function onFavoritesChanged() { card.rev++ }
    }

    // Barato salvo que se haya instalado un esquema desde el último escaneo.
    Component.onCompleted: if (card.available) appearance.refreshIfStale()
    onModeChanged: if (card.available) appearance.refreshIfStale()

    // Anclas y no un Column: la lista tiene que quedarse con **todo** el alto
    // que sobra, y un Column reparte por contenido (una lista sin alto propio
    // sale de 0 px y la sección parece vacía).
    Row {
        id: switcher
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 6

        CmButton {
            fg: card.fg
            compact: card.compact
            icon: "preferences-desktop-icons"
            label: qsTr("Iconos")
            checked: card.iconsMode
            onClicked: card.mode = 0
        }
        CmButton {
            fg: card.fg
            compact: card.compact
            icon: "color-management"
            label: qsTr("Colores")
            checked: !card.iconsMode
            onClicked: card.mode = 1
        }
    }

    // Qué está aplicado ahora mismo. En una lista de 450 esquemas es la única
    // forma de saber dónde se está parado sin recorrerla, y en la tarjeta chica
    // el espacio no da: ahí lo dice la negrita de la fila.
    Text {
        id: appliedLabel
        visible: !card.compact && card.available
        anchors.top: switcher.bottom
        anchors.topMargin: 4
        anchors.left: parent.left
        anchors.right: parent.right
        elide: Text.ElideRight
        text: card.iconsMode
              ? qsTr("Iconset: %1").arg(appearance.currentIconTheme.length > 0
                                        ? appearance.currentIconTheme : qsTr("(sin definir)"))
              : qsTr("Colores: %1").arg(appearance.currentColorScheme.length > 0
                                        ? appearance.currentColorScheme : qsTr("(sin definir)"))
        color: card.fg
        opacity: 0.6
        font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
    }

    Text {
        visible: !card.available
        anchors.centerIn: parent
        width: parent.width * 0.8
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: qsTr("No se pudieron leer los temas del escritorio.")
        color: card.fg
        opacity: 0.55
        font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
    }

    ListView {
        id: list
        visible: card.available
        anchors.top: appliedLabel.visible ? appliedLabel.bottom : switcher.bottom
        anchors.topMargin: 4
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        model: card.entries
        spacing: 2
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        // Mantener a la vista lo aplicado: en una lista de 450 esquemas, abrir
        // la sección y no ver dónde está el actual se lee como que la lista
        // está desordenada. Va con callLater porque en el mismo tick el modelo
        // ya cambió pero la vista todavía no tiene delegates que posicionar
        // (posicionar ahí no hace nada, y parece que la lista ignora el pedido).
        onModelChanged: Qt.callLater(list.showApplied)
        Component.onCompleted: Qt.callLater(list.showApplied)

        function showApplied() {
            for (var i = 0; i < card.entries.length; ++i) {
                if (card.entries[i].current === true) {
                    list.positionViewAtIndex(i, ListView.Center)
                    return
                }
            }
        }

        delegate: Rectangle {
            id: row
            required property int index
            required property var modelData
            readonly property bool applied: modelData.current === true

            width: list.width
            height: Math.round((card.compact ? 26 : 32) * cmConfig.fontScale)
            radius: 5
            color: rowMouse.containsMouse
                   ? Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.28)
                   : (row.applied
                      ? Qt.rgba(card.fg.r, card.fg.g, card.fg.b, 0.10) : "transparent")

            Row {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 6
                // Encima del MouseArea de la fila, que se declara último:
                // así el casillero de favorito recibe sus propios clics y
                // el resto de la fila cae al MouseArea.
                z: 1

                // Vista previa. Los íconos se resuelven **contra ese tema**
                // (`name@rev@tema`), que es la única URL del panel que no
                // sale de win.iconSuffix: acá el tema es justamente el dato.
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    visible: card.iconsMode
                    Repeater {
                        model: card.iconsMode
                               ? ["folder", "utilities-terminal", "configure"] : []
                        Image {
                            required property string modelData
                            width: Math.round(row.height * 0.62)
                            height: width
                            source: "image://icon/" + modelData + "@" + theme.revision
                                    + "@" + row.modelData.id
                            sourceSize: Qt.size(width * Screen.devicePixelRatio,
                                                width * Screen.devicePixelRatio)
                            asynchronous: true
                        }
                    }
                }
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    visible: !card.iconsMode
                    Repeater {
                        model: card.iconsMode ? [] : ["bg", "fg", "sel"]
                        Rectangle {
                            required property string modelData
                            width: Math.round(row.height * 0.62)
                            height: width
                            radius: 3
                            color: row.modelData[modelData]
                            border.width: 1
                            border.color: Qt.rgba(card.fg.r, card.fg.g, card.fg.b, 0.25)
                        }
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 3 * Math.round(row.height * 0.62) - 2 * 3
                           - favBox.width - 3 * parent.spacing
                    text: row.modelData.name
                    color: card.fg
                    elide: Text.ElideRight
                    font.pixelSize: Math.max(7, Math.round((12) * cmConfig.fontScale))
                    font.bold: row.applied
                }

                // Favorito: lo mismo que el selector del dock, y el orden ya
                // viene resuelto del backend (favoritos primero).
                Item {
                    id: favBox
                    width: Math.round(row.height * 0.62)
                    height: width
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        anchors.centerIn: parent
                        width: 15; height: 15; radius: 3
                        color: row.modelData.fav === true ? theme.highlight : "transparent"
                        border.width: 1
                        border.color: Qt.rgba(card.fg.r, card.fg.g, card.fg.b,
                                              row.modelData.fav === true ? 0.0 : 0.45)
                        Text {
                            anchors.centerIn: parent
                            visible: row.modelData.fav === true
                            text: "★"
                            color: theme.background
                            font.pixelSize: 10
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: appearance.setFavorite(card.iconsMode ? "icons" : "colors",
                                                          row.modelData.id,
                                                          row.modelData.fav !== true)
                    }
                }
            }

            MouseArea {
                id: rowMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    if (card.iconsMode)
                        appearance.applyIconTheme(row.modelData.id)
                    else
                        appearance.applyColorScheme(row.modelData.id)
                }
            }
        }
    }
}
