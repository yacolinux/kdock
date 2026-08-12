// The four configuration panels, the restarts, and the session row.
//
// Two of the four (kdock itself and its own settings dialog) are only reachable
// through org.kdock.Dock: kdock's dialog opens from its own menu and nowhere
// else. The other two are ordinary launches of the accessory binaries with
// --settings, which their single-instance guard forwards to whatever instance
// is already running.

import QtQuick
import QtQuick.Controls.Basic
import ".."

Item {
    id: card

    property bool compact: false
    // Text colour, pushed in by CmSectionView: the card's contrast answer on
    // Principal, the panel's on a full tab. Every text of this file uses it.
    property color fg: theme.foreground
    readonly property bool haveDock: dock && dock.available

    Flickable {
        anchors.fill: parent
        // AutoFlickIfNeeded and not the default AutoFlickDirection: the latter
        // calls itself flickable whenever contentHeight != height — content
        // *shorter* than the viewport included — and then steals the drag of
        // any slider inside it (see CmSlider). This one only flicks when there
        // is something to scroll.
        flickableDirection: Flickable.AutoFlickIfNeeded
        contentWidth: width
        contentHeight: col.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: col
            width: parent.width
            spacing: card.compact ? 6 : 10

            // --- the four settings panels ---
            Text {
                visible: !card.compact
                text: qsTr("Configuración")
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
                    icon: "preferences-system"
                    label: qsTr("KDE")
                    tip: qsTr("Preferencias del sistema")
                    onClicked: win.runCommand("systemsettings")
                }
                CmButton {
                    fg: card.fg
                    compact: card.compact
                    icon: "configure"
                    label: qsTr("kdock")
                    enabled: card.haveDock
                    tip: card.haveDock ? qsTr("Configuración del dock")
                                       : qsTr("kdock no está en el bus")
                    onClicked: { dock.openSettings(""); win.hidePanel() }
                }
                CmButton {
                    fg: card.fg
                    compact: card.compact
                    icon: "view-list-icons"
                    label: qsTr("Mosaicos")
                    tip: qsTr("Configuración del menú de mosaicos")
                    onClicked: { win.runCommand("kdock-tilemenu", ["--settings"]); win.hidePanel() }
                }
                CmButton {
                    fg: card.fg
                    compact: card.compact
                    icon: "view-preview"
                    label: qsTr("Previews")
                    tip: qsTr("Configuración de las vistas previas")
                    onClicked: { win.runCommand("kdock-previews", ["--settings"]); win.hidePanel() }
                }
                CmButton {
                    fg: card.fg
                    compact: card.compact
                    icon: "preferences-system"
                    label: qsTr("Este panel")
                    tip: qsTr("Configuración de Control Manager")
                    onClicked: win.openSettings()
                }
            }

            // --- restarts ---
            Text {
                visible: !card.compact
                text: qsTr("Reiniciar")
                color: card.fg
                opacity: 0.6
                font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                font.bold: true
            }

            Flow {
                width: parent.width
                spacing: 6
                visible: !card.compact

                CmButton {
                    fg: card.fg
                    icon: "view-refresh"
                    label: qsTr("Dock")
                    enabled: card.haveDock
                    // kdock is one process drawing every dock, so this restarts
                    // all of them — say so rather than surprise the user.
                    tip: qsTr("Reinicia el proceso de kdock (todos sus docks)")
                    onClicked: {
                        if (win.confirm(qsTr("Reiniciar el dock"),
                                        qsTr("kdock es un solo proceso: se reinician todos los "
                                             + "docks a la vez. ¿Seguir?")))
                            dock.restartDock()
                    }
                }
                CmButton {
                    fg: card.fg
                    icon: "view-refresh"
                    label: qsTr("Mosaicos")
                    tip: qsTr("Lo cierra; vuelve solo en el próximo clic del widget")
                    onClicked: win.runCommand("qdbus6", ["org.kdock.TileMenu", "/TileMenu",
                                                         "org.kdock.TileMenu.quit"])
                }
                CmButton {
                    fg: card.fg
                    icon: "view-refresh"
                    label: qsTr("Previews")
                    tip: qsTr("Lo cierra; vuelve al prender su casilla")
                    onClicked: win.runCommand("qdbus6", ["org.kdock.Previews", "/Previews",
                                                         "org.kdock.Previews.quit"])
                }
                CmButton {
                    fg: card.fg
                    icon: "view-refresh"
                    label: qsTr("Este panel")
                    onClicked: win.restartSelf()
                }
            }

            // --- session ---
            Text {
                visible: !card.compact && power && power.available
                text: qsTr("Sesión")
                color: card.fg
                opacity: 0.6
                font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                font.bold: true
            }

            Flow {
                width: parent.width
                spacing: 6
                visible: power && power.available

                Repeater {
                    model: [
                        { ic: "system-lock-screen", act: "lock",     label: qsTr("Bloquear"), tip: qsTr("Bloquear") },
                        { ic: "system-suspend",     act: "suspend",  label: qsTr("Suspender"), tip: qsTr("Suspender") },
                        { ic: "system-log-out",     act: "logout",   label: qsTr("Cerrar sesión"), tip: qsTr("Cerrar sesión…") },
                        { ic: "system-reboot",      act: "reboot",   label: qsTr("Reiniciar"), tip: qsTr("Reiniciar…") },
                        { ic: "system-shutdown",    act: "shutdown", label: qsTr("Apagar"), tip: qsTr("Apagar…") }
                    ]
                    delegate: CmButton {
                        fg: card.fg
                        required property var modelData
                        compact: card.compact
                        icon: modelData.ic
                        label: modelData.label
                        tip: modelData.tip
                        onClicked: {
                            win.hidePanel()
                            power[modelData.act]()
                        }
                    }
                }
            }
        }
    }
}
