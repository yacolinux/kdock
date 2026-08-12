// El clima, sobre el mismo WeatherControl que usan el widget del dock y la
// mini-app kdock-weather: las tres superficies leen la misma configuración
// (weather.conf) y la misma caché, así que no pueden decir cosas distintas.
//
// La compacta es un resumen (ícono, temperatura, ciudad) más el botón que abre
// la sección; la completa repite lo que muestra la mini-app: actual, pronóstico
// y detalles. La *lógica* —íconos, unidades, nombres de día, textos de
// condición— no se duplica: viene formateada de C++.

import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: card

    property bool compact: false
    // Text colour, pushed in by CmSectionView: the card's contrast answer on
    // Principal, the panel's on a full tab. Every text of this file uses it.
    property color fg: theme.foreground

    // 0 = pronóstico, 1 = detalles (solo en la vista completa).
    property int tab: 0

    property int rev: 0
    Connections {
        target: weather
        function onChanged() { card.rev++ }
    }
    readonly property var days: { card.rev; return weather ? weather.forecast() : [] }
    readonly property var detailRows: {
        card.rev
        if (!weather || card.compact || card.tab !== 1) return []
        return weather.details()
    }

    function px(size) { return Math.max(7, Math.round(size * cmConfig.fontScale)) }

    // --- sin ciudad ----------------------------------------------------------
    Column {
        anchors.centerIn: parent
        width: parent.width - 16
        spacing: 8
        visible: !weather || !weather.configured

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Todavía no elegiste una ciudad")
            color: card.fg
            opacity: 0.7
            wrapMode: Text.Wrap
            font.pixelSize: card.px(12)
        }
        CmButton {
            anchors.horizontalCenter: parent.horizontalCenter
            fg: card.fg
            compact: card.compact
            icon: "configure"
            label: qsTr("Elegir…")
            visible: typeof weatherLauncher !== "undefined" && weatherLauncher
            onClicked: weatherLauncher.openSettings()
        }
    }

    // --- compacta ------------------------------------------------------------
    Column {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 6
        visible: card.compact && weather && weather.configured

        Row {
            width: parent.width
            spacing: 8
            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                height: 30
                source: weather ? "image://icon/" + weather.iconName + win.iconSuffix : ""
                sourceSize: Qt.size(30 * Screen.devicePixelRatio, 30 * Screen.devicePixelRatio)
                opacity: weather && weather.stale ? 0.55 : 1.0
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 38
                spacing: 0
                Text {
                    width: parent.width
                    text: weather ? weather.tempText : ""
                    color: card.fg
                    font.pixelSize: card.px(18)
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: weather ? weather.conditionText : ""
                    color: card.fg
                    opacity: 0.7
                    font.pixelSize: card.px(11)
                    elide: Text.ElideRight
                }
            }
        }

        Text {
            width: parent.width
            text: weather ? weather.cityLabel : ""
            color: card.fg
            opacity: 0.6
            font.pixelSize: card.px(10)
            elide: Text.ElideRight
        }

        Row {
            spacing: 6
            CmButton {
                fg: card.fg
                compact: true
                icon: "weather-few-clouds"
                label: qsTr("Pronóstico")
                onClicked: win.currentTab = "weather"
            }
        }
    }

    // --- completa ------------------------------------------------------------
    Item {
        anchors.fill: parent
        visible: !card.compact && weather && weather.configured

        Column {
            id: head
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 6

            Text {
                width: parent.width
                text: weather ? weather.cityLabel : ""
                color: card.fg
                font.pixelSize: card.px(13)
                elide: Text.ElideRight
            }

            // Actual: temperatura, ícono y viento, igual que la mini-app.
            Item {
                width: parent.width
                height: 64

                Column {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.32
                    spacing: 0
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: weather ? weather.tempText : ""
                        color: card.fg
                        font.pixelSize: card.px(24)
                        font.bold: true
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: weather ? qsTr("ST %1").arg(weather.feelsLikeText) : ""
                        color: card.fg
                        opacity: 0.6
                        font.pixelSize: card.px(10)
                    }
                }

                Image {
                    anchors.centerIn: parent
                    width: 52
                    height: 52
                    source: weather ? "image://icon/" + weather.iconName + win.iconSuffix : ""
                    sourceSize: Qt.size(52 * Screen.devicePixelRatio, 52 * Screen.devicePixelRatio)
                    opacity: weather && weather.stale ? 0.55 : 1.0
                }

                Column {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.32
                    spacing: 2
                    // Painted, not themed: breeze's arrow icons are toolbar line
                    // art and read as a broken glyph at this size. It points
                    // where the wind goes (the reported direction is where it
                    // comes from).
                    Canvas {
                        id: windArrow
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 20
                        height: 20
                        rotation: weather ? weather.windDirection + 180 : 0
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.fillStyle = card.fg
                            ctx.beginPath()
                            ctx.moveTo(width / 2, 2)
                            ctx.lineTo(width - 3, height - 3)
                            ctx.lineTo(width / 2, height * 0.72)
                            ctx.lineTo(3, height - 3)
                            ctx.closePath()
                            ctx.fill()
                        }
                        // A Canvas does not re-run onPaint on a colour change.
                        onRotationChanged: requestPaint()
                        Component.onCompleted: requestPaint()
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: weather ? weather.windText : ""
                        color: card.fg
                        opacity: 0.7
                        font.pixelSize: card.px(11)
                    }
                }
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: weather ? weather.conditionText : ""
                color: card.fg
                font.pixelSize: card.px(12)
            }

            Row {
                spacing: 6
                CmButton {
                    fg: card.fg
                    compact: true
                    icon: "weather-few-clouds"
                    label: qsTr("%1 días").arg(card.days.length)
                    checked: card.tab === 0
                    onClicked: card.tab = 0
                }
                CmButton {
                    fg: card.fg
                    compact: true
                    icon: "documentinfo"
                    label: qsTr("Detalles")
                    checked: card.tab === 1
                    onClicked: card.tab = 1
                }
                CmButton {
                    fg: card.fg
                    compact: true
                    icon: "view-refresh"
                    label: qsTr("Actualizar")
                    enabled: weather ? !weather.loading : false
                    onClicked: weather.refresh(true)
                }
                CmButton {
                    fg: card.fg
                    compact: true
                    icon: "configure"
                    label: qsTr("Ciudad")
                    visible: typeof weatherLauncher !== "undefined" && weatherLauncher
                    onClicked: weatherLauncher.openSettings()
                }
            }
        }

        Text {
            id: foot
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            text: {
                if (!weather) return ""
                if (weather.loading) return qsTr("Actualizando…")
                if (weather.errorText.length > 0) return weather.errorText
                if (weather.updatedText.length === 0) return ""
                return weather.stale
                       ? qsTr("Último dato: %1 (sin conexión)").arg(weather.updatedText)
                       : qsTr("Actualizado %1 · Open-Meteo.com").arg(weather.updatedText)
            }
            color: card.fg
            opacity: 0.5
            font.pixelSize: card.px(9)
            elide: Text.ElideRight
        }

        Flickable {
            anchors.top: head.bottom
            anchors.topMargin: 8
            anchors.bottom: foot.top
            anchors.bottomMargin: 4
            anchors.left: parent.left
            anchors.right: parent.right
            // AutoFlickIfNeeded and not the default AutoFlickDirection: the latter
            // calls itself flickable whenever contentHeight != height — content
            // *shorter* than the viewport included — and then steals the drag of
            // any slider inside it (see CmSlider). This one only flicks when there
            // is something to scroll.
            flickableDirection: Flickable.AutoFlickIfNeeded
            contentWidth: width
            contentHeight: Math.max(forecastRow.height, detailsColumn.height)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            // --- pronóstico ---
            Row {
                id: forecastRow
                width: parent.width
                visible: card.tab === 0
                spacing: 0

                Repeater {
                    model: card.days
                    delegate: Column {
                        required property var modelData
                        width: forecastRow.width / Math.max(1, card.days.length)
                        spacing: 3

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: modelData.dayLabel
                            color: card.fg
                            font.pixelSize: card.px(11)
                        }
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 30; height: 30
                            source: "image://icon/" + modelData.iconName + win.iconSuffix
                            sourceSize: Qt.size(30 * Screen.devicePixelRatio,
                                                30 * Screen.devicePixelRatio)
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            visible: modelData.precipProbability >= 0
                            text: modelData.precipProbability + " %"
                            color: card.fg
                            opacity: 0.6
                            font.pixelSize: card.px(10)
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: modelData.maxText
                            color: card.fg
                            font.pixelSize: card.px(11)
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: modelData.minText
                            color: card.fg
                            opacity: 0.6
                            font.pixelSize: card.px(11)
                        }
                    }
                }
            }

            // --- detalles ---
            Column {
                id: detailsColumn
                width: parent.width
                visible: card.tab === 1
                spacing: 3

                Repeater {
                    model: card.detailRows
                    delegate: Row {
                        required property var modelData
                        width: detailsColumn.width
                        spacing: 8
                        Text {
                            width: parent.width * 0.45
                            horizontalAlignment: Text.AlignRight
                            text: modelData.label
                            color: card.fg
                            opacity: 0.6
                            font.pixelSize: card.px(11)
                        }
                        Text {
                            text: modelData.text
                            color: card.fg
                            font.pixelSize: card.px(11)
                        }
                    }
                }
            }
        }
    }
}
