# kdock test suite. Everything is registered in ctest with a LABEL, so the local
# runner (tests/run.sh) and the GitHub Actions job are the same command with a
# different filter:
#
#   static  repo invariants; no compiler, no GUI, no D-Bus. Seconds.
#   unit    Qt Test against kdock_core, QT_QPA_PLATFORM=offscreen.
#   qml     the real binaries under Xvfb (the QML actually loads and draws).
#   live    needs the user's Wayland session + KWin. NEVER in CI.
#
# See tests/README.md.

find_package(Qt6 REQUIRED COMPONENTS Test)

set(KDOCK_TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(KDOCK_REPO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ---------------------------------------------------------------- unit tier --
#
# Each test links the very objects the dock is built from (kdock_core, see the
# root CMakeLists) and runs offscreen inside its own XDG sandbox — the sandbox
# is set up in C++ by tests/unit/sandbox.h, before QApplication exists, because
# XDG_DATA_HOME has to be in place before the first QStandardPaths call.
function(kdock_add_unit_test name)
    qt_add_executable(${name} ${CMAKE_CURRENT_SOURCE_DIR}/unit/${name}.cpp)
    target_link_libraries(${name} PRIVATE kdock_core Qt6::Test)
    target_include_directories(${name} PRIVATE ${KDOCK_TESTS_DIR}/unit)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        LABELS "unit"
        # offscreen keeps it headless; the fake PATH is the safety net that stops
        # a backend from changing the real brightness / colour scheme / audio.
        # KDOCK_FIXTURES travels in the environment and not as a compile
        # definition: this repo's path has a '#' in it and CMake silently drops
        # any -D that contains one (it warns and moves on, so the test would
        # fail to compile for a reason that has nothing to do with the test).
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${CMAKE_CURRENT_BINARY_DIR}/fakebin:$ENV{PATH};KDOCK_FIXTURES=${KDOCK_TESTS_DIR}/fixtures"
        SKIP_RETURN_CODE 77)
endfunction()

kdock_add_unit_test(tst_dockgeometry)
kdock_add_unit_test(tst_dockconfig)
kdock_add_unit_test(tst_dockmanager)
kdock_add_unit_test(tst_desktopentry)
kdock_add_unit_test(tst_translations)
kdock_add_unit_test(tst_qmlload)
kdock_add_unit_test(tst_weather)
kdock_add_unit_test(tst_brightness)
kdock_add_unit_test(tst_wallpapercolors)
kdock_add_unit_test(tst_autocolorscheme)
kdock_add_unit_test(tst_qtcompat)
kdock_add_unit_test(tst_configarchive)
kdock_add_unit_test(tst_appswidget)
kdock_add_unit_test(tst_apppreview)
kdock_add_unit_test(tst_settingsdialog)
kdock_add_unit_test(tst_audiocontrol)
kdock_add_unit_test(tst_session)
kdock_add_unit_test(tst_lxqtwallpapers)
kdock_add_unit_test(tst_kwinscripts)

# tst_systray is registered by hand too, and wrapped in `dbus-run-session`: the
# host it exercises takes org.kde.StatusNotifierWatcher, so on the developer's
# real session bus it would fight the running kdock for the session's tray (and
# on a KDE session, plasmashell). Without the wrapper the test does not run at
# all — it is skipped, not silently pointed at the real bus.
qt_add_executable(tst_systray ${CMAKE_CURRENT_SOURCE_DIR}/unit/tst_systray.cpp)
target_link_libraries(tst_systray PRIVATE kdock_core Qt6::Test)
target_include_directories(tst_systray PRIVATE ${KDOCK_TESTS_DIR}/unit)
find_program(KDOCK_DBUS_RUN_SESSION dbus-run-session)
if(KDOCK_DBUS_RUN_SESSION)
    add_test(NAME tst_systray
             COMMAND ${KDOCK_DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_systray>)
else()
    add_test(NAME tst_systray COMMAND ${CMAKE_COMMAND} -E echo
             "dbus-run-session not found: skipping the systray test")
    set_tests_properties(tst_systray PROPERTIES SKIP_REGULAR_EXPRESSION "skipping")
endif()
set_tests_properties(tst_systray PROPERTIES
    LABELS "unit"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${CMAKE_CURRENT_BINARY_DIR}/fakebin:$ENV{PATH}"
    SKIP_RETURN_CODE 77)

# tst_cmslider is registered by hand instead of through kdock_add_unit_test: it
# runs once per QQC2 style, because QQuickStyle::setStyle() only takes effect
# once per process and the bug it freezes (a Slider 0 px tall, see the file)
# only showed up under the styles that leave `padding` at 0. It also needs the
# repo path in the environment — the .qml it measures lives in the
# kdock-controlmanager target, which the unit tier does not link.
qt_add_executable(tst_cmslider ${CMAKE_CURRENT_SOURCE_DIR}/unit/tst_cmslider.cpp)
target_link_libraries(tst_cmslider PRIVATE kdock_core Qt6::Test)
target_include_directories(tst_cmslider PRIVATE ${KDOCK_TESTS_DIR}/unit)
foreach(style Basic Fusion Material Universal)
    add_test(NAME tst_cmslider.${style} COMMAND tst_cmslider)
    set_tests_properties(tst_cmslider.${style} PROPERTIES
        LABELS "unit"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;KDOCK_REPO=${KDOCK_REPO_DIR};KDOCK_TEST_QQC2_STYLE=${style}"
        SKIP_RETURN_CODE 77)
endforeach()

# The fake tools have to exist before any test runs, so they are generated at
# build time by a target every unit test depends on.
#
# DEPENDS on the script itself, and it is not optional: without it the stamp
# already exists, editing fakebin.sh regenerates nothing, and the tests keep
# running against the previous fakes. That cost a full positive control
# (2026-08-16): a new slow-pactl fake was never written, so the test measured an
# instant pactl and passed against the very bug it was written to catch.
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fakebin/.stamp
    COMMAND ${KDOCK_TESTS_DIR}/lib/fakebin.sh ${CMAKE_CURRENT_BINARY_DIR}/fakebin
    DEPENDS ${KDOCK_TESTS_DIR}/lib/fakebin.sh
    COMMENT "Generating the fake system tools used by the tests")
add_custom_target(kdock_fakebin ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fakebin/.stamp)
foreach(t tst_dockgeometry tst_dockconfig tst_dockmanager tst_desktopentry tst_translations tst_brightness
          tst_qmlload tst_weather tst_wallpapercolors tst_autocolorscheme tst_qtcompat tst_configarchive
          tst_appswidget tst_apppreview tst_settingsdialog tst_audiocontrol)
    add_dependencies(${t} kdock_fakebin)
endforeach()

# -------------------------------------------------------------- static tier --
#
# Repo invariants, most of them a trap from CLAUDE.md turned into an assertion.
function(kdock_add_static_test name script)
    add_test(NAME ${name} COMMAND ${script} ${KDOCK_REPO_DIR})
    set_tests_properties(${name} PROPERTIES LABELS "static" SKIP_RETURN_CODE 77)
endfunction()

kdock_add_static_test(static.qmllint          ${KDOCK_TESTS_DIR}/static/qmllint.sh)
kdock_add_static_test(static.qml_resources    ${KDOCK_TESTS_DIR}/static/check-qml-resources.py)
kdock_add_static_test(static.catalog          ${KDOCK_TESTS_DIR}/static/check-catalog.sh)
kdock_add_static_test(static.tr_separator     ${KDOCK_TESTS_DIR}/static/check-tr-separator.py)
kdock_add_static_test(static.widget_tokens    ${KDOCK_TESTS_DIR}/static/check-widget-tokens.py)
kdock_add_static_test(static.card_fg          ${KDOCK_TESTS_DIR}/static/check-card-fg.py)
kdock_add_static_test(static.cm_sections      ${KDOCK_TESTS_DIR}/static/check-cm-sections.py)

# ----------------------------------------------------------------- qml tier --
#
# The real binaries under Xvfb. $<TARGET_FILE:...> so the scripts never have to
# guess where the build put them.
function(kdock_add_qml_test name script)
    add_test(NAME ${name} COMMAND ${script} ${KDOCK_REPO_DIR} ${ARGN})
    set_tests_properties(${name} PROPERTIES
        LABELS "qml"
        ENVIRONMENT "KDOCK_FAKEBIN=${CMAKE_CURRENT_BINARY_DIR}/fakebin"
        SKIP_RETURN_CODE 77)
endfunction()

kdock_add_qml_test(qml.smoke       ${KDOCK_TESTS_DIR}/qml/smoke.sh       $<TARGET_FILE:kdock>)
kdock_add_qml_test(qml.accessories ${KDOCK_TESTS_DIR}/qml/accessories.sh
                   $<TARGET_FILE:kdock-tilemenu> $<TARGET_FILE:kdock-controlmanager>)

# ---------------------------------------------------------------- live tier --
#
# Excluded from CI by its label: these need the user's Wayland session and KWin,
# and they touch the running desktop (they clean up after themselves).
function(kdock_add_live_test name script)
    add_test(NAME ${name} COMMAND ${script} ${KDOCK_REPO_DIR} ${ARGN})
    set_tests_properties(${name} PROPERTIES
        LABELS "live"
        ENVIRONMENT "KDOCK_FAKEBIN=${CMAKE_CURRENT_BINARY_DIR}/fakebin"
        SKIP_RETURN_CODE 77
        RUN_SERIAL TRUE)
endfunction()

kdock_add_live_test(live.dodge        ${KDOCK_TESTS_DIR}/live/dodge.sh        $<TARGET_FILE:kdock>)
kdock_add_live_test(live.multimonitor ${KDOCK_TESTS_DIR}/live/multimonitor.sh $<TARGET_FILE:kdock>)
