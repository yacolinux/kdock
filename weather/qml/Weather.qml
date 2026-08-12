// Weather.qml — the whole window: place, current conditions, and the two tabs
// (forecast / details), laid out like KDE's own weather applet, which is the
// reference the user asked for.
//
// Every string of data comes formatted from WeatherControl (`weather`): units,
// icon names, day labels and condition texts are C++ so that this window, the
// dock widget and the control panel's card cannot disagree.

import QtQuick
import QtQuick.Controls

Item {
    id: root

    readonly property int pad: 14
    // Todo texto (y todo ícono que acompaña a un texto) pasa por acá: el tamaño
    // de fuente de la ventana es configurable y 0 significa "los tamaños de
    // siempre" — mismo trato que el fontSize del panel de control.
    readonly property real fs: weatherConfig ? weatherConfig.fontScale : 1.0
    function px(size) { return Math.max(7, Math.round(size * root.fs)) }
    readonly property color fg: theme.foreground
    readonly property color dim: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                         theme.foreground.b, 0.6)
    // 0 = pronóstico, 1 = detalles.
    property int tab: 0

    // Function calls, not properties: they only re-run when something bumps this.
    property int rev: 0
    Connections {
        target: weather
        function onChanged() { root.rev++ }
    }
    readonly property var days: { root.rev; return weather ? weather.forecast() : [] }
    readonly property var detailRows: { root.rev; return weather ? weather.details() : [] }

    Rectangle {
        anchors.fill: parent
        color: theme.background
    }

    // --- Sin ciudad configurada ---------------------------------------------
    Column {
        anchors.centerIn: parent
        width: parent.width - root.pad * 4
        spacing: 12
        visible: weather ? !weather.configured : true

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Todavía no elegiste una ciudad")
            color: root.fg
            font.pixelSize: root.px(16)
            wrapMode: Text.Wrap
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Elegir una ciudad…")
            onClicked: win.openSettings()
        }
    }

    Column {
        id: content
        anchors.fill: parent
        anchors.margins: root.pad
        // The credit/updated line is anchored to the bottom, so the content has
        // to stop above it — at a large font size they used to overlap.
        anchors.bottomMargin: root.pad + footer.height + 6
        spacing: Math.round(10 * root.fs)
        visible: weather ? weather.configured : false

        // --- Encabezado: la ubicación --------------------------------------
        Row {
            width: parent.width
            spacing: 8
            Text {
                id: placeLabel
                width: parent.width - refreshButton.width - settingsButton.width - 16
                text: weather ? weather.cityLabel : ""
                color: root.fg
                font.pixelSize: root.px(17)
                elide: Text.ElideRight
            }
            ToolButton {
                id: refreshButton
                anchors.verticalCenter: placeLabel.verticalCenter
                enabled: weather ? !weather.loading : false
                onClicked: weather.refresh(true)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Actualizar ahora")
                contentItem: Image {
                    source: "image://icon/view-refresh" + win.iconSuffix()
                    sourceSize: Qt.size(16 * Screen.devicePixelRatio, 16 * Screen.devicePixelRatio)
                    width: 16; height: 16
                    opacity: refreshButton.enabled ? 1.0 : 0.4
                }
                background: Item {}
            }
            ToolButton {
                id: settingsButton
                anchors.verticalCenter: placeLabel.verticalCenter
                onClicked: win.openSettings()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Configuración")
                contentItem: Image {
                    source: "image://icon/configure" + win.iconSuffix()
                    sourceSize: Qt.size(16 * Screen.devicePixelRatio, 16 * Screen.devicePixelRatio)
                    width: 16; height: 16
                }
                background: Item {}
            }
        }

        // --- Actual: temperatura, ícono, viento ----------------------------
        Item {
            width: parent.width
            height: Math.round(96 * root.fs)

            Column {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width * 0.3
                spacing: 2
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: weather ? weather.tempText : ""
                    color: root.fg
                    font.pixelSize: root.px(30)
                    font.bold: true
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    visible: weather && weather.feelsLikeText.length > 0
                    text: weather ? qsTr("ST %1").arg(weather.feelsLikeText) : ""
                    color: root.dim
                    font.pixelSize: root.px(11)
                }
            }

            Image {
                anchors.centerIn: parent
                width: Math.round(72 * root.fs)
                height: width
                source: weather ? "image://icon/" + weather.iconName + win.iconSuffix() : ""
                sourceSize: Qt.size(width * Screen.devicePixelRatio,
                                    width * Screen.devicePixelRatio)
            }

            Column {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width * 0.3
                spacing: 2
                // Painted rather than themed: breeze's arrow icons are line art
                // meant for toolbars and read as a broken glyph at this size,
                // and the KDE applet ships its own arrow for the same reason.
                // It points where the wind *goes*, i.e. the reported direction
                // (which is where it comes from) turned around.
                Canvas {
                    id: windArrow
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.round(24 * root.fs)
                    height: width
                    rotation: weather ? weather.windDirection + 180 : 0
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.fillStyle = root.fg
                        ctx.beginPath()
                        ctx.moveTo(width / 2, 2)                 // punta
                        ctx.lineTo(width - 4, height - 3)
                        ctx.lineTo(width / 2, height * 0.72)     // muesca
                        ctx.lineTo(4, height - 3)
                        ctx.closePath()
                        ctx.fill()
                    }
                    // A Canvas does not re-run onPaint on its own: neither a
                    // scheme change (the colour) nor a font-size change (the
                    // size) would reach it otherwise.
                    onWidthChanged: requestPaint()
                    Connections {
                        target: theme
                        function onChanged() { windArrow.requestPaint() }
                    }
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: weather ? weather.windText : ""
                    color: root.dim
                    font.pixelSize: root.px(12)
                }
            }
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: weather ? weather.conditionText : ""
            color: root.fg
            font.pixelSize: root.px(14)
        }

        // --- Solapas --------------------------------------------------------
        Row {
            width: parent.width
            spacing: 0

            component TabPill: Item {
                id: pill
                required property string label
                required property int index
                width: (content.width) / 2
                height: Math.round(30 * root.fs)

                Text {
                    anchors.centerIn: parent
                    text: pill.label
                    color: root.tab === pill.index ? root.fg : root.dim
                    font.pixelSize: root.px(13)
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 2
                    color: root.tab === pill.index ? theme.highlight : "transparent"
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: root.tab = pill.index
                }
            }

            TabPill { index: 0; label: qsTr("%1 días").arg(root.days.length) }
            TabPill { index: 1; label: qsTr("Detalles") }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.15)
        }

        // --- Pronóstico -----------------------------------------------------
        Row {
            id: forecastRow
            width: parent.width
            visible: root.tab === 0
            spacing: 0

            Repeater {
                model: root.days
                delegate: Column {
                    required property var modelData
                    width: forecastRow.width / Math.max(1, root.days.length)
                    spacing: 4

                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData.dayLabel
                        color: root.fg
                        font.pixelSize: root.px(12)
                    }
                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.round(40 * root.fs); height: width
                        source: "image://icon/" + modelData.iconName + win.iconSuffix()
                        sourceSize: Qt.size(width * Screen.devicePixelRatio,
                                            width * Screen.devicePixelRatio)
                        ToolTip.visible: dayMouse.containsMouse
                        ToolTip.text: modelData.conditionText + "  ·  " + modelData.dateLabel
                        MouseArea {
                            id: dayMouse
                            anchors.fill: parent
                            hoverEnabled: true
                        }
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 3
                        visible: modelData.precipProbability >= 0
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            width: root.px(11); height: width
                            source: "image://icon/weather-showers-scattered" + win.iconSuffix()
                            sourceSize: Qt.size(width * Screen.devicePixelRatio,
                                                width * Screen.devicePixelRatio)
                            opacity: 0.8
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.precipProbability + " %"
                            color: root.dim
                            font.pixelSize: root.px(11)
                        }
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData.maxText
                        color: root.fg
                        font.pixelSize: root.px(12)
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData.minText
                        color: root.dim
                        font.pixelSize: root.px(12)
                    }
                }
            }
        }

        // --- Detalles -------------------------------------------------------
        Column {
            width: parent.width
            visible: root.tab === 1
            spacing: 4

            Repeater {
                model: root.detailRows
                delegate: Row {
                    required property var modelData
                    width: parent.width
                    spacing: 8
                    Text {
                        width: parent.width * 0.45
                        horizontalAlignment: Text.AlignRight
                        text: modelData.label
                        color: root.dim
                        font.pixelSize: root.px(13)
                    }
                    Text {
                        text: modelData.text
                        color: root.fg
                        font.pixelSize: root.px(13)
                    }
                }
            }
        }
    }

    // --- Pie: procedencia del dato y su antigüedad ---------------------------
    Row {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: root.pad
        spacing: 6
        visible: weather ? weather.configured : false

        Text {
            width: parent.width - creditLabel.width - 6
            text: {
                if (!weather) return ""
                if (weather.loading) return qsTr("Actualizando…")
                if (weather.errorText.length > 0) return weather.errorText
                if (weather.updatedText.length === 0) return ""
                return weather.stale ? qsTr("Último dato: %1 (sin conexión)").arg(weather.updatedText)
                                     : qsTr("Actualizado %1").arg(weather.updatedText)
            }
            color: root.dim
            font.pixelSize: root.px(10)
            elide: Text.ElideRight
        }
        Text {
            id: creditLabel
            text: "Open-Meteo.com"
            color: root.dim
            font.pixelSize: root.px(10)
        }
    }
}
