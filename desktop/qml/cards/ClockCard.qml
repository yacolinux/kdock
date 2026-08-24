// Big clock. The one card-only section: a whole tab showing nothing but the
// time would be a tab wasted.
//
// The timer runs at 1 s but only while the panel is visible — a hidden panel
// still has its QML alive (it is hide(), never destroy()), and a clock ticking
// behind a surface nobody can see is a wakeup per second for nothing.

import QtQuick

Item {
    id: card

    property bool compact: false
    // Text colour, pushed in by CmSectionView: the card's contrast answer on
    // Principal, the panel's on a full tab. Every text of this file uses it.
    property color fg: theme.foreground

    property date now: new Date()

    // The attached property has to be read *here*, on an Item. Reading it from
    // inside the Timer attaches Window to the Timer itself, which logs
    // "Window.window does only support types deriving from Item" and leaves the
    // clock frozen (caught by the Xvfb harness on the first run).
    readonly property bool panelVisible: Window.window ? Window.window.visible : true

    Timer {
        interval: 1000
        repeat: true
        running: card.panelVisible
        onTriggered: card.now = new Date()
    }

    // Click the clock, get the calendar — the same reflex the dock's clock
    // widget trains (there a left click opens kdock-calendar; here the calendar
    // is a tab away, so it just goes there). Only when that tab actually
    // exists: with the section disabled the click would set a currentTab the
    // tab bar does not draw, and the panel would look empty.
    readonly property bool calendarReachable: cmConfig.sectionEnabled("calendar")
                                              && cmConfig.visibleTabs().indexOf("calendar") >= 0

    MouseArea {
        anchors.fill: parent
        enabled: card.calendarReachable
        cursorShape: Qt.PointingHandCursor
        onClicked: win.currentTab = "calendar"
    }

    // The size the card can afford for a line, before the user's font scale is
    // applied. The scale still grows the text on a card that has room, but it
    // cannot push it past the card: this is a fixed 1x1…6x3 grid, so "bigger
    // font" has to mean "as big as fits" once the card runs out.
    //
    // The height budget is a cap; the width is left to Qt's HorizontalFit, which
    // shrinks the string that is actually drawn (the time is 5 characters and
    // the date is twenty-odd, so a single formula for both would waste the card
    // on one of them).
    function fitted(natural, heightBudget) {
        return Math.max(7, Math.round(Math.min(Math.max(natural, 7) * cmConfig.fontScale,
                                               heightBudget)))
    }

    Column {
        anchors.centerIn: parent
        width: card.width - 8
        spacing: card.compact ? 0 : 6

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: Qt.formatDateTime(card.now, "HH:mm")
            color: card.fg
            // HorizontalFit: pixelSize is the *maximum*, and Qt walks it down to
            // minimumPixelSize until the string fits the width it was given.
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 7
            font.pixelSize: card.fitted(Math.max(20, Math.min(card.height * 0.42,
                                                              card.width * 0.30)),
                                        card.height * (card.compact ? 0.52 : 0.46))
            font.bold: true
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: Qt.formatDateTime(card.now, "ddd d MMM yyyy")
            color: card.fg
            opacity: 0.7
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 7
            font.pixelSize: card.fitted(Math.max(10, Math.min(card.height * 0.13,
                                                              card.width * 0.10)),
                                        card.height * 0.22)
            font.bold: cmConfig.labelBold
        }
        Text {
            visible: !card.compact && card.height > 150
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: Qt.formatDateTime(card.now, "ss") + " s"
            color: card.fg
            opacity: 0.45
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 7
            font.pixelSize: card.fitted(12, card.height * 0.14)
        }
    }
}
