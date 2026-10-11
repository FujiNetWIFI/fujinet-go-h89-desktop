# core tests (see each file's header). Tests that need the Heath ROMs, a disk
# image or the FujiNet runtime exit 77 without them, which ctest reports as
# skipped.

function(h89_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE h89_core)
  target_include_directories(${name} PRIVATE
    "${CMAKE_SOURCE_DIR}/core/tests"
    "${CMAKE_SOURCE_DIR}/core/net"
    "${CMAKE_SOURCE_DIR}/core/debugger")
  target_compile_definitions(${name} PRIVATE _GNU_SOURCE _DEFAULT_SOURCE
    H89_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  set_target_properties(${name} PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
  if(WIN32)
    target_link_options(${name} PRIVATE -static)
  endif()
endfunction()

function(h89_add_test name)
  add_test(NAME ${name} COMMAND ${ARGN})
  set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endfunction()

# The core links and runs alone: no SDL, no session.
h89_test(no_sdl_link_test no_sdl_link_test.c)
h89_add_test(no_sdl_link no_sdl_link_test)

# The machine configuration: defaults, validation (ports, interrupts, each
# card once), SW501 derived per monitor, the settings keys.
h89_test(config_test config_test.c)
h89_add_test(config config_test)

# The H19: every escape sequence it handles, line 25, wrapping, the
# keyboard's encodings in every keypad mode, BREAK, off-line, the 8250.
h89_test(h19_escape_test h19_escape_test.cpp)
h89_add_test(h19_escape h19_escape_test)

# The H17's image formats: raw (both side orders), H8D (HDOS volume
# numbers), H17Disk v2 and the draft, read, write back, blank images.
h89_test(h17_formats_test h17_formats_test.cpp)
h89_add_test(h17_formats h17_formats_test)

# The FujiNet card's 82C55 against a loopback TCP peer: mode and bit
# set/reset, the status word, the handshake both ways, ESP32 reset and
# download mode, back-pressure.
h89_test(card_test card_test.cpp)
h89_add_test(card card_test)

# The machine with small test programs of our own (no Heath ROMs): the 2 ms
# timer interrupt, ORG-0 and the H88 map, the MMS 77318's unlock and banks,
# the combined interrupt controller's priorities.
h89_test(machine_test machine_test.cpp)
h89_add_test(machine machine_test)

# The ROM table and the zip reader.
h89_test(roms_test roms_test.c)
h89_add_test(roms roms_test)

# The paced host with the real MTR-90: the monitor's prompt, and the 2 ms
# slices at real time. Needs the ROMs (77 without).
h89_test(boot_smoke boot_smoke.c)
h89_add_test(boot_smoke boot_smoke)

# HDOS 2.0 boots from its distribution disk (tools/disks/images, fetched by
# tools/disks/fetch-disks.sh) to its date prompt. Needs the ROMs and the
# image (77 without).
h89_test(hdos_boot_test hdos_boot_test.c)
h89_add_test(hdos_boot hdos_boot_test)
