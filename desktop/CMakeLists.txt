# kdock-desktop: the desktop-widget manager, a repurposed copy of
# kdock-controlmanager (controlmanager/). Same cards and backends, but the
# window is a full-screen, always-on, transparent layer-shell surface that sits
# *below* the dock and lets ordinary windows cover it — a canvas the user drops
# widgets onto, not a panel that opens and closes. See src/cmwindow.cpp here for
# the differences (layer=bottom, exclusive zone -1, all four edges anchored).
#
# It is a full copy on purpose (the request: "a copy of its code with another
# name"): kdock-controlmanager is left completely untouched so it can keep being
# the panel while this binary is grown into the desktop layer.
#
# Like the control panel it *is* a layer-shell surface and still needs **no**
# privileged interface (zwlr_layer_shell_v1 is advertised to unprivileged
# clients), so its .desktop needs no ksycoca refresh.
#
# The cm-specific sources live here (desktop/src); everything shared with kdock
# is pulled from ../src, exactly as controlmanager/ does.

set(KDOCK_SHARED_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

qt_add_executable(kdock-desktop
    src/main.cpp
    src/cmconfig.cpp          src/cmconfig.h
    src/cmsections.cpp        src/cmsections.h
    src/cmlayout.cpp          src/cmlayout.h
    src/cmmodel.cpp           src/cmmodel.h
    src/cmwindow.cpp          src/cmwindow.h
    src/cmsettingsdialog.cpp  src/cmsettingsdialog.h
    src/cmservice.cpp         src/cmservice.h
    src/cmbackends.h
    # New backends, specific to this binary family:
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
    ${KDOCK_SHARED_SRC}/themepicker.cpp      ${KDOCK_SHARED_SRC}/themepicker.h
    ${KDOCK_SHARED_SRC}/appearancecontrol.cpp ${KDOCK_SHARED_SRC}/appearancecontrol.h
    ${KDOCK_SHARED_SRC}/virtualdesktops.cpp  ${KDOCK_SHARED_SRC}/virtualdesktops.h
    ${KDOCK_SHARED_SRC}/dockconfig.cpp       ${KDOCK_SHARED_SRC}/dockconfig.h
    ${KDOCK_SHARED_SRC}/translations.cpp     ${KDOCK_SHARED_SRC}/translations.h
)

target_include_directories(kdock-desktop PRIVATE ${KDOCK_SHARED_SRC})

qt_generate_wayland_protocol_client_sources(kdock-desktop FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/xdg-shell.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/wlr-layer-shell-unstable-v1.xml
)

qt_add_resources(kdock-desktop "desktop-qml"
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
        qml/cards/ScreensaverCard.qml
)

# WallpaperWindow's own QML (shared with kdock, qrc:/qml/Wallpaper.qml)
qt_add_resources(kdock-desktop "desktop-wallpaper-qml"
    PREFIX "/"
    BASE ".."
    FILES ../qml/Wallpaper.qml
)

# kdock's IconMenuItem, verbatim (lands on :/qml/IconMenuItem.qml).
qt_add_resources(kdock-desktop "desktop-shared-qml"
    PREFIX "/"
    BASE ".."
    FILES ../qml/IconMenuItem.qml
)

# The four base translation layers (never the ALT ones: this binary resolves to
# the base, like the control panel).
qt_add_resources(kdock-desktop "desktop-translations"
    PREFIX "/"
    BASE ".."
    FILES ../translations/capabase.md ../translations/english.md
          ../translations/spanish.md ../translations/zh-CN.md
)

# The in-tree layer-shell integration is a static Qt plugin.
target_compile_definitions(kdock-desktop PRIVATE QT_STATICPLUGIN)

target_link_libraries(kdock-desktop PRIVATE
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

install(TARGETS kdock-desktop RUNTIME DESTINATION bin)

# No privileged interfaces here (layer-shell needs none), so this .desktop only
# exists to give the process a name and an icon. Installing it needs no ksycoca
# refresh.
configure_file(kdock-desktop.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/kdock-desktop.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kdock-desktop.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
