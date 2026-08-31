# kdock-clipboard: standalone clipboard history window.
#
# The history backend is intentionally compiled only into this target. kdock
# knows this binary through ClipboardLauncher and D-Bus, so a clipboard offer,
# image decoder or window bug cannot take the dock process down.

set(KDOCK_SHARED_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

qt_add_executable(kdock-clipboard
    src/main.cpp
    src/clipboardwindow.cpp src/clipboardwindow.h
    src/clipboardservice.cpp src/clipboardservice.h
    ${KDOCK_SHARED_SRC}/clipboardhistory.cpp ${KDOCK_SHARED_SRC}/clipboardhistory.h
    ${KDOCK_SHARED_SRC}/waylandclipboard.cpp ${KDOCK_SHARED_SRC}/waylandclipboard.h
    ${KDOCK_SHARED_SRC}/iconprovider.cpp ${KDOCK_SHARED_SRC}/iconprovider.h
    ${KDOCK_SHARED_SRC}/desktopentry.cpp ${KDOCK_SHARED_SRC}/desktopentry.h
    ${KDOCK_SHARED_SRC}/dockconfig.cpp ${KDOCK_SHARED_SRC}/dockconfig.h
    ${KDOCK_SHARED_SRC}/translations.cpp ${KDOCK_SHARED_SRC}/translations.h
)

target_include_directories(kdock-clipboard PRIVATE ${KDOCK_SHARED_SRC})

qt_generate_wayland_protocol_client_sources(kdock-clipboard FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/ext-data-control-v1.xml
)

qt_add_resources(kdock-clipboard "clipboard-qml"
    PREFIX "/"
    FILES qml/Clipboard.qml
)

qt_add_resources(kdock-clipboard "clipboard-translations"
    PREFIX "/"
    BASE ".."
    FILES ../translations/capabase.md ../translations/english.md
          ../translations/spanish.md ../translations/zh-CN.md
)

target_link_libraries(kdock-clipboard PRIVATE
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::WaylandClient
    Qt6::DBus
)

install(TARGETS kdock-clipboard RUNTIME DESTINATION bin)

configure_file(kdock-clipboard.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/kdock-clipboard.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kdock-clipboard.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
