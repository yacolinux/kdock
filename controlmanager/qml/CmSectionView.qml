// One section's content, at either size.
//
// This is the single place that maps a section id to its .qml, so a card on the
// Principal grid and a whole tab are the same component with `compact` flipped.
// Adding a section is a row in CmSections plus a case here.

import QtQuick

Loader {
    id: view

    property string sectionId: ""
    property bool compact: false
    // The colour every text of the loaded section is drawn in: the card's
    // contrast answer on Principal, the panel's on a full tab.
    property color fg: theme.foreground

    asynchronous: false
    source: {
        switch (view.sectionId) {
        case "clock":     return "cards/ClockCard.qml"
        case "audio":     return "cards/AudioCard.qml"
        case "video":     return "cards/VideoCard.qml"
        case "calendar":  return "cards/CalendarCard.qml"
        case "play":      return "cards/PlayCard.qml"
        case "network":   return "cards/NetworkCard.qml"
        case "weather":   return "cards/WeatherCard.qml"
        case "wallpaper": return "cards/WallpaperCard.qml"
        case "wallpaperqt": return "cards/WallpaperQtCard.qml"
        case "system":    return "cards/SystemCard.qml"
        case "appearance": return "cards/AppearanceCard.qml"
        case "desktops":  return "cards/DesktopsCard.qml"
        case "colorauto": return "cards/ColorAutoCard.qml"
        case "screensaver": return "cards/ScreensaverCard.qml"
        }
        // No case = an empty Loader, i.e. a tab that opens onto nothing and a
        // card that draws nothing, with no error anywhere. tests/static checks
        // this switch against CmSections' table for exactly that reason.
        return ""
    }

    // `compact` is set on the loaded item rather than declared required on it,
    // so a card can be instantiated by hand in a probe without a Loader.
    onLoaded: {
        if (item) {
            item.compact = view.compact
            item.fg = view.fg
        }
    }
    onCompactChanged: if (item) item.compact = view.compact

    // Pushed the same way, and not with a Binding element: the default property
    // of a Loader is `sourceComponent`, so a Binding declared in this body is
    // read as the thing to load and the card ends up fighting its own `fg`
    // (measured: "Binding loop detected for property fg" once per card).
    onFgChanged: if (item) item.fg = view.fg
}
