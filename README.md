# FujiNet Go H89

A self-contained Heathkit H89 for the desktop with a FujiNet built in: the
H89 (with its H19 terminal) emulated by [VirtualH89](https://github.com/mgarlanger/VirtualH89)'s
core, the H89-ESP32 FujiNet card on its I/O bus, and the FujiNet firmware
itself running in-process — the newest member of the FujiNet Go desktop
family (Adam, Apple II, CoCo, MSX, Intellivision, Astrocade, ColecoVision,
Master System, NES, Atari 2600 and 7800).

> **Status: under construction.** The emulator core, the FujiNet card and the
> H17 disk formats work (HDOS 2.0 boots from its distribution disk); the
> session layer, the four frontends, the debugger, packaging and release are
> in progress. See `TODO`.

## What it is

| | |
|---|---|
| Emulator | VirtualH89 (BSD-3), vendored and adapted in `core/v89`: Z80, H88/H89 ORG-0/MMS 77318 memory, H17, H37, MMS 77316, MMS 77320, CP/NET host files |
| Terminal | the H19, Heath mode, drawn from its own character ROM |
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

## Ports

| | |
|---|---|
| FujiNet BoIP (the card dials it) | 127.0.0.1:11512 |
| FujiNet web UI | 127.0.0.1:11513 |

The family's pairs: Astrocade 11500/1, ColecoVision 11502/3, Atari 2600
11504/5, NES 11506/7, Master System 11508/9, Atari 7800 11510/11.

## Licence

GPL-3.0-or-later; VirtualH89's code is BSD-3-Clause (`core/v89/LICENSE`).
See `COMPLIANCE.md`.
