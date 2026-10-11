# FujiNet Go H89

A self-contained Heathkit H89 for the desktop with a FujiNet built in: the
H89 (with its H19 terminal) emulated by [VirtualH89](https://github.com/mgarlanger/VirtualH89)'s
core, the H89-ESP32 FujiNet card on its I/O bus, and the FujiNet firmware
itself running in-process — the newest member of the FujiNet Go desktop
family (Adam, Apple II, CoCo, MSX, Intellivision, Astrocade, ColecoVision,
Master System, NES, Atari 2600 and 7800).

Two versions of the app are built, one for GNOME and one for KDE. There are
Linux packages for each, Flatpaks, a Windows installer and zip, and macOS
builds for Apple Silicon and Intel. The Heath ROMs aren't included: the app
imports them from your own MAME `h89.zip` and `heath_tlb.zip` the first
time it starts.

## What it is

| | |
|---|---|
| Emulator | VirtualH89 (BSD-3), vendored and adapted in `core/v89`: Z80, H88/H89 ORG-0/MMS 77318 memory, H17, H37, MMS 77316, MMS 77320, CP/NET host files |
| Terminal | the H19, Heath and ANSI modes, drawn from its own character ROM (444-29); hold-screen, 25th line, graphics, reverse video |
| FujiNet | the H89-ESP32 card: an 82C55 at 0xD0-0xD3, driven by fujinet-lib's `bus/h89`; FujiNet's RS232 firmware in-process |
| Frontends | GNOME (GTK4/libadwaita), KDE (Qt6), Windows (Win32), macOS (AppKit) |
| Packaging | DEB, RPM, TGZ, Flatpak (GNOME and KDE), Windows installer and zip, macOS zips |

## Building

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

The first build also builds the FujiNet runtime from the pinned firmware
(a few minutes). `-DWITH_FUJINET=OFF` skips it; `-DFUJINET_SRC=...` builds
from a firmware checkout of your own.

## ROMs

The H89's monitor ROMs, the H17 controller's ROM and the H19's character
generator are copyrighted and not included. Import them (MAME's `h89.zip` and
`heath_tlb.zip` as they are, or the loose images) on first start; they are
recognised by CRC-32. See `COMPLIANCE.md`.

## Disks

| Controller | Drives | Image formats |
|---|---|---|
| H17 (H-88-1), hard-sectored, port 174Q | 3 × H-17-1 (SS 40) or H-17-4 (DS 80) | raw tracks (`.h17raw`, `.rawdisk`), `.h8d`, H17Disk (`.h17disk`, `.h17`) |
| H37 (Z-89-37), soft-sectored, port 170Q | 4 × 5.25" SS/DS, 40/80 tracks, or 8" | ImageDisk `.imd`, Teledisk `.td0`, VirtualH89 sector and raw images |
| MMS 77316, port 070Q | 8 (4 × 8", 4 × 5.25") | as the H37 |
| MMS 77320 SASI | 8 XEBEC units | fixed disks the controller creates |

## Keys

The H19's keyboard is typed on the PC's; what a PC keyboard lacks is on the
**H19 Keys** window (F9), with a SHIFT latch and the terminal's mode lights.

| | |
|---|---|
| f1-f5, ERASE, BLUE, RED, GRAY | F1-F5, F6, F7, F8, F10 |
| BREAK, SCROLL | Pause, Scroll Lock |
| the keypad (IC, DC, IL, DL, arrows, HOME) | the keypad with Num Lock off, or the arrow, Home and Insert keys |
| SHIFT+RESET | Machine → Reset (Ctrl+Shift+R; ⌘R on a Mac) |
| H19 Keys / Fullscreen / Debugger | F9 / F11 / F12 (debugger: F5 run/stop, F7 step, F8 step over, Shift+F8 step out) |

Control is the H19's (CP/M wants ^C, ^P, ^S and the rest), so the app's own
shortcuts are on **Ctrl+Shift** — Copy Screen Text, Paste, Insert Disk,
Reset, Quit — and Preferences is Ctrl+, (on a Mac, all of them are on ⌘).
Paste types the text at the line's pace.

### Cross-building Windows on Linux

```sh
curl -LO https://github.com/libsdl-org/SDL/releases/download/release-3.4.12/SDL3-devel-3.4.12-mingw.tar.gz
tar xzf SDL3-devel-3.4.12-mingw.tar.gz
cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DFRONTEND=windows -DWITH_FUJINET=OFF \
      -DCMAKE_PREFIX_PATH="$PWD/SDL3-3.4.12/x86_64-w64-mingw32"
cmake --build build-win
cp SDL3-3.4.12/x86_64-w64-mingw32/bin/SDL3.dll build-win/frontends/windows/
wine build-win/frontends/windows/fujinet-go-h89-windows.exe
```

This is the desk-side check. `fujinet.dll` does not cross-build, so the
release build is native MSYS2/UCRT64, which builds it.

## Ports

| | |
|---|---|
| FujiNet BoIP (the card dials it) | 127.0.0.1:11512 |
| FujiNet web UI | 127.0.0.1:11513 |

The family's pairs: Astrocade 11500/1, ColecoVision 11502/3, Atari 2600
11504/5, NES 11506/7, Master System 11508/9, Atari 7800 11510/11.

## How this was verified

- **The core on Linux.** `ctest` runs these tests, none of which needs a
  Heath ROM; each runs its own Z80 programs where it needs a CPU:
  - `machine`: the 2 ms timer, ORG-0, the H88's memory map, the MMS 77318's
    banks, the combined interrupt controller, /RESET and power cycling. It
    also checks the single-step interrupt: exactly one instruction runs.
  - `h19_escape`: Heath and ANSI sequences, the keys in both modes, and
    hold-screen with XOFF and XON.
  - `card`: the 82C55 card's mode-2 handshake.
  - `h17_formats`: raw, `.h8d` and H17Disk images.
  - `soft_disk`: IMD read and write-back, TD0, the sector and raw images,
    and Write Track in FM and MFM.
  - `sasi`: the MMS 77320 and its Xebec drives.
  - `hostfile_bdos`: CP/NET's host folder.
  - `debug`: every breakpoint kind, every step, the radixes and the prompt.
  - `roms`: ROM import from zips and loose images, by CRC.
  - `config`: configuration validation.
  - `boot_smoke`: 500 slices a second, real time, with the worst wake-up
    75 µs late.
  - `fujibus_smoke`: a FujiBus exchange with the real libfujinet through
    the card; the 140-byte adapter config comes back with a good checksum.

  `hdos_boot` boots HDOS 2.0 from its distribution disk. It runs only where
  the ROMs and that disk are present, which is never in CI.
- **HDOS end to end.** `tools/e2e/hdos-fujidev.sh` runs fujinet-lib's
  FUJIDEV under HDOS 2.0 against the in-process FujiNet: 23 of 24 tests
  pass. The failure is the firmware's: setting an empty filename does not
  clear a device slot. HDOS INIT formatted a blank `.h8d`, PIP copied a file
  onto it, and a fresh boot TYPEd it back.
- **GNOME and KDE.** Both build with no frontend warnings, and the desktop
  and metainfo files validate. Checked under Hyprland from screenshots:
  - **GNOME:** showed the MTR-90 prompt and took typing; booted HDOS from a
    disk named on the command line, with the date pasted; opened the H19
    Keys window and every Preferences page; stepped the MTR-90 in the
    debugger's split octal; ran FUJIDEV (the AppStream screenshots are its
    own).
  - **KDE:** booted HDOS from the remembered disk, with the date pasted;
    opened the H19 Keys window and Preferences (the Switches page with
    SW501's per-monitor rows); stepped the MTR-90 in the debugger's split
    octal and showed the card and H89 sections.
- **Flatpak.** Both manifests build and install locally. The GNOME one is
  built without ROMs, like every published build:
  - it showed the first-run ROM dialog;
  - it imported `h89.zip` and `heath_tlb.zip` through the portal's file
    chooser, and the MTR-90 prompt came up;
  - a blank `.h8d` chosen through the portal was INITed by HDOS, and its
    label is in the file on the host.
- **Windows.** The Win32 frontend cross-builds with mingw-w64 with no
  warnings, and the tests pass under Wine. The app was run under Wine at
  192 DPI: it booted HDOS from a disk named on the command line and
  catalogued it, and its Settings, H19 Keys and debugger windows (stepping,
  split octal) were checked from screenshots. CI builds and tests it
  natively with MSYS2/UCRT64, the FujiNet runtime included. It has not yet
  been run on a Windows desktop.
- **macOS.** CI is the only place it has been compiled and tested, on Apple
  Silicon and Intel. It has never run on a Mac yet.
- **Not yet verified.** CP/M has not been booted, because no CP/M image for
  the H89 has turned up yet. The H37 and MMS 77316 have only been tested
  through `soft_disk` and the controller code; neither has run under an OS.

## Cutting a release

Pushing a `v*` tag builds every platform. Only if all of it passes is the
result published, as a **draft** release:

| Asset | Contents |
|---|---|
| `fujinet-go-h89-gnome-<version>-Linux.{deb,rpm,tar.gz}` | the GNOME frontend, packaged with CPack |
| `fujinet-go-h89-kde-<version>-Linux.{deb,rpm,tar.gz}` | the KDE frontend, packaged with CPack |
| `fujinet-go-h89-<version>-windows.zip` | the exe, `fujinet.dll`, the `fujinet/` runtime tree and the licences |
| `fujinet-go-h89-<version>-windows-setup.exe` | NSIS installer, per-user, no admin rights |
| `fujinet-go-h89-<version>-macos-{arm64,x86_64}.zip` | the `.app` bundle for Apple Silicon and Intel, FujiNet inside |
| `online.fujinet.go.h89.{gnome,kde}.flatpak` | single-file bundles: `flatpak install ./…flatpak` |

The version comes from the tree, not the tag: `project(… VERSION …)` sets
it, and both metainfo files template it. `check-version` stops the release
if the tag disagrees.

To release 0.2.0:
1. Set the version in `CMakeLists.txt`.
2. Add a `<release>` entry with its date to both
   `frontends/*/data/*.metainfo.xml.in`.
3. Commit, then `git tag -a v0.2.0 && git push origin v0.2.0`.

Running the workflow by hand (Actions → Release → Run workflow) is a dry
run. Everything is built and tested and the artifacts are attached to the
run, but no release is made.

## Licence

GPL-3.0-or-later. VirtualH89's code is BSD-3-Clause (`core/v89/LICENSE`),
and its Z80 core has portions of Z80Pack, which is MIT
(`core/v89/LICENSE.z80pack`). Every package carries all three. See
`COMPLIANCE.md` for per-component provenance. "Heathkit", "Heath" and
"Zenith" name the machine emulated; this project is not affiliated with
their owners.
