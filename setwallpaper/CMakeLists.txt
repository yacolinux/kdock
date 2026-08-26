# kdock-setwallpaper: a tiny binary that sets one image as the wallpaper of the
# current monitor (the companion of the "next wallpaper" advance). Its reason to
# exist is the .desktop below: it makes "Setear Kdock Wallpaper" show up under
# "Abrir con" in Dolphin and PCManFM-Qt.
#
# Like kdock-calendar it is an ordinary, self-contained tool: no Wayland
# protocol, no privileged interface, so it runs straight from build/ and its
# install needs no ksycoca refresh. The only kdock source it touches is the
# header-only plasmascript.h (the fallback that drives Plasma directly), pulled
# in through the include path — nothing of kdock is compiled here, so this target
# can never disturb the dock build.

qt_add_executable(kdock-setwallpaper
    src/main.cpp
)

target_include_directories(kdock-setwallpaper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(kdock-setwallpaper PRIVATE Qt6::Gui Qt6::DBus)

install(TARGETS kdock-setwallpaper RUNTIME DESTINATION bin)

# No privileged interfaces: the primary path is a call into the running kdock,
# the fallback is org.kde.PlasmaShell (plain session D-Bus). The entry exists so
# the tool appears in file managers' "Open with" for image mimetypes; install
# needs no ksycoca refresh.
configure_file(kdock-setwallpaper.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/kdock-setwallpaper.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kdock-setwallpaper.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
