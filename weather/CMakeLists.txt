# kdock-weather: the weather mini-app, a separate binary launched by the dock's
# "weather" widget (and by the control panel's Clima card).
#
# Like kdock-tilemenu and kdock-calendar this is an **ordinary toplevel**, not a
# layer-shell surface: no Wayland protocol, no privileged interface, no ksycoca
# refresh, and — what matters day to day — it runs straight from build/ and can
# be screenshotted whole under Xvfb.
#
# Single-instance through its D-Bus name but deliberately **not resident**:
# closing the window quits the process, so an install is never shadowed by an old
# one still mapped in memory (the trap the other three accessories have).
#
# The backend (weathercontrol/weatherconfig) is compiled here *and* in kdock and
# kdock-controlmanager: the three surfaces share the same source so they cannot
# disagree, exactly like networkcontrol.cpp already does.

set(KDOCK_SHARED_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

qt_add_executable(kdock-weather
    src/main.cpp
    src/weatherwindow.cpp          src/weatherwindow.h
    src/weatherservice.cpp         src/weatherservice.h
    src/weathersettingsdialog.cpp  src/weathersettingsdialog.h
    # Shared with kdock, unmodified:
    ${KDOCK_SHARED_SRC}/weathercontrol.cpp  ${KDOCK_SHARED_SRC}/weathercontrol.h
    ${KDOCK_SHARED_SRC}/weatherconfig.cpp   ${KDOCK_SHARED_SRC}/weatherconfig.h
    ${KDOCK_SHARED_SRC}/theme.cpp           ${KDOCK_SHARED_SRC}/theme.h
    ${KDOCK_SHARED_SRC}/iconprovider.cpp    ${KDOCK_SHARED_SRC}/iconprovider.h
    # DockConfig comes along for one reason: Theme asks it where the shared
    # settings file lives, and iconSuffix() reads the dock's two widget icon
    # sets from it so this window resolves icons the same way the dock does.
    ${KDOCK_SHARED_SRC}/dockconfig.cpp      ${KDOCK_SHARED_SRC}/dockconfig.h
    ${KDOCK_SHARED_SRC}/translations.cpp    ${KDOCK_SHARED_SRC}/translations.h
)

target_include_directories(kdock-weather PRIVATE ${KDOCK_SHARED_SRC})

qt_add_resources(kdock-weather "weather-qml"
    PREFIX "/"
    FILES qml/Weather.qml
)

# Las cuatro capas base de traducción (nunca las ALT: este binario resuelve
# siempre a la base, ver Translations::BaseOnly). Con BASE ".." caen en
# :/translations, que es donde las busca seedFiles().
qt_add_resources(kdock-weather "weather-translations"
    PREFIX "/"
    BASE ".."
    FILES ../translations/capabase.md ../translations/english.md
          ../translations/spanish.md ../translations/zh-CN.md
)

target_link_libraries(kdock-weather PRIVATE
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::Widgets
    Qt6::DBus
)

install(TARGETS kdock-weather RUNTIME DESTINATION bin)

# No privileged interfaces here, so this .desktop only exists to give the window
# a name and an icon in the task switcher. Installing it needs no ksycoca
# refresh.
configure_file(kdock-weather.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/kdock-weather.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kdock-weather.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
