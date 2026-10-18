# Compliance

Per-component provenance for `fujinet-go-h89-desktop`, written before the
first public build, in the family tradition (see
`fujinet-go-sms-desktop/COMPLIANCE.md`, `fujinet-go-astrocade-desktop/COMPLIANCE.md`).

## What ships

| Component | Origin | Licence | How it enters the build |
|---|---|---|---|
| This application | this repository | GPL-3.0-or-later | — |
| **The H89 core** (`core/v89/`) | [VirtualH89](https://github.com/mgarlanger/VirtualH89) by Mark Garlanger, with Douglas Miller, David Troendle and Mark Fisher, at commit `d1dea92`; vendored and adapted | BSD-3-Clause, © 2009-2016 Mark Garlanger (`core/v89/LICENSE`, kept); each changed file says so and what changed in its header, the changes GPL-3.0-or-later | compiled into `h89_core` |
| The Z80 core (`core/v89/z80.cpp`) | VirtualH89's, "portions derived from Z80Pack 1.17" by Udo Munk | BSD-3-Clause (VirtualH89); Z80Pack has been MIT-licensed since 2018 (© 1987-2017 Udo Munk) | as above |
| **The machine layer** (`core/machine/`): the builder, the paced host, the H19 terminal, the FujiNet card, the interrupt controller, the H17 and soft-sectored image formats, the renderer, the drive layer | this repository; the H19 terminal's escape interpreter and the machine graph are derived from VirtualH89's `h19.cpp` and `H89.cpp` and say so | GPL-3.0-or-later (the VirtualH89-derived parts BSD-3-Clause as above) | compiled into `h89_core` |
| **puff** (`core/third_party/puff/`) | Mark Adler's inflate, from zlib's `contrib/puff`, vendored verbatim with its README | zlib | compiled into `h89_core`; reads MAME's ROM zips |
| **FujiNet firmware** (`libfujinet`) | [`FujiNetWIFI/fujinet-firmware`](https://github.com/FujiNetWIFI/fujinet-firmware), PC target `RS232` | GPL-3.0-or-later | built as a shared library by `tools/fujinet/build-fujinet-desktop.sh`, `dlopen`'d at run time |
| The frontends (`frontends/{gnome,kde,windows,macos}`) | this project's family (the SMS sibling's, © Thomas Cherryhomes) | GPL-3.0-or-later | one executable per platform |
| GTK4, libadwaita (GNOME) / Qt6 (KDE) | the GNOME and Qt projects | LGPL-2.1-or-later / LGPL-3.0 | system libraries, dynamically linked |
| SDL3 | libsdl-org | Zlib | system package on Linux; linked statically on macOS and Windows |
| mbedTLS 3.6.x | Mbed-TLS | Apache-2.0 | for `libfujinet`: system package where it is a usable 3.x, otherwise the pinned source |

Everything above is GPL-3.0-or-later or compatible with it (BSD-3-Clause,
MIT, zlib, Apache-2.0, LGPL): the combined work is distributed under
GPL-3.0. Binary packages carry VirtualH89's licence text beside the GPL
(BSD-3-Clause, clause 2).

## Consulted, not copied

These were read to get the behaviour right; no code from them is in this
repository:

- **MAME** (`src/mame/heathzenith/h89.cpp`, `src/devices/bus/heathzenith/h19/tlb.cpp`,
  `src/lib/formats/h17disk.cpp`): the per-monitor SW501 tables, the H19's
  switch polarities, its character generator's layout, the H17Disk v2 block
  layout. The SW501 tables are restated as data in `core/machine/config.c`.
- **z88dk** (`src/appmake/cpmdisk.c`, `hdos.c`): the H17Disk v2 writer's
  layout and the HDOS label (its volume serial on every track but the first).
- Mark Garlanger's **H17Disk specification** (v2.0.0 and the earlier draft,
  heathkit.garlanger.com/diskformats/).
- **fujinet-lib** `bus/h89/portio.c`, the **H89-ESP32 schematic v1.28**,
  J. Travis's **H89-ESP32-FUJINET Rev 1.1** schematic as reworked for
  fujinet-firmware PR #2042, and N. Collado's "How to control the
  H89-FujiNet 82C55 in Mode 2": the FujiNet card's registers and handshake.

## What is changed from VirtualH89

Every changed file names its changes in its header. In short:

- **No globals.** The wall clock, the general-purpose-port listener list and
  the machine itself were process singletons; each machine now owns them
  (`core/v89/MachineContext.h`), so a power cycle cannot reach the previous
  machine's devices.
- **No SIGALRM.** The CPU runs a budget per emulated 2 ms slice
  (`Z80::runCycles`), paced by the host thread against the monotonic clock;
  the timer interrupt is raised between slices. The speed-up board's
  multiplier no longer speeds up the disks: devices run on 2.048 MHz time.
- **The H19** is this project's `H19Terminal`, derived from VirtualH89's,
  with its bugs fixed (line 25 detection, the alternate keypad mode), the
  keyboard's encodings, the bell and key click, and pacing that never
  overruns the 8250.
