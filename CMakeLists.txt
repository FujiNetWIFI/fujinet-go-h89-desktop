cmake_minimum_required(VERSION 3.20)

# Must be set before project() to take effect. Left unset, Clang defaults the
# bundle's minimum macOS version to whatever SDK the CI runner's Xcode ships
# -- e.g. the macos-latest runner's SDK can be newer than a real tester's OS,
# which produced "You can't use this version of the application ... with this
# version of macOS" on an M1 Mac for the sibling MSX port. 13.3 matches the
# SMS, NES and Atari 2600 siblings (std::filesystem in the core needs 10.15
# at least; 13.3 keeps the family on one floor).
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version")
endif()

# The version lives here and nowhere else: the About boxes, the macOS bundle
# and the release workflow all take it from PROJECT_VERSION, and cutting a tag
# checks that the tag agrees with it (see "Cutting a release" in the README).
project(fujinet-go-h89-desktop VERSION 0.4.0 LANGUAGES C CXX)

add_compile_definitions(H89_VERSION_STRING="${PROJECT_VERSION}")

# The session layer and the GTK/Win32 frontends are C11; the emulator core
# (VirtualH89, adapted -- core/v89 and core/machine) and the Qt frontend are
# C++17.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Which native frontend(s) to build. Each is found-or-skipped so a lean
# machine can still build the others (or just the core + tests).
set(FRONTEND "all" CACHE STRING
    "Frontend to build: gnome, kde, macos, windows, all, none")

# The H89's monitor ROMs (MTR-88, MTR-89, MTR-90 and the third-party
# monitors), the H-17 controller ROM and the H19's character generator are
# copyrighted Heath/Zenith (and third-party) firmware and are NOT freely
# licensed, so this repository does not redistribute them.
#
#   OFF (default) -- the app reads the ROMs the user imported ("Import
#                    System ROMs...", MAME's h89.zip and heath_tlb.zip or the
#                    loose images, identified by CRC-32) from the ROM
#                    directory at run time. Every artifact this repository
#                    publishes is built this way, and
#                    core/tests/no_embedded_roms.py checks that claim against
#                    the shipped binaries.
#   ON            -- embed whatever recognised images are in tools/roms/ so a
#                    development build has them from the first start. NOT for
#                    redistribution.
#
# See COMPLIANCE.md.
option(WITH_H89_ROMS "Embed the H89/H19 ROMs found in tools/roms (see COMPLIANCE.md)" OFF)

# Included here, at the top of the tree, so every subdirectory sees the
# *variables* it defines: GNUInstallDirs caches CMAKE_INSTALL_DATADIR as an
# empty string and only sets its non-cache fallback (share) in the including
# scope. Including it in a subdirectory (as core/ would) therefore leaves the
# frontends with an empty DATADIR -- and "${CMAKE_INSTALL_DATADIR}/applications"
# then resolves to the absolute path /applications, outside the prefix.
include(GNUInstallDirs)

include(cmake/Dependencies.cmake)

enable_testing()
add_subdirectory(core)

# The FujiNet runtime: built from the pinned firmware sources as part of the
# normal build and installed alongside the app (the session provisions the
# user's runtime tree from the installed copy on first start). Set up before
# the frontends so the macOS bundle can depend on the library it packs.
include(cmake/FujiNetRuntime.cmake)

# The exports check: libfujinet must export the fujinet_desktop_* entry
# points the session dlsym()s -- and nothing that could shadow a host
# symbol.
if(WITH_FUJINET)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME check_exports_libfujinet
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_SOURCE_DIR}/core/tests/check_exports.py"
                     "${FUJINET_LIB}")
    set_tests_properties(check_exports_libfujinet PROPERTIES SKIP_RETURN_CODE 77)
  endif()
endif()

# Each frontend is native to one platform; with FRONTEND=all (the default)
# only the host's viable frontends are considered, so a Windows/macOS
# configure never descends into the Linux (pkg-config) frontends and vice
# versa. Naming a frontend explicitly still forces just that one.
#
# add_frontend also skips a directory that has no CMakeLists.txt yet, which is
# how a milestone that has not landed stays out of the way instead of breaking
# the configure for everyone. Naming it explicitly is an error, so nobody can
# ask for a frontend and silently get nothing.
function(add_frontend name)
  if(NOT EXISTS "${CMAKE_SOURCE_DIR}/frontends/${name}/CMakeLists.txt")
    if(FRONTEND STREQUAL name)
      message(FATAL_ERROR "The ${name} frontend is not implemented yet.")
    endif()
    message(STATUS "${name} frontend: not implemented yet -- skipping")
    return()
  endif()
  add_subdirectory("frontends/${name}")
endfunction()

if(FRONTEND STREQUAL "gnome" OR (FRONTEND STREQUAL "all" AND UNIX AND NOT APPLE))
  add_frontend(gnome)
endif()
if(FRONTEND STREQUAL "kde" OR (FRONTEND STREQUAL "all" AND UNIX AND NOT APPLE))
  add_frontend(kde)
endif()
if(FRONTEND STREQUAL "macos" OR (FRONTEND STREQUAL "all" AND APPLE))
  add_frontend(macos)
endif()
if(FRONTEND STREQUAL "windows" OR (FRONTEND STREQUAL "all" AND WIN32))
  add_frontend(windows)
