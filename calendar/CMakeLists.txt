# kdock-calendar: a standalone KDE-style month calendar, a separate binary meant
# to be launched on its own (e.g. from the dock's clock widget or a Script
# Runner). The clock widget is *not* touched: this is an independent toplevel,
# so wiring it up never means editing the dock's QML.
#
# No Wayland protocol and no privileged interface: like kdock-tilemenu it is an
# ordinary window, so it runs straight from build/ and needs no ksycoca refresh.
# Everything is self-contained (Qt Widgets + a custom-painted calendar); no kdock
# source is compiled here, so this target can never disturb the dock build.

qt_add_executable(kdock-calendar
    src/main.cpp
    src/calendarwidget.cpp  src/calendarwidget.h
)

target_link_libraries(kdock-calendar PRIVATE Qt6::Widgets)

install(TARGETS kdock-calendar RUNTIME DESTINATION bin)

# No privileged interfaces here; the entry exists to give the window a name and
# an icon in the task switcher, nothing else. Install needs no ksycoca refresh.
configure_file(kdock-calendar.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/kdock-calendar.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kdock-calendar.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
