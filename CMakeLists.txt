cmake_minimum_required(VERSION 3.21)
project(kdock VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core CorePrivate Gui Qml Quick Widgets WaylandClient WaylandClientPrivate DBus)

qt_standard_project_setup()

qt_add_executable(kdock
    src/main.cpp
    src/layershell.cpp        src/layershell.h
    src/windowmonitor.cpp     src/windowmonitor.h
    src/taskmanager.cpp       src/taskmanager.h
    src/plasmawindowmonitor.cpp src/plasmawindowmonitor.h
    src/desktopentry.cpp      src/desktopentry.h
    src/dockmodel.cpp         src/dockmodel.h
    src/theme.cpp             src/theme.h
    src/iconprovider.cpp      src/iconprovider.h
    src/iconcolorprovider.cpp src/iconcolorprovider.h
    src/appmenu.cpp           src/appmenu.h
    src/xdgmenutree.cpp       src/xdgmenutree.h
    src/dockconfig.cpp        src/dockconfig.h
    src/dockwindow.cpp        src/dockwindow.h
    src/dockmanager.cpp       src/dockmanager.h
    src/settingsdialog.cpp    src/settingsdialog.h
    src/coloredtabbar.cpp     src/coloredtabbar.h
    src/volumecontrol.cpp     src/volumecontrol.h
    src/audiocontrol.cpp      src/audiocontrol.h
    src/clockwidget.cpp       src/clockwidget.h
    src/clockwidget2.cpp      src/clockwidget2.h
    src/clipboardhistory.cpp  src/clipboardhistory.h
    src/waylandclipboard.cpp  src/waylandclipboard.h
    src/overviewcontrol.cpp   src/overviewcontrol.h
    src/desktopcontrol.cpp    src/desktopcontrol.h
    src/virtualdesktops.cpp   src/virtualdesktops.h
    src/monitorcontrol.cpp    src/monitorcontrol.h
    src/maxmincontrol.cpp     src/maxmincontrol.h
    src/activewindowcontrol.cpp src/activewindowcontrol.h
    src/kwinshortcut.cpp      src/kwinshortcut.h
    src/wallpapercontrol.cpp  src/wallpapercontrol.h
    src/desktopwallpapers.cpp src/desktopwallpapers.h
    src/desktopmaximize.cpp   src/desktopmaximize.h
    src/plasmascript.h
    src/brightnesscontrol.cpp src/brightnesscontrol.h
    src/batterycontrol.cpp    src/batterycontrol.h
    src/systray.cpp           src/systray.h
    src/dbusmenu.cpp          src/dbusmenu.h
    src/systraymodel.cpp      src/systraymodel.h
    src/systrayimageprovider.cpp src/systrayimageprovider.h
    src/relanzadorconfig.cpp  src/relanzadorconfig.h
    src/relanzadormodel.cpp   src/relanzadormodel.h
    src/relanzadoresmanager.cpp src/relanzadoresmanager.h
    src/scriptrunnerconfig.cpp  src/scriptrunnerconfig.h
    src/scriptrunnersmanager.cpp src/scriptrunnersmanager.h
    src/iconpickerdialog.cpp    src/iconpickerdialog.h
    src/themepicker.cpp         src/themepicker.h
    src/configarchive.cpp       src/configarchive.h
    src/powercontrol.cpp        src/powercontrol.h
    src/diskscontrol.cpp        src/diskscontrol.h
    src/nmdbus.cpp              src/nmdbus.h
    src/networkcontrol.cpp      src/networkcontrol.h
    src/networksettings.cpp     src/networksettings.h
    src/networksettingswidget.cpp src/networksettingswidget.h
    src/connectioneditor.cpp    src/connectioneditor.h
    src/routesdialog.cpp        src/routesdialog.h
    src/appearancecontrol.cpp   src/appearancecontrol.h
    src/darkmodeappearance.cpp  src/darkmodeappearance.h
    src/previewslauncher.cpp    src/previewslauncher.h
    src/tilemenulauncher.cpp    src/tilemenulauncher.h
)

qt_generate_wayland_protocol_client_sources(kdock FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/xdg-shell.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-foreign-toplevel-management-unstable-v1.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/plasma-window-management.xml
    # Clipboard access without keyboard focus (see src/waylandclipboard.h).
    # Unlike plasma-window-management this one is *not* in KWin's restricted
    # interface list, so no .desktop privilege is involved.
    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/ext-data-control-v1.xml
)

qt_add_resources(kdock "qml"
    PREFIX "/"
    FILES qml/Dock.qml qml/RelanzadorWidget.qml qml/RelanzadorPopup.qml qml/AppMenuPopup.qml qml/IconMenuItem.qml qml/SystrayMenu.qml qml/BackgroundColorMenu.qml qml/ModeMenu.qml qml/IconLabelMenu.qml qml/WidgetLabelMenu.qml qml/ScriptRunnerWidget.qml qml/ClipboardPopup.qml qml/DisksPopup.qml qml/NetworkPopup.qml qml/ThemeListPopup.qml
)

# The in-tree layer-shell integration is built as a static Qt plugin
target_compile_definitions(kdock PRIVATE QT_STATICPLUGIN)

target_link_libraries(kdock PRIVATE
    Qt6::CorePrivate
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Widgets
    Qt6::WaylandClient
    Qt6::WaylandClientPrivate
    Qt6::DBus
)

include(GNUInstallDirs)

install(TARGETS kdock RUNTIME DESTINATION bin)

# KWin only grants the restricted org_kde_plasma_window_management interface
# (needed to see running windows) when the client's *absolute* executable path
# matches an Exec= line in a kdock.desktop it can find. A relative "Exec=kdock"
# never matches, so substitute the real install path here.
configure_file(kdock.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/kdock.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kdock.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# Accessory binary: the window-preview strips (see previews/ and PLAN.md). It
# runs alongside kdock, keeps its own config, and is toggled from Settings →
# Previews. Deliberately a separate target so nothing here changes for kdock.
add_subdirectory(previews)
add_subdirectory(tilemenu)
add_subdirectory(calendar)
