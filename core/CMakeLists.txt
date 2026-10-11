# h89_core: the toolkit-agnostic, SDL-free H89 core.
#
#   v89/       VirtualH89 (github.com/mgarlanger/VirtualH89 @ d1dea92,
#              BSD-3-Clause), vendored and adapted: the Z80, the memory
#              decoders (H88, H89 ORG-0, MMS 77318), the I/O bus, the GPP,
#              the 2 ms timer, the 8250s, the disk controllers (H17, H37,
#              MMS 77316, MMS 77320) and their drives and image formats, and
#              CP/NET's host-file server. Every file that was changed says
#              so in its header; the GUI, the ROM images, the font and the
#              Z47 are not vendored (see COMPLIANCE.md).
#
#   machine/   this app's own C++: the machine builder (Machine), the paced
#              host thread and its C API (Host, host.h), the H19 terminal,
#              the FujiNet card, the combined interrupt controller, the H17
#              and soft-sectored image formats, the screen renderer.
#
#   net/       rawpipe, the FujiNet card's TCP byte pipe.
#
#   debugger/  the debugger engine (core/include/h89debug.h).
#
# Nothing here links SDL. The session layer (SDL3 audio) is a separate target
# so that stays true; no_sdl_link guards it.

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

file(GLOB H89_V89_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/v89/*.cpp")
file(GLOB H89_MACHINE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/machine/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/machine/*.c")
set(H89_NET_SOURCES net/rawpipe.c)
# Mark Adler's puff (zlib licence): inflate for reading MAME's ROM zips.
set(H89_THIRD_PARTY_SOURCES third_party/puff/puff.c)
file(GLOB H89_DEBUGGER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/debugger/*.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/debugger/*.cpp")

add_library(h89_core STATIC
  ${H89_V89_SOURCES} ${H89_MACHINE_SOURCES} ${H89_NET_SOURCES} ${H89_DEBUGGER_SOURCES}
  ${H89_THIRD_PARTY_SOURCES})
set_target_properties(h89_core PROPERTIES
  C_STANDARD 11
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON)

target_include_directories(h89_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/machine")
# SYSTEM: the vendored headers are not warning-clean, and our own sources
# that include them should not inherit their warnings.
target_include_directories(h89_core SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/v89")
target_include_directories(h89_core PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/net"
  "${CMAKE_CURRENT_SOURCE_DIR}/debugger"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/puff")

# getaddrinfo/clock_gettime/nanosleep under a strict -std=c11.
target_compile_definitions(h89_core PRIVATE _GNU_SOURCE _DEFAULT_SOURCE)

target_link_libraries(h89_core PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(h89_core PUBLIC ws2_32 winmm)
else()
  target_link_libraries(h89_core PUBLIC m)
endif()

# Our own modules are warning-clean; the vendored tree builds with whatever
# warnings upstream is comfortable with.
set(H89_WARN_SOURCES ${H89_MACHINE_SOURCES} ${H89_NET_SOURCES} ${H89_DEBUGGER_SOURCES})
set_property(SOURCE ${H89_WARN_SOURCES} APPEND PROPERTY COMPILE_OPTIONS
  "$<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wall;-Wextra>"
  "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall;-Wextra>")
set_property(SOURCE ${H89_V89_SOURCES} ${H89_THIRD_PARTY_SOURCES} APPEND PROPERTY COMPILE_OPTIONS
  "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-w>"
  "$<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-w>")

# ---- h89_session: the frontend contract ----------------------------------
#
# A separate target from h89_core so the core stays SDL-free: audio_sdl.c
# lives here. Found-or-skipped, exactly as the frontends are:
# `-DFRONTEND=none` on a machine (or a cross-toolchain) with no SDL3 must
# still configure and build the emulator and its tests.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/session.c")
  find_package(SDL3 CONFIG QUIET)
  if(NOT SDL3_FOUND)
    message(STATUS "h89_session: SDL3 not found -- building the core and its "
                   "tests only (no audio, no frontends)")
  else()
    include(session.cmake)
  endif()
endif()

# The scripted headless machine (core/tools/h89_headless.c): the tests and
# the end-to-end runs drive the H89 through it.
add_executable(h89_headless tools/h89_headless.c)
target_link_libraries(h89_headless PRIVATE h89_core)
target_compile_definitions(h89_headless PRIVATE _GNU_SOURCE _DEFAULT_SOURCE
  H89_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(h89_headless PROPERTIES C_STANDARD 11)
if(WIN32)
  target_link_options(h89_headless PRIVATE -static)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
  add_subdirectory(tests)
endif()
