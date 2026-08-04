# kdock-tilemenu: the full-screen tile menu, a separate binary launched by the
# dock's "tilemenu" widget (see PLAN-TILE.md).
#
# Unlike kdock-previews this target needs **no** Wayland protocol of its own and
# no privileged interface: the menu is a plain maximized toplevel, so KWin's
# maximize area (which already subtracts the docks' layer-shell struts) does all
# the geometry work. That is why layershell.cpp is absent from the list below.
#
# A handful of kdock sources are listed here instead of being factored into a
# shared library: they are self-contained, and compiling them twice keeps the
# kdock target itself completely untouched (same call as previews/).

set(KDOCK_SHARED_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

qt_add_executable(kdock-tilemenu
    src/main.cpp
    src/tileconfig.cpp          src/tileconfig.h
    src/tilelayout.cpp          src/tilelayout.h
    src/tilemodel.cpp           src/tilemodel.h
    src/tilewindow.cpp          src/tilewindow.h
    src/tilesettingsdialog.cpp  src/tilesettingsdialog.h
    src/tilemenuservice.cpp     src/tilemenuservice.h
    # Shared with kdock, unmodified:
    ${KDOCK_SHARED_SRC}/desktopentry.cpp     ${KDOCK_SHARED_SRC}/desktopentry.h
    ${KDOCK_SHARED_SRC}/appmenu.cpp          ${KDOCK_SHARED_SRC}/appmenu.h
    ${KDOCK_SHARED_SRC}/xdgmenutree.cpp      ${KDOCK_SHARED_SRC}/xdgmenutree.h
    ${KDOCK_SHARED_SRC}/iconprovider.cpp     ${KDOCK_SHARED_SRC}/iconprovider.h
    ${KDOCK_SHARED_SRC}/theme.cpp            ${KDOCK_SHARED_SRC}/theme.h
    ${KDOCK_SHARED_SRC}/powercontrol.cpp     ${KDOCK_SHARED_SRC}/powercontrol.h
    ${KDOCK_SHARED_SRC}/iconpickerdialog.cpp ${KDOCK_SHARED_SRC}/iconpickerdialog.h
    # DockConfig comes along for two reasons: Theme asks it where the shared
    # settings file lives (icon-theme override), and AppMenu takes one for the
    # menu favorites — so this menu and the dock's menu share one favorites list
    # without a copy of either.
    ${KDOCK_SHARED_SRC}/dockconfig.cpp       ${KDOCK_SHARED_SRC}/dockconfig.h
)

target_include_directories(kdock-tilemenu PRIVATE ${KDOCK_SHARED_SRC})

qt_add_resources(kdock-tilemenu "tilemenu-qml"
    PREFIX "/"
    FILES
        qml/TileMenu.qml
        qml/TileMenuTile.qml
        qml/TileGroupTabs.qml
        qml/TileSidebar.qml
        qml/TileSizeMenu.qml
)

# kdock's IconMenuItem, verbatim. BASE lands it on :/qml/IconMenuItem.qml, i.e.
# the same resource directory as the files above, which is what lets the tile
# menus use it as a plain neighbouring type with no import.
qt_add_resources(kdock-tilemenu "tilemenu-shared-qml"
    PREFIX "/"
    BASE ".."
    FILES ../qml/IconMenuItem.qml
)

target_link_libraries(kdock-tilemenu PRIVATE
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Widgets
    Qt6::DBus
)

install(TARGETS kdock-tilemenu RUNTIME DESTINATION bin)

# No privileged interfaces here, so this .desktop only exists to give the window
# a name and an icon in the task switcher (and something for a KWin rule to
# match on). Installing it needs no ksycoca refresh.
configure_file(kdock-tilemenu.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/kdock-tilemenu.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kdock-tilemenu.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
