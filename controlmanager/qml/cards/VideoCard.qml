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
    readonly property bool haveFallback: brightness && brightness.internalAvailable

    // PowerDevil usually lists the DDC monitors and NOT the laptop panel, so
    // without this row the internal screen has no slider at all — which is
    // exactly how it looked with a dock station: one monitor listed, the built-in
    // one missing (reported 2026-08-12, with a screenshot of this card).
    readonly property bool haveInternalDisplay: {
        for (var i = 0; i < card.displays.length; ++i) {
            if (card.displays[i].internal)
                return true
        }
        return false
    }
    readonly property bool internalRow: card.haveFallback && !card.haveInternalDisplay

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

        // One slider, and it drives exactly the monitor the dock's wheel drives:
        // `brightness` resolves that itself (BrightnessControl::wheelDisplay),
        // so the compact card and the dock widget can never disagree. The other
        // screens are one click away, in the full section below.
        CmSlider {
            fg: card.fg
            width: parent.width
            compact: true
            icon: card.brightnessIcon(brightness ? brightness.brightness : 0)
            enabled: brightness ? brightness.available : false
            value: brightness ? brightness.brightness : 0
            onMoved: (v) => brightness.setBrightness(v)
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
        // AutoFlickIfNeeded and not the default AutoFlickDirection: the latter
        // calls itself flickable whenever contentHeight != height — content
        // *shorter* than the viewport included — and then steals the drag of
        // any slider inside it (see CmSlider). This one only flicks when there
        // is something to scroll.
        flickableDirection: Flickable.AutoFlickIfNeeded
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

                // The model is the *count*, not the array of displays. Handing a
                // Repeater a fresh JS array destroys and rebuilds every
                // delegate, and this list is rebuilt on each backend echo — so
                // the very first move of a drag deleted the slider being
                // dragged. The grab died with it: the row only answered to
                // isolated clicks and the pointer "escaped" the panel mid-drag
                // (reported 2026-08-12). With an int, delegates only come and
                // go when a monitor does.
                Repeater {
                    model: card.perMonitor ? card.displays.length : 0
                    delegate: CmSlider {
                        required property int index
                        // Can be null for one frame when a monitor is unplugged:
                        // the count changes before the delegate is destroyed.
                        readonly property var display: card.displays[index]
                                                       ? card.displays[index] : null
                        fg: card.fg
                        width: full.width
                        icon: card.brightnessIcon(display ? display.value : 0)
                        label: !display ? ""
                               : (display.label.length > 0 ? display.label : display.name)
                        value: display ? display.value : 0
                        onMoved: (v) => { if (display) screens.setBrightness(display.name, v) }
                    }
                }

                // The internal backlight, through brightnessctl. Shown whenever
                // PowerDevil does not report an internal display of its own —
                // with or without external monitors listed above, because that
                // is the docked-laptop case where the built-in screen would
                // otherwise have no slider anywhere.
                CmSlider {
                    fg: card.fg
                    visible: card.internalRow
                    width: full.width
                    icon: card.brightnessIcon(card.haveFallback ? brightness.internalBrightness : 0)
                    label: qsTr("Pantalla interna")
                    value: card.haveFallback ? brightness.internalBrightness : 0
                    onMoved: (v) => brightness.setInternalBrightness(v)
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
