// Brightness of every monitor, the power profile, and kdock's dark mode.
//
// Brightness comes from PowerDevil (org.kde.ScreenBrightness), which is the only
// thing that knows about the external displays; the `brightness` backend
// (brightnessctl) is the fallback for a session without PowerDevil, and then
// there is exactly one slider.
//
// Dark mode is not ours to write: it lives in the dock's config and only the
// dock's process can repaint from it, so it goes through org.kdock.Dock.

import QtQuick
import QtQuick.Controls.Basic
import ".."

Item {
    id: card

    property bool compact: false
    // Text colour, pushed in by CmSectionView: the card's contrast answer on
    // Principal, the panel's on a full tab. Every text of this file uses it.
    property color fg: theme.foreground

    property int rev: 0
    Connections {
        target: screens
        function onChanged() { card.rev++ }
    }

    readonly property var displays: { card.rev; return screens ? screens.displays() : [] }
    readonly property bool perMonitor: screens && screens.available && card.displays.length > 0
    readonly property bool haveFallback: brightness && brightness.available

    // The one the compact card drives: the internal panel if there is one, else
    // the first display.
    readonly property var mainDisplay: {
        for (var i = 0; i < card.displays.length; ++i) {
            if (card.displays[i].internal)
                return card.displays[i]
        }
        return card.displays.length > 0 ? card.displays[0] : null
    }

    // Only two brightness icons exist in breeze (there is no -medium, checked
    // against the iconset rather than guessed), so the split is at half.
    function brightnessIcon(value) {
        return value < 0.5 ? "brightness-low" : "brightness-high"
    }

    // --- compact ------------------------------------------------------------
    Column {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4
        visible: card.compact

        CmSlider {
            fg: card.fg
            width: parent.width
            compact: true
            icon: card.brightnessIcon(card.perMonitor
                                      ? (card.mainDisplay ? card.mainDisplay.value : 0)
                                      : (card.haveFallback ? brightness.brightness : 0))
            enabled: card.perMonitor || card.haveFallback
            value: card.perMonitor
                   ? (card.mainDisplay ? card.mainDisplay.value : 0)
                   : (card.haveFallback ? brightness.brightness : 0)
            onMoved: (v) => {
                if (card.perMonitor && card.mainDisplay)
                    screens.setBrightness(card.mainDisplay.name, v)
                else if (card.haveFallback)
                    brightness.setBrightness(v)
            }
        }

        Row {
            spacing: 6
            CmButton {
                fg: card.fg
                compact: true
                icon: dock && dock.darkMode ? "weather-clear-night" : "weather-clear"
                label: qsTr("Oscuro")
                enabled: dock ? dock.available : false
                checked: dock ? dock.darkMode : false
                tip: dock && dock.available ? qsTr("Modo oscuro del dock")
                                            : qsTr("kdock no está en el bus")
                onClicked: dock.toggleDarkMode()
            }
            CmButton {
                fg: card.fg
                compact: true
                icon: "preferences-system-power-management"
                label: qsTr("Energía")
                onClicked: win.currentTab = "video"
            }
        }
    }

    // --- full ---------------------------------------------------------------
    Flickable {
        anchors.fill: parent
        visible: !card.compact
        contentWidth: width
        contentHeight: full.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: full
            width: parent.width
            spacing: 12

            // --- brightness ---
            Column {
                width: parent.width
                spacing: 2
                Text {
                    text: qsTr("Brillo")
                    color: card.fg
                    opacity: 0.6
                    font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                    font.bold: true
                }
                Text {
                    visible: !card.perMonitor && !card.haveFallback
                    text: qsTr("Ni PowerDevil ni brightnessctl responden.")
                    color: card.fg
                    opacity: 0.55
                    font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                }

                Repeater {
                    model: card.perMonitor ? card.displays : []
                    delegate: CmSlider {
                        fg: card.fg
                        required property var modelData
                        width: full.width
                        icon: card.brightnessIcon(modelData.value)
                        label: modelData.label.length > 0 ? modelData.label : modelData.name
                        value: modelData.value
                        onMoved: (v) => screens.setBrightness(modelData.name, v)
                    }
                }

                // Fallback: no PowerDevil, so the internal backlight is all
                // there is.
                CmSlider {
                    fg: card.fg
                    visible: !card.perMonitor && card.haveFallback
                    width: full.width
                    icon: card.brightnessIcon(card.haveFallback ? brightness.brightness : 0)
                    label: qsTr("Pantalla")
                    value: card.haveFallback ? brightness.brightness : 0
                    onMoved: (v) => brightness.setBrightness(v)
                }

                Row {
                    visible: card.perMonitor && card.displays.length > 1
                    spacing: 6
                    topPadding: 4
                    CmButton {
                        fg: card.fg
                        compact: true
                        label: qsTr("Todos al 100 %")
                        onClicked: screens.setAll(1.0)
                    }
                    CmButton {
                        fg: card.fg
                        compact: true
                        label: qsTr("Todos al 50 %")
                        onClicked: screens.setAll(0.5)
                    }
                }
            }

            // --- power profile ---
            Column {
                width: parent.width
                spacing: 4
                visible: battery && battery.profilesAvailable
                Text {
                    text: qsTr("Perfil de energía")
                    color: card.fg
                    opacity: 0.6
                    font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                    font.bold: true
                }
                Row {
                    spacing: 6
                    Repeater {
                        model: battery ? battery.profiles : []
                        delegate: CmButton {
                            fg: card.fg
                            required property string modelData
                            label: modelData === "power-saver" ? qsTr("Ahorro")
                                 : modelData === "balanced" ? qsTr("Equilibrado")
                                 : modelData === "performance" ? qsTr("Rendimiento") : modelData
                            icon: modelData === "power-saver" ? "battery-low"
                                : modelData === "performance" ? "battery-full" : "battery-good"
                            checked: battery.activeProfile === modelData
                            onClicked: battery.setProfile(modelData)
                        }
                    }
                }
                Text {
                    visible: battery && battery.available
                    text: battery ? battery.tooltipText : ""
                    color: card.fg
                    opacity: 0.55
                    font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                }
            }

            // --- dark mode ---
            Column {
                width: parent.width
                spacing: 4
                Text {
                    text: qsTr("Modo oscuro del dock")
                    color: card.fg
                    opacity: 0.6
                    font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                    font.bold: true
                }
                Row {
                    spacing: 6
                    CmButton {
                        fg: card.fg
                        label: qsTr("Normal")
                        icon: "weather-clear"
                        enabled: dock ? dock.available : false
                        checked: dock ? !dock.darkMode : false
                        onClicked: dock.setDarkMode(false)
                    }
                    CmButton {
                        fg: card.fg
                        label: qsTr("Oscuro")
                        icon: "weather-clear-night"
                        enabled: dock ? dock.available : false
                        checked: dock ? dock.darkMode : false
                        onClicked: dock.setDarkMode(true)
                    }
                }
                Text {
                    visible: !dock || !dock.available
                    text: qsTr("kdock no está en el bus: no se puede conmutar desde acá.")
                    color: card.fg
                    opacity: 0.55
                    font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                }
            }
        }
    }
}