- **Interrupts**: one controller for the motherboard levels plus every
  soft-sectored controller, so the H37 and the MMS 77316 can be fitted
  together.
- **Disks**: H17 images are this project's `H17Image` (raw, H8D, H17Disk and
  its draft); every format is written back to its own file. VirtualH89 saved
  H17 disks to `./h17_saveDiskN.rawdisk` on exit.
- **Debugger hooks** on every instruction, I/O and memory access, and IM 2.
- **The Z80**: `EX (SP),IX/IY` and `LD SP,IX/IY` (they acted on HL, which
  broke zsdcc's stack frames).
- **Portability**: Windows builds (MinGW-w64) through `compat_posix.h`.
- **Not vendored**: the GUIs (GLUT, wxWidgets, Dear ImGui), `main`, the stdio
  consoles and the operator protocol, the Z47 (not functional upstream),
  VirtualH89's disassembler and its property-file reader (which carries no
  licence; `propertyutil.cpp` here is a new implementation of the few
  helpers the devices use), and — see below — `H89-roms.h` and `h19-font.h`.

## System ROMs — not redistributed

The H89's monitor ROMs (Heath MTR-88 444-40, MTR-89 444-62, Zenith MTR-90
444-84 and 444-142; Magnolia MicroSystems 444-84A/B; Kres KMR-100; Ultimeth
MTRHEX; ETS; SigmaSoft SigmaROM), the H-88-1 controller ROM (444-19) and the
H19's character generator (444-29) are copyrighted firmware that is **not**
freely licensed. VirtualH89 compiles copies of some of them in
(`H89-roms.h`) and a font drawn from the H19's (`h19-font.h`); this project
does **not** vendor either file, and does not contain or redistribute any of
the ROMs.

- **Import System ROMs…** (every frontend) reads MAME's `h89.zip` and
  `heath_tlb.zip` as they are, or loose images, and keeps the ones it
  recognises by size and CRC-32 (`core/machine/romset.c`, MAME's table).
- `-DWITH_H89_ROMS=ON` embeds whatever recognised images a developer put in
  `tools/roms/`, for local development only. Every artifact this project
  publishes is built with it **OFF** (every CI, release and Flatpak
  configure passes it explicitly), and the `no_embedded_roms_*` tests
  (`core/tests/no_embedded_roms.py`) check the shipped binaries for the
  ROMs' bytes.
- The core tests that need no Heath ROM use programs of their own
  (`core/tests/machine_test.cpp`); the ones that do skip without them.

## Disk images

The application ships no disk. Each release also publishes a separate zip of
disks for trying FujiNet, `fujinet-go-h89-<version>-disks.zip`. The
release workflow builds it with `tools/disks/make-fujinet-disks.sh`:

| Disk | Origin | Licence |
|---|---|---|
| `fujinet-hdos-N.h8d`, `fujinet-cpm-N.h8d` | fujinet-lib's integration tests (`testing/`), built by the release workflow at a pinned fujinet-lib commit with z88dk | GPL-3.0; the zip carries `LICENSE.fujinet-lib` and names the source commit |
| `hdos-2.0.h17disk` | HDOS 2.0, Mark Garlanger's capture of the distribution disk (heathkit.garlanger.com/software/OSes/HDOS/2.0/), unchanged | public domain: the page says "This software was placed in the public domain." |
| `cpm-2.2.04-64k.h8d` | Heath/Zenith CP/M 2.2.04 for the H17 (Digital Research's CP/M 2.2 with Heath's BIOS and utilities): disk 1 of `CPM_2_2_04_Update.zip` from the Society of Eight-Bit Heath Computerists' archive (sebhc.github.io/sebhc/software.html), configured as a standard system and rebuilt for 64K with its own MOVCPM17 and SYSGEN | none stated |

The two boot disks are committed in `tools/disks/boot/`; they are the only
disks in the repository. CI cannot rebuild the 64K CP/M disk, because that
means booting it, and CI has no Heath ROMs. `make-fujinet-disks.sh
--rebuild-cpm-boot` rebuilds it, byte for byte, on a machine that has the
ROMs. Shipping CP/M 2.2.04 was the maintainer's
decision (2026-10-11): it is not under a stated licence, but SEBHC's
archive distributes it.

`tools/disks/fetch-disks.sh` downloads the rest of the HDOS 2.0 distribution
and SEBHC's CP/M disks into the gitignored `tools/disks/images/` for the
end-to-end tests. None of those ship.

## Trademarks and names

"Heathkit", "Heath", "H89", "H88", "H19", "H17" and "Zenith" are or were
trademarks of their owners; they are used here only to name the machine and
the parts being emulated, and the project is not affiliated with or endorsed
by them. "FujiNet" is the FujiNet project's name.

## Icon

The icon is the FujiNet Go family mark (the same artwork as the other
desktops), recoloured after the H89's nameplate: a glossy black plate, a
gold frame and cream stones (`tools/icons/make-icons.py`). It does not
reproduce the Heathkit wordmark.
