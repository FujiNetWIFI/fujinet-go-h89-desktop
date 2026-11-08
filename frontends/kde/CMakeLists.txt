# The KDE frontend: plain Qt6 Widgets.
#
# Deliberately NOT KDE Frameworks. It picks up Breeze through the platform
# theme anyway, and staying framework-free keeps this frontend usable outside
# a KDE session -- and keeps the door open to reusing it elsewhere.
#
# Found-or-skipped, like the GNOME one: a machine without Qt6 development
# packages still builds the core, the tests and the other frontend.

set(CMAKE_AUTOMOC ON)

# Qt 6.7 for QRhiWidget (the GPU display); GuiPrivate for rhi/qrhi.h and
# ShaderTools to bake the display's shaders.
find_package(Qt6 6.7 COMPONENTS Widgets GuiPrivate ShaderTools QUIET)
if(NOT Qt6_FOUND)
  message(STATUS "kde frontend: Qt6 Widgets not found -- skipping")
  return()
endif()

add_executable(fujinet-go-h89-kde
  main.cpp
  MainWindow.cpp
  DisplayWidget.cpp
  SoftDisplayWidget.cpp
  FrameFeed.cpp
  KeyForward.cpp
  SettingsDialog.cpp
  FujiNetWindows.cpp
  H19KeysWindow.cpp
  help/HelpWindow.cpp
  debugger/DebuggerWindow.cpp
)
target_link_libraries(fujinet-go-h89-kde PRIVATE h89_session Qt6::Widgets Qt6::GuiPrivate)
# The display's shaders, baked for every backend and embedded as resources.
# PREFIX "/" puts them at :/shaders/display.*.qsb ("/shaders" would nest it
# twice, and the display would quietly fall back to software).
qt_add_shaders(fujinet-go-h89-kde "display_shaders"
  PREFIX "/"
  FILES shaders/display.vert shaders/display.frag)
target_compile_options(fujinet-go-h89-kde PRIVATE -Wall -Wextra)
target_include_directories(fujinet-go-h89-kde PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(fujinet-go-h89-kde PRIVATE
  H89_SOURCE_ICON_DIR="${CMAKE_SOURCE_DIR}/data/icons")

# Guards the shader resource path: a wrong one is a silent fall back to the
# software display, so the test insists the baked shaders are where the
# display loads them from.
find_package(Qt6 COMPONENTS Test QUIET)
if(Qt6Test_FOUND)
  add_executable(display_shaders_test display_shaders_test.cpp)
  target_link_libraries(display_shaders_test PRIVATE Qt6::Widgets Qt6::GuiPrivate Qt6::Test)
  qt_add_shaders(display_shaders_test "display_shaders_test"
    PREFIX "/"
    FILES shaders/display.vert shaders/display.frag)
  target_compile_options(display_shaders_test PRIVATE -Wall -Wextra)
  add_test(NAME display_shaders_kde COMMAND display_shaders_test)
  set_tests_properties(display_shaders_kde PROPERTIES
                       ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

install(TARGETS fujinet-go-h89-kde RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

configure_file(data/online.fujinet.go.h89.kde.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/online.fujinet.go.h89.kde.desktop
               @ONLY)
configure_file(data/online.fujinet.go.h89.kde.metainfo.xml.in
               ${CMAKE_CURRENT_BINARY_DIR}/online.fujinet.go.h89.kde.metainfo.xml
               @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/online.fujinet.go.h89.kde.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/online.fujinet.go.h89.kde.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

find_program(DESKTOP_FILE_VALIDATE desktop-file-validate)
if(DESKTOP_FILE_VALIDATE)
  add_test(NAME desktop_file_kde
           COMMAND ${DESKTOP_FILE_VALIDATE}
                   ${CMAKE_CURRENT_BINARY_DIR}/online.fujinet.go.h89.kde.desktop)
endif()
find_program(APPSTREAMCLI appstreamcli)
if(APPSTREAMCLI)
  add_test(NAME metainfo_kde
           COMMAND ${APPSTREAMCLI} validate --no-net --pedantic
                   ${CMAKE_CURRENT_BINARY_DIR}/online.fujinet.go.h89.kde.metainfo.xml)
endif()
