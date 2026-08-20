# kdock-controlmanager: the control panel, a separate binary launched by the
# dock's "controlmanager" widget (see PLAN-MANAGER.md).
#
# Unlike kdock-tilemenu this target *is* a layer-shell surface: the panel has a
# size of its own and has to hang off a screen edge where the user asks, and in
# Wayland a client cannot place an ordinary toplevel. So layershell.cpp and the
# two protocols come along, exactly as in previews/.
#
# It still needs **no** privileged interface: zwlr_layer_shell_v1 is advertised
# to unprivileged clients (verified with wayland-info on this session), so this
# .desktop needs no ksycoca refresh — same as the tile menu.
#
# A handful of kdock sources are listed here instead of being factored into a
# shared library: they are self-contained, and compiling them twice keeps the
# kdock target itself completely untouched (same call as previews/ and tilemenu/).

set(KDOCK_SHARED_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

qt_add_executable(kdock-controlmanager
    src/main.cpp
    src/cmconfig.cpp          src/cmconfig.h
    src/cmsections.cpp        src/cmsections.h
    src/cmlayout.cpp          src/cmlayout.h
    src/cmmodel.cpp           src/cmmodel.h
    src/cmwindow.cpp          src/cmwindow.h
    src/cmsettingsdialog.cpp  src/cmsettingsdialog.h
    src/cmservice.cpp         src/cmservice.h
    src/cmbackends.h
    # New backends, specific to this binary:
    src/mpriscontrol.cpp      src/mpriscontrol.h
    src/docklink.cpp          src/docklink.h
    # Shared with kdock, unmodified:
    ${KDOCK_SHARED_SRC}/layershell.cpp       ${KDOCK_SHARED_SRC}/layershell.h
    ${KDOCK_SHARED_SRC}/theme.cpp            ${KDOCK_SHARED_SRC}/theme.h
    ${KDOCK_SHARED_SRC}/iconprovider.cpp     ${KDOCK_SHARED_SRC}/iconprovider.h
    ${KDOCK_SHARED_SRC}/desktopentry.cpp     ${KDOCK_SHARED_SRC}/desktopentry.h
    ${KDOCK_SHARED_SRC}/powercontrol.cpp     ${KDOCK_SHARED_SRC}/powercontrol.h
    ${KDOCK_SHARED_SRC}/session.cpp          ${KDOCK_SHARED_SRC}/session.h
    ${KDOCK_SHARED_SRC}/audiocontrol.cpp     ${KDOCK_SHARED_SRC}/audiocontrol.h
    ${KDOCK_SHARED_SRC}/batterycontrol.cpp   ${KDOCK_SHARED_SRC}/batterycontrol.h
    ${KDOCK_SHARED_SRC}/brightnesscontrol.cpp ${KDOCK_SHARED_SRC}/brightnesscontrol.h
    ${KDOCK_SHARED_SRC}/screenbrightness.cpp ${KDOCK_SHARED_SRC}/screenbrightness.h
    ${KDOCK_SHARED_SRC}/networkcontrol.cpp   ${KDOCK_SHARED_SRC}/networkcontrol.h
    ${KDOCK_SHARED_SRC}/weathercontrol.cpp   ${KDOCK_SHARED_SRC}/weathercontrol.h
    ${KDOCK_SHARED_SRC}/weatherconfig.cpp    ${KDOCK_SHARED_SRC}/weatherconfig.h
    ${KDOCK_SHARED_SRC}/weatherlauncher.cpp  ${KDOCK_SHARED_SRC}/weatherlauncher.h
    ${KDOCK_SHARED_SRC}/nmdbus.cpp           ${KDOCK_SHARED_SRC}/nmdbus.h
    ${KDOCK_SHARED_SRC}/wallpapercontrol.cpp ${KDOCK_SHARED_SRC}/wallpapercontrol.h
    ${KDOCK_SHARED_SRC}/wallpaperfolder.cpp  ${KDOCK_SHARED_SRC}/wallpaperfolder.h
    ${KDOCK_SHARED_SRC}/wallpaperwindow.cpp  ${KDOCK_SHARED_SRC}/wallpaperwindow.h
    ${KDOCK_SHARED_SRC}/lxqtwallpapers.cpp   ${KDOCK_SHARED_SRC}/lxqtwallpapers.h
    ${KDOCK_SHARED_SRC}/desktopwallpapers.cpp ${KDOCK_SHARED_SRC}/desktopwallpapers.h
    ${KDOCK_SHARED_SRC}/plasmascript.h
    ${KDOCK_SHARED_SRC}/iconpickerdialog.cpp ${KDOCK_SHARED_SRC}/iconpickerdialog.h
    # The icon-theme picker of the settings dialog (and AppearanceControl, the
    # theme lists + favorites it reads).
    ${KDOCK_SHARED_SRC}/themepicker.cpp      ${KDOCK_SHARED_SRC}/themepicker.h
    ${KDOCK_SHARED_SRC}/appearancecontrol.cpp ${KDOCK_SHARED_SRC}/appearancecontrol.h
    # Los escritorios virtuales de KWin, para la sección Escritorios.
    ${KDOCK_SHARED_SRC}/virtualdesktops.cpp  ${KDOCK_SHARED_SRC}/virtualdesktops.h
    # DockConfig comes along for two reasons: Theme asks it where the shared
    # settings file lives (icon-theme override), and the card colour menu offers
    # the same eight quick colours the dock's own submenu does.
    ${KDOCK_SHARED_SRC}/dockconfig.cpp       ${KDOCK_SHARED_SRC}/dockconfig.h
    ${KDOCK_SHARED_SRC}/translations.cpp     ${KDOCK_SHARED_SRC}/translations.h
)

target_include_directories(kdock-controlmanager PRIVATE ${KDOCK_SHARED_SRC})

# xdg-shell is needed even though layershell.cpp only forward-declares xdg_popup:
# the generated layer-shell C code references xdg_popup_interface
# (zwlr_layer_surface_v1.get_popup takes one).
qt_generate_wayland_protocol_client_sources(kdock-controlmanager FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/xdg-shell.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/wlr-layer-shell-unstable-v1.xml
)

qt_add_resources(kdock-controlmanager "controlmanager-qml"
    PREFIX "/"
    FILES
        qml/ControlManager.qml
        qml/CmTabs.qml
        qml/CmCard.qml
        qml/CmCornerGrip.qml
        qml/CmSlider.qml
        qml/CmSpinBox.qml
        qml/CmButton.qml
        qml/CmSectionView.qml
        qml/cards/ClockCard.qml
        qml/cards/AudioCard.qml
        qml/cards/VideoCard.qml
        qml/cards/CalendarCard.qml
        qml/cards/PlayCard.qml
        qml/cards/NetworkCard.qml
        qml/cards/WeatherCard.qml
        qml/cards/WallpaperCard.qml
        qml/cards/WallpaperQtCard.qml
        qml/cards/SystemCard.qml
        qml/cards/AppearanceCard.qml
        qml/cards/DesktopsCard.qml
        qml/cards/ColorAutoCard.qml
)

# WallpaperWindow's own QML (shared with kdock, qrc:/qml/Wallpaper.qml)
qt_add_resources(kdock-controlmanager "controlmanager-wallpaper-qml"
    PREFIX "/"
    BASE ".."
    FILES ../qml/Wallpaper.qml
)

# kdock's IconMenuItem, verbatim. BASE lands it on :/qml/IconMenuItem.qml, i.e.
# the same resource directory as the files above, which is what lets the card
# menus use it as a plain neighbouring type with no import.
qt_add_resources(kdock-controlmanager "controlmanager-shared-qml"
    PREFIX "/"
    BASE ".."
    FILES ../qml/IconMenuItem.qml
)

# Las cuatro capas base de traducción (nunca las ALT: este binario resuelve
# siempre a la base, ver Translations::BaseOnly). Con BASE ".." caen en
# :/translations, que es donde las busca seedFiles().
qt_add_resources(kdock-controlmanager "controlmanager-translations"
    PREFIX "/"
    BASE ".."
    FILES ../translations/capabase.md ../translations/english.md
          ../translations/spanish.md ../translations/zh-CN.md
)

# The in-tree layer-shell integration is a static Qt plugin.
target_compile_definitions(kdock-controlmanager PRIVATE QT_STATICPLUGIN)

target_link_libraries(kdock-controlmanager PRIVATE
    Qt6::CorePrivate
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::WaylandClient
    Qt6::WaylandClientPrivate
    Qt6::DBus
)

install(TARGETS kdock-controlmanager RUNTIME DESTINATION bin)

# No privileged interfaces here (layer-shell needs none), so this .desktop only
# exists to give the panel a name and an icon. Installing it needs no ksycoca
# refresh.
configure_file(kdock-controlmanager.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/kdock-controlmanager.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kdock-controlmanager.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
