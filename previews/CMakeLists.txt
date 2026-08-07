# kdock-previews: the window-preview strips, a separate binary that runs
# alongside kdock (see PLAN.md).
#
# A handful of kdock sources are listed here as well instead of being factored
# into a shared library: they are self-contained, and compiling them twice keeps
# the kdock target itself completely untouched.

set(KDOCK_SHARED_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

qt_add_executable(kdock-previews
    src/main.cpp
    src/previewconfig.cpp           src/previewconfig.h
    src/previewmanager.cpp          src/previewmanager.h
    src/previewwindow.cpp           src/previewwindow.h
    src/previewmodel.cpp            src/previewmodel.h
    src/previewsettingsdialog.cpp   src/previewsettingsdialog.h
    src/previewsservice.cpp         src/previewsservice.h
    src/kwinwindows.cpp             src/kwinwindows.h
    src/virtualdesktops.cpp         src/virtualdesktops.h
    src/screenshotsource.cpp        src/screenshotsource.h
    src/thumbnailsource.h
    src/thumbnailcache.cpp          src/thumbnailcache.h
    src/thumbnailimageprovider.cpp  src/thumbnailimageprovider.h
    # Shared with kdock, unmodified:
    ${KDOCK_SHARED_SRC}/layershell.cpp    ${KDOCK_SHARED_SRC}/layershell.h
    ${KDOCK_SHARED_SRC}/desktopentry.cpp  ${KDOCK_SHARED_SRC}/desktopentry.h
    ${KDOCK_SHARED_SRC}/iconprovider.cpp  ${KDOCK_SHARED_SRC}/iconprovider.h
    ${KDOCK_SHARED_SRC}/theme.cpp         ${KDOCK_SHARED_SRC}/theme.h
    # Theme reads the user's icon-theme override out of kdock's shared settings
    # file and asks DockConfig where that file is. Compiling DockConfig here (it
    # only includes Qt headers) is what keeps the two docks agreeing on the path
    # *and* on the icon theme, without a copy of either.
    ${KDOCK_SHARED_SRC}/dockconfig.cpp    ${KDOCK_SHARED_SRC}/dockconfig.h
    ${KDOCK_SHARED_SRC}/translations.cpp   ${KDOCK_SHARED_SRC}/translations.h
)

target_include_directories(kdock-previews PRIVATE ${KDOCK_SHARED_SRC})

# xdg-shell is needed even though layershell.cpp only forward-declares
# xdg_popup: the generated layer-shell C code references xdg_popup_interface
# (zwlr_layer_surface_v1.get_popup takes one).
qt_generate_wayland_protocol_client_sources(kdock-previews FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/xdg-shell.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/wlr-layer-shell-unstable-v1.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/plasma-window-management.xml
)

qt_add_resources(kdock-previews "previews-qml"
    PREFIX "/"
    FILES qml/PreviewStrip.qml qml/PreviewCard.qml qml/StripMenu.qml
)

# Las cuatro capas base de traducción (nunca las ALT: este binario resuelve
# siempre a la base, ver Translations::BaseOnly). Con BASE ".." caen en
# :/translations, que es donde las busca seedFiles().
qt_add_resources(kdock-previews "previews-translations"
    PREFIX "/"
    BASE ".."
    FILES ../translations/capabase.md ../translations/english.md
          ../translations/spanish.md ../translations/zh-CN.md
)

# The in-tree layer-shell integration is a static Qt plugin.
target_compile_definitions(kdock-previews PRIVATE QT_STATICPLUGIN)

target_link_libraries(kdock-previews PRIVATE
    Qt6::CorePrivate
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Widgets
    Qt6::WaylandClient
    Qt6::WaylandClientPrivate
    Qt6::DBus
)

install(TARGETS kdock-previews RUNTIME DESTINATION bin)

# KWin matches the two privileged-interface declarations against this Exec= path,
# so it has to be the absolute installed one (same reason as kdock.desktop.in).
configure_file(kdock-previews.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/kdock-previews.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kdock-previews.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