endif()

# The ROM-less claim, checked on every binary built with this option off --
# which is the default here, and what every published artifact is built with.
#
# Registered at the top level rather than in core/ so it can be pointed at the
# frontend executables: those are the artifacts a user actually downloads, and
# they link differently from the test binaries.
if(NOT WITH_H89_ROMS)
  find_package(Python3 COMPONENTS Interpreter)
  set(H89_ROM_CHECK_TARGETS "")
  foreach(_t h89_headless boot_smoke)
    if(TARGET ${_t})
      list(APPEND H89_ROM_CHECK_TARGETS ${_t})
    endif()
  endforeach()
  foreach(_fe gnome kde macos windows)
    if(TARGET fujinet-go-h89-${_fe})
      list(APPEND H89_ROM_CHECK_TARGETS fujinet-go-h89-${_fe})
    endif()
  endforeach()
  if(Python3_Interpreter_FOUND)
    foreach(_t IN LISTS H89_ROM_CHECK_TARGETS)
      add_test(NAME no_embedded_roms_${_t}
               COMMAND "${Python3_EXECUTABLE}"
                       "${CMAKE_SOURCE_DIR}/core/tests/no_embedded_roms.py"
                       "$<TARGET_FILE:${_t}>"
                       "${CMAKE_SOURCE_DIR}/tools/roms")
      # SKIPs (ctest 77) rather than fails when tools/roms has no real ROM to
      # probe with -- true of every CI/release checkout.
      set_tests_properties(no_embedded_roms_${_t} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
  endif()
endif()

# Application icons (rendered from source art by tools/icons/make-icons.py).
# Desktop environments look the icon up by the desktop-entry id, so the same
# art is installed under each frontend's id -- at every hicolor size, which is
# what makes it show up crisply everywhere from the 16px window list to the
# 256px app grid.
#
# Only for frontends actually built here: a package should not ship files for
# an app it does not contain.
set(H89_ICON_SIZES 16 24 32 48 64 128 192 256 512)
set(H89_ICON_TARGETS_AND_IDS
  fujinet-go-h89-gnome online.fujinet.go.h89.gnome
  fujinet-go-h89-kde online.fujinet.go.h89.kde)
list(LENGTH H89_ICON_TARGETS_AND_IDS _h89_icon_n)
math(EXPR _h89_icon_last "${_h89_icon_n} - 1")
foreach(_i RANGE 0 ${_h89_icon_last} 2)
  list(GET H89_ICON_TARGETS_AND_IDS ${_i} _target)
  math(EXPR _j "${_i} + 1")
  list(GET H89_ICON_TARGETS_AND_IDS ${_j} _id)
  if(TARGET ${_target})
    foreach(_size IN LISTS H89_ICON_SIZES)
      install(FILES data/icons/hicolor/${_size}x${_size}/apps/fujinet-go-h89.png
              DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps
              RENAME ${_id}.png)
    endforeach()
  endif()
endforeach()

# ---- packaging --------------------------------------------------------
#
# One package per frontend, not one package for both: GNOME (GTK4/libadwaita)
# and KDE (Qt6) pull in entirely different runtime library stacks, and a user
# who wants one should not be made to depend on the other.
if(FRONTEND STREQUAL "gnome")
  set(CPACK_PACKAGE_NAME "fujinet-go-h89-gnome")
elseif(FRONTEND STREQUAL "kde")
  set(CPACK_PACKAGE_NAME "fujinet-go-h89-kde")
else()
  set(CPACK_PACKAGE_NAME "fujinet-go-h89")
endif()

# The licences that travel with every binary: the GPL, and the notices the
# vendored code's licences ask a binary distribution to carry (VirtualH89's
# BSD-3-Clause, clause 2; Z80Pack's MIT). In the package's own doc directory
# on Linux; frontends/macos puts them in the bundle's Resources and
# release.yml beside the Windows exe.
if(TARGET fujinet-go-h89-gnome OR TARGET fujinet-go-h89-kde)
  set(H89_DOCDIR "${CMAKE_INSTALL_DATADIR}/doc/${CPACK_PACKAGE_NAME}")
  install(FILES LICENSE core/v89/LICENSE.z80pack DESTINATION ${H89_DOCDIR})
  install(FILES core/v89/LICENSE DESTINATION ${H89_DOCDIR} RENAME LICENSE.VirtualH89)
endif()

set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "FujiNet")
set(CPACK_PACKAGE_CONTACT "https://github.com/tschak909/fujinet-go-h89-desktop/issues")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://fujinet.online/")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Self-contained Heathkit H89 with built-in FujiNet")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES ON)

if(UNIX AND NOT APPLE)
  set(CPACK_GENERATOR "DEB;RPM;TGZ")

  # Auto-detected from the actual linked libraries (dpkg-shlibdeps /
  # rpmbuild's own dependency generator) rather than hand-listed: GTK4 vs Qt6
  # alone makes one fixed list wrong for whichever frontend was not built, and
  # a hand-list silently goes stale the moment a dependency's package name
  # changes across distributions.
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_SECTION "otherosfs")
  set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")

  set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
  set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
  set(CPACK_RPM_PACKAGE_GROUP "Applications/Emulators")
  set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
endif()

include(CPack)
