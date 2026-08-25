# kdock-systray: the system tray, a separate resident binary launched by the
# dock's "systray" widget.
#
# It owns the StatusNotifierItem host AND watcher for the whole session and draws
# the tray icons + their DBusMenu menus in its own window. Two reasons it lives
# outside kdock: a real toplevel surface is where an item's xdg_popup menu can
# actually parent (inside the dock it could not), and the tray host must be
# resident to keep collecting items — which it is here, the window merely hides.
#
# Like the control panel this target *is* a layer-shell surface (it anchors to a
# screen edge next to the dock), so layershell.cpp and the two protocols come
# along. It still needs **no** privileged interface: zwlr_layer_shell_v1 is not
# on KWin's restricted list, and SNI is plain session D-Bus — so the .desktop
# needs no ksycoca refresh, same as the tile menu and the control panel.

set(KDOCK_SHARED_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

qt_add_executable(kdock-systray
    src/main.cpp
    src/systray.cpp             src/systray.h
    src/systraymodel.cpp        src/systraymodel.h
    src/systrayimageprovider.cpp src/systrayimageprovider.h
    src/dbusmenu.cpp            src/dbusmenu.h
    src/systrayconfig.cpp       src/systrayconfig.h
    src/systraywindow.cpp       src/systraywindow.h
    src/systrayservice.cpp      src/systrayservice.h
    src/systraysettingsdialog.cpp src/systraysettingsdialog.h
    # Shared with kdock, unmodified:
    ${KDOCK_SHARED_SRC}/layershell.cpp   ${KDOCK_SHARED_SRC}/layershell.h
    ${KDOCK_SHARED_SRC}/theme.cpp        ${KDOCK_SHARED_SRC}/theme.h
    ${KDOCK_SHARED_SRC}/iconprovider.cpp ${KDOCK_SHARED_SRC}/iconprovider.h
    ${KDOCK_SHARED_SRC}/dockconfig.cpp   ${KDOCK_SHARED_SRC}/dockconfig.h
    ${KDOCK_SHARED_SRC}/translations.cpp ${KDOCK_SHARED_SRC}/translations.h
)

target_include_directories(kdock-systray PRIVATE ${KDOCK_SHARED_SRC})

# xdg-shell is needed even though layershell.cpp only forward-declares xdg_popup:
# the generated layer-shell C code references xdg_popup_interface.
qt_generate_wayland_protocol_client_sources(kdock-systray FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/xdg-shell.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/wlr-layer-shell-unstable-v1.xml
)

qt_add_resources(kdock-systray "systray-qml"
    PREFIX "/"
    FILES
        qml/Systray.qml
        qml/SystrayMenu.qml
)

# kdock's IconMenuItem, verbatim. BASE ".." lands it on :/qml/IconMenuItem.qml,
# the same resource directory as the files above, so SystrayMenu can use it as a
# plain neighbouring type with no import.
qt_add_resources(kdock-systray "systray-shared-qml"
    PREFIX "/"
    BASE ".."
    FILES ../qml/IconMenuItem.qml
)

# The four base translation layers (never the ALT ones: BaseOnly). BASE ".."
# lands them on :/translations, where seedFiles() looks.
qt_add_resources(kdock-systray "systray-translations"
    PREFIX "/"
    BASE ".."
    FILES ../translations/capabase.md ../translations/english.md
          ../translations/spanish.md ../translations/zh-CN.md
)

# The in-tree layer-shell integration is a static Qt plugin.
target_compile_definitions(kdock-systray PRIVATE QT_STATICPLUGIN)

target_link_libraries(kdock-systray PRIVATE
    Qt6::CorePrivate
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::WaylandClient
    Qt6::WaylandClientPrivate
    Qt6::DBus
)

install(TARGETS kdock-systray RUNTIME DESTINATION bin)

# No privileged interfaces here (layer-shell needs none, SNI is session D-Bus),
# so this .desktop only names the process and gives it an icon. Installing it
# needs no ksycoca refresh.
configure_file(kdock-systray.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/kdock-systray.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kdock-systray.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
