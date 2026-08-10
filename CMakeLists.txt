cmake_minimum_required(VERSION 3.21)
project(kdock VERSION 0.1.11 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core CorePrivate Gui Qml Quick Widgets WaylandClient WaylandClientPrivate DBus)

qt_standard_project_setup()

# Everything except main.cpp lives in an OBJECT library so the test binaries can
# link the very same objects the dock is made of (tests/CMakeLists.txt). Before
# this, a probe had to scrape build/CMakeFiles/kdock.dir for *.o by hand.
#
# OBJECT and not STATIC on purpose: every object ends up in the final link, so
# the qt_add_resources initialisers below and the static layer-shell plugin
# (Q_PLUGIN_METADATA in src/layershell.cpp; Q_IMPORT_PLUGIN stays in main.cpp)
# survive without any Q_INIT_RESOURCE bookkeeping.
#
# The three accessory binaries are deliberately NOT built on this: they compile
# their handful of ../src files themselves, which is what keeps them decoupled
# from this target (see the comment at the top of controlmanager/CMakeLists.txt).
add_library(kdock_core OBJECT
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
    src/translations.cpp        src/translations.h
    src/previewslauncher.cpp    src/previewslauncher.h
    src/tilemenulauncher.cpp    src/tilemenulauncher.h
    src/controlmanagerlauncher.cpp src/controlmanagerlauncher.h
    src/apprestart.cpp          src/apprestart.h
    src/dockservice.cpp         src/dockservice.h
    src/globalshortcut.cpp      src/globalshortcut.h
)

qt_generate_wayland_protocol_client_sources(kdock_core FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/xdg-shell.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-foreign-toplevel-management-unstable-v1.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/plasma-window-management.xml
    # Clipboard access without keyboard focus (see src/waylandclipboard.h).
    # Unlike plasma-window-management this one is *not* in KWin's restricted
    # interface list, so no .desktop privilege is involved.
    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/ext-data-control-v1.xml
)

qt_add_resources(kdock_core "qml"
    PREFIX "/"
    FILES qml/Dock.qml qml/RelanzadorWidget.qml qml/RelanzadorPopup.qml qml/AppMenuPopup.qml qml/IconMenuItem.qml qml/SubMenuDelegate.qml qml/SystrayMenu.qml qml/BackgroundColorMenu.qml qml/ModeMenu.qml qml/IconLabelMenu.qml qml/WidgetLabelMenu.qml qml/ScriptRunnerWidget.qml qml/ClipboardPopup.qml qml/DisksPopup.qml qml/NetworkPopup.qml qml/ThemeListPopup.qml
)

# Shipped translation layers. They are copied to
# ~/.local/share/kdock/translations on first run (Translations::seedFiles) and
# edited there; regenerate capabase.md with tools/gen-capabase.py.
qt_add_resources(kdock_core "translations"
    PREFIX "/"
    FILES translations/capabase.md translations/english.md translations/spanish.md
          translations/zh-CN.md
          translations/english-ALT-startrek.md
          translations/english-ALT-hacker.md
          translations/english-ALT-starwars.md
          translations/spanish-ALT-startrek.md
          translations/spanish-ALT-hacker.md
          translations/spanish-ALT-starwars.md
          translations/zh-CN-ALT-startrek.md
          translations/zh-CN-ALT-hacker.md
          translations/zh-CN-ALT-starwars.md
)

# The in-tree layer-shell integration is built as a static Qt plugin. PUBLIC so
# main.cpp's Q_IMPORT_PLUGIN compiles with the same definition.
target_compile_definitions(kdock_core PUBLIC QT_STATICPLUGIN)

target_link_libraries(kdock_core PUBLIC
    Qt6::CorePrivate
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Widgets
    Qt6::WaylandClient
    Qt6::WaylandClientPrivate
    Qt6::DBus
)
# Tests include headers as "dockconfig.h", same as the sources do.
target_include_directories(kdock_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

qt_add_executable(kdock src/main.cpp)
target_link_libraries(kdock PRIVATE kdock_core)

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
add_subdirectory(controlmanager)

# Test suite (tests/README.md). Off by default so a plain build of the dock
# needs neither Qt6::Test nor the extra targets; tests/run.sh turns it on.
option(KDOCK_BUILD_TESTS "Build the test suite (ctest)" OFF)
if(KDOCK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
