# h89_session: the frontend contract (core/include/h89session.h), included
# from core/CMakeLists.txt once SDL3 is found.

set(H89_SESSION_SOURCES
  src/session.c
  src/settings.c
  src/paths.c
  src/media.c
  src/roms.c
  src/fujinet_runtime.c
  src/audio_sdl.c
  src/beeper.c
  src/debug.c)

# The embedded ROM table: empty unless WITH_H89_ROMS (see COMPLIANCE.md).
set(H89_ROMS_C "${CMAKE_CURRENT_BINARY_DIR}/roms_embedded.c")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(WITH_H89_ROMS)
  file(GLOB H89_ROM_INPUTS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/roms/*")
  add_custom_command(
    OUTPUT "${H89_ROMS_C}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/roms/embed-roms.py"
            "${H89_ROMS_C}" "${CMAKE_SOURCE_DIR}/tools/roms"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/roms/embed-roms.py" ${H89_ROM_INPUTS}
    COMMENT "Embedding the ROMs in tools/roms (WITH_H89_ROMS=ON: NOT redistributable)"
    VERBATIM)
else()
  add_custom_command(
    OUTPUT "${H89_ROMS_C}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/roms/embed-roms.py"
            --none "${H89_ROMS_C}"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/roms/embed-roms.py"
    VERBATIM)
endif()

add_library(h89_session STATIC ${H89_SESSION_SOURCES} "${H89_ROMS_C}")
set_target_properties(h89_session PROPERTIES
  C_STANDARD 11
  POSITION_INDEPENDENT_CODE ON)
target_include_directories(h89_session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(h89_session PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/machine"
  "${CMAKE_CURRENT_SOURCE_DIR}/debugger")
target_link_libraries(h89_session PUBLIC h89_core PRIVATE SDL3::SDL3)
# setenv()/getaddrinfo() under a strict -std=c11
target_compile_definitions(h89_session PRIVATE _GNU_SOURCE _DEFAULT_SOURCE)
set_property(SOURCE ${H89_SESSION_SOURCES} APPEND PROPERTY COMPILE_OPTIONS
  "$<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra>"
  # paths are bounded by H89_PATH_MAX and joined with snprintf on purpose;
  # GCC's truncation analysis flags every such join
  "$<$<C_COMPILER_ID:GNU>:-Wno-format-truncation>")

# Where an uninstalled build and an installed copy find the FujiNet runtime
# (core/src/paths.c probes these after the executable's own directory).
target_compile_definitions(h89_session PRIVATE
  H89_DEV_FUJINET_OUT="${CMAKE_SOURCE_DIR}/tools/fujinet/work/out"
  H89_INSTALL_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/fujinet-go-h89"
  H89_INSTALL_LIBDIR="${CMAKE_INSTALL_FULL_LIBDIR}/fujinet-go-h89")
if(WIN32)
  target_link_libraries(h89_session PUBLIC ws2_32)
else()
  target_link_libraries(h89_session PUBLIC ${CMAKE_DL_LIBS})
endif()

# The scripted headless machine again, through the session: FujiNet runs
# in-process (core/tools/h89_headless.c, -DH89_HEADLESS_SESSION).
add_executable(h89_session_run tools/h89_headless.c)
target_link_libraries(h89_session_run PRIVATE h89_session)
target_compile_definitions(h89_session_run PRIVATE _GNU_SOURCE _DEFAULT_SOURCE
  H89_HEADLESS_SESSION H89_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(h89_session_run PROPERTIES C_STANDARD 11)
if(WIN32)
  target_link_options(h89_session_run PRIVATE -static)
endif()

# The FujiNet card against the real in-process runtime: one FujiBus
# exchange through the 82C55 (core/tests/fujibus_smoke_test.cpp). 77 without
# the runtime; serial, since it owns FujiNet's fixed ports.
add_executable(fujibus_smoke_test tests/fujibus_smoke_test.cpp)
target_link_libraries(fujibus_smoke_test PRIVATE h89_session)
target_include_directories(fujibus_smoke_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_compile_definitions(fujibus_smoke_test PRIVATE _GNU_SOURCE _DEFAULT_SOURCE)
set_target_properties(fujibus_smoke_test PROPERTIES CXX_STANDARD 17)
if(WIN32)
  target_link_options(fujibus_smoke_test PRIVATE -static)
endif()
add_test(NAME fujibus_smoke COMMAND fujibus_smoke_test)
set_tests_properties(fujibus_smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE)

# The external-FujiNet address field (pure), and "Use external FujiNet"
# through a session: no runtime of its own, and the card dials a stand-in
# listener on a spare port (core/tests/hostport_test.c, external_test.c). The
# second is 77 without the ROMs.
foreach(_name hostport external)
  add_executable(${_name}_test tests/${_name}_test.c)
  target_link_libraries(${_name}_test PRIVATE h89_session)
  target_include_directories(${_name}_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  target_compile_definitions(${_name}_test PRIVATE _GNU_SOURCE _DEFAULT_SOURCE
    H89_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  set_target_properties(${_name}_test PROPERTIES C_STANDARD 11)
  if(WIN32)
    target_link_options(${_name}_test PRIVATE -static)
  endif()
  add_test(NAME ${_name} COMMAND ${_name}_test)
  set_tests_properties(${_name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endforeach()
