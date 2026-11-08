This is the text of FujiNet Go H89's built-in Help (Help ▸ FujiNet Go H89 Help,
or Ctrl+Shift+H; ⌘? on a Mac). The app shows each part only to the frontend it
is about; here you see them all. Every topic below is one page in the app.

# Getting started

FujiNet Go H89 is a Heathkit H89 with an H19 terminal, and the H89-ESP32 FujiNet
card in a slot of its I/O bus. The machine is VirtualH89's core: the Z80, the
H88 and H89 memory, the H17 and H37 disk controllers, the MMS cards and the CP/NET
host. The FujiNet runs inside the app, so the card is connected from power-on.

1. **Import the ROMs.** The Heath ROMs are copyrighted, so none comes with the
   app; the first start asks for them. See [ROMs](#roms).
2. **Put a disk in a drive.** **Drives ▸ Insert Disk…** takes a disk image, and
   **New Blank Disk…** makes one. See [Disks](#disks).
3. **Boot.** The H89's monitor boots from the disk, and the H19 types what the
   machine says. Type on your keyboard: see [Controllers and keys](#controllers-and-keys).
4. **Use the FujiNet.** Programs that talk to the card (fujinet-lib's `bus/h89`)
   reach the FujiNet and the network. See [FujiNet](#fujinet).
5. **Change the machine.** [Preferences](#preferences) strap and stuff the CPU
   board, fit cards in the slots, set the switches and the picture.

## This Help

The list on the left has a topic for each part of the app; the search box
above it finds every section that has all the words you type. A **Help**
button in Preferences, the Debugger, the H19 Keys window and the FujiNet console
log opens the topic for what it shows, and <kbd>Ctrl+Shift+H</kbd> does the same
for the window you are in.

<!-- only: gnome kde windows -->

In Preferences, the Debugger and the console log, <kbd>F1</kbd> does too. In the
main window <kbd>F1</kbd> is the H19's f1 key, so it goes to the machine.

<!-- end -->

The same text is on GitHub, as
[data/help/help.md](https://github.com/FujiNetWIFI/fujinet-go-h89-desktop/blob/main/data/help/help.md).

<!-- only: macos -->

On a Mac, the Help menu's search field also finds Help topics.

<!-- end -->

<!-- context: main -->
# The window

The H19's screen fills the window. The menus have:

- **Machine**: **Reset** (SHIFT+RESET), **Power Cycle**, **Warp Speed** and
  **Import System ROMs…**.
- **Drives**: a submenu for each drive, **Insert Disk…**, **New Blank Disk…** and
  **Open Disks Folder**. See [Disks](#disks).
- **Edit**: **Copy Screen Text** and **Paste**.
- **View**: **H19 Keys** (<kbd>F9</kbd>), **Debugger** (<kbd>F12</kbd>), **TV
  Aspect**, **Smooth Scaling** and **Fullscreen** (<kbd>F11</kbd>).
- **FujiNet**: **FujiNet Configuration**, **Console Log** and **Restart FujiNet**.
- **Preferences**, **Help**, **About** and **Quit**.

<!-- only: gnome -->

On GNOME, everything is in the main menu (the button at the right of the
header bar).

<!-- end -->

<!-- only: macos -->

On a Mac, Settings and Quit are in the app menu, and the shortcuts are on ⌘.

<!-- end -->

The status line shows what the card's link is doing, and *WARP* while Warp Speed
is on.

## Reset and power

- **Reset** (SHIFT+RESET) is the H19 keyboard's reset: the CPU and the cards reset,
  and the monitor starts again, with what is in memory kept as a real H89 keeps
  it.
- **Power Cycle** is the power switch: memory is cleared and everything starts from
  nothing.
- **Warp Speed** runs as fast as the computer can; the disk and clock keep their
  own time.

## Copy and paste

**Copy Screen Text** (<kbd>Ctrl+Shift+C</kbd>) copies what the H19's screen shows
as text. **Paste** (<kbd>Ctrl+Shift+V</kbd>) types the text at the line's pace.

<!-- context: nobios -->
## Needing the ROMs

When a ROM the machine needs is missing, the app says **Import the H89's ROMs**,
and names what is still needed. **Import ROMs…** takes MAME's `h89.zip` and
`heath_tlb.zip`, or the loose images; they are recognised by their contents.
**Not Now** leaves the window as it is. See [ROMs](#roms).

# Controllers and keys

The H19's keyboard is typed on the PC's. What a PC keyboard lacks is on the **H19
Keys** window. Control is the H19's, so CP/M's ^C, ^P, ^S and the rest reach the
machine, and the app's own shortcuts are on Ctrl+Shift (Cmd on a Mac).

## The H19 keys

| H19 key | PC key |
|---|---|
| f1 to f5 | <kbd>F1</kbd> to <kbd>F5</kbd> |
| ERASE | <kbd>F6</kbd> |
| BLUE, RED, GRAY | <kbd>F7</kbd> <kbd>F8</kbd> <kbd>F10</kbd> |
| BREAK | <kbd>Pause</kbd> |
| SCROLL | <kbd>Scroll Lock</kbd> |
| keypad (IC, DC, IL, DL, HOME) | the keypad with Num Lock off, or the arrow, Home and Insert keys |
| SHIFT+RESET | Machine ▸ Reset |

## The app's shortcuts

| Shortcut | Does |
|---|---|
| <kbd>Ctrl+Shift+C</kbd> | Copy Screen Text |
| <kbd>Ctrl+Shift+V</kbd> | Paste |
| <kbd>Ctrl+Shift+O</kbd> | Insert Disk |
| <kbd>Ctrl+Shift+R</kbd> | Reset |
| <kbd>Ctrl+Shift+Q</kbd> | Quit |
| <kbd>Ctrl+Shift+H</kbd> | Help |
| <kbd>Ctrl+,</kbd> | Preferences |
| <kbd>F9</kbd> | H19 Keys |
| <kbd>F11</kbd> | Fullscreen |
| <kbd>F12</kbd> | Debugger |

<!-- only: macos -->

On a Mac all of them are on ⌘ instead.

<!-- end -->

<!-- only: linux -->

Key combinations with the Super (Windows) key are the desktop's.

<!-- end -->

<!-- context: keys -->
# H19 Keys window

The H19 Keys window (<kbd>F9</kbd>) has the H19's keys a PC keyboard lacks (ERASE,
BLUE, RED, GRAY, BREAK, SCROLL, the keypad's IC, DC, IL and DL, HOME, the RESET
key and more), and a **SHIFT** latch: click it, then a key, to press that key
shifted; it lets go after one key. The terminal's mode lights show below. The
buttons never take the keyboard focus, so you can click ERASE and carry on
typing, and keys typed in the window go to the H89 as they would from the main
window.

# Machines

The H89 is one machine, strapped and stuffed as Preferences say. A change applies
with a power cycle when Preferences closes.

- **Model**: *H89 (ORG-0)* (the ORG-0 decoder lets CP/M put RAM at 0) or *H88 (no
  ORG-0)*.
- **Memory** on the CPU board: *16K*, *32K*, *48K* or *64K*.
- **Memory slot P503**: *None*, *WH-88-16 (64K)* or *MMS 77318 (128K)*.
- **Monitor ROM**: which monitor the CPU board has; the **SW501** switches take
  that monitor's fields.
- **Speed-up**: a faster Z80, *None (2.048 MHz)*, *2×*, *3×* up to *40×*. The disks and
  the clock keep their own time. Not with the MMS 77318.

# ROMs

The H89's monitor ROMs, the H17 controller's ROM and the H19's character generator
are copyrighted, so none comes with the app. **Import System ROMs…** (in the Machine
menu and on the Machine page of Preferences) takes MAME's `h89.zip` and
`heath_tlb.zip` as they are, or the loose images. Each is recognised by its
checksum, whatever it is called, and kept in the app's `roms` folder (see
[Files and folders](#files-and-folders)). The Machine page's **Imported ROMs**
row lists them.

# Disks

The disk controllers are cards in the I/O slots, and their drives are set on the
Slots & Drives page of Preferences:

| Controller | Drives |
|---|---|
| H-88-1 (H17), hard-sectored | 3 drives: H-17-1 (SS, 40 tracks) or H-17-4 (DS, 80 tracks) |
| Z-89-37 (H37), soft-sectored | 4 drives: 5.25" 40 or 80 tracks, single or double sided, or 8" |
| MMS 77316 | 8 units: 4 × 8", 4 × 5.25" |
| MMS 77320 (SASI) | 8 XEBEC hard disk units |

## Drives menu

Each fitted drive has a submenu: **Insert…**, **Eject** and **Write Protect**.
**Insert Disk…** (<kbd>Ctrl+Shift+O</kbd>) takes an image and puts it in a drive,
**New Blank Disk…** makes an empty one, and **Open Disks Folder** shows the app's
`disks` folder in your file manager. With no controller fitted the menu says so,
and takes you to Preferences.

## Disk images

H17 disks are raw tracks (`.h17raw`, `.rawdisk`), `.h8d` or H17Disk (`.h17disk`,
`.h17`); H37 and MMS disks are ImageDisk (`.imd`), Teledisk (`.td0`) or
VirtualH89 sector and raw images. The MMS 77320's hard disks are files the
controller creates in the app's `sasi` folder.

# FujiNet

The FujiNet is FujiNet's own firmware, running inside the app, and the H89-ESP32
card is its connection to the machine: an 82C55 at 0xD0 to 0xD3, driven by
fujinet-lib's `bus/h89`. The card dials FujiNet over a local socket (BoIP). Fit the
card in a slot on the Slots & Drives page: **FujiNet (H89-ESP32)**. Each program
sets up the card's 82C55A itself.

## The FujiNet menu

- **FujiNet Configuration** shows FujiNet's own web page, which mounts disks, joins
  networks and sets up hosts. It is on port 11513 on this computer only.
- **Console Log** shows what FujiNet prints. See [The console log](#the-console-log).
- **Restart FujiNet** restarts the FujiNet runtime.

<!-- context: fujilog -->
## The console log

The FujiNet Console Log window shows FujiNet's own messages as they come,
following the end while you are at the bottom.

## An external FujiNet

Preferences ▸ FujiNet ▸ **Use external FujiNet** skips the built-in FujiNet and
dials another FujiNet instance at the **Address:port** you give, for example
`127.0.0.1:11512`. Start that instance listening there (its `[BOIP]` port).
FujiNet Configuration and the console log belong to the built-in FujiNet, so they
say the FujiNet is external.

## Behind the scenes

- FujiNet's BoIP port is 11512 and its web UI 11513, on this computer only. A stray
  standalone `fujinet-pc` holding 11512 makes the link misbehave.
- FujiNet's settings and its SD folder are in the app's `fujinet` folder.

# Preferences

Preferences set up the machine, its slots and drives, its switches, the CP/NET host,
the picture and sound, and FujiNet. Changes to the machine apply with a power
cycle when Preferences closes, and say so as you make them; the picture and sound
apply at once.

<!-- only: macos -->

On a Mac this window is called **Settings** (⌘,).

<!-- end -->

<!-- context: prefs/machine -->
## Machine settings

- **Configuration**: whether the machine is as it is now, or changed.
- **Model**, **Memory**, **Memory slot P503**, **Monitor ROM** and **Speed-up**: see
  [Machines](#machines).
- **Imported ROMs**: the ROMs found, with an **Import…** button (see [ROMs](#roms)).

<!-- context: prefs/slots -->
## Slots and drives

- **P504**, **P505** and **P506** are the three I/O slots. Each takes one card:
  Empty, FujiNet (H89-ESP32), H-88-1 (H17 hard-sectored), Z-89-37 (H37
  soft-sectored), MMS 77316 (double density), MMS 77320 (SASI hard disk) or HA-88-3
  (3-port serial). Their ports may not overlap.
- **H-88-1 (H17) Drives**: Drive 1 to Drive 3.
- **Z-89-37 Drives**: Drive 1 to Drive 4.
- **MMS 77316 Drives**: Unit 1 to 4 are 8", Unit 5 to 8 are 5.25".
- **MMS 77320 SASI**: the **Port** (170Q or 174Q), the **Interrupt** (None, INT 3,
  INT 4 or INT 5), and Unit 0 to 7, each None or a XEBEC drive.

<!-- context: prefs/switches -->
## Switches

- **Set the device switches from the slots**: set SW501's boot controllers as the
  cards in the slots need them.
- **SW501 (CPU board)**: the monitor ROM's own switches, with the fields of the
  monitor chosen (memory test, boot mode, console baud rate, the controllers and
  the boot device).
- **SW401 (H19: the line)**: **Baud rate**, **Parity**, **Even parity**, **Stick
  parity** and **Full duplex**. HDOS and CP/M find the baud rate at boot.
- **SW402 (H19: the terminal)**: **Block cursor**, **No key click**, **Wrap at the
  end of a line**, **Line feed on carriage return**, **Carriage return on line
  feed**, **ANSI mode** (at power-up; Heath mode otherwise), **Keypad shifted** and
  **50 Hz refresh**.

<!-- context: prefs/cpnet -->
## CP/NET

**CP/NET device** adds a CP/NET host: a folder served as the machine's CP/NET
drives, where a, b and so on are folders inside it. **Port** is the device's I/O
port, **Client ID** the machine's id, and **Host folder** the folder you choose.

<!-- context: prefs/display -->
## Display and sound settings

- **Phosphor**: *White (P4)*, *Green (P1)* or *Amber (P3)* (setting
  `view.phosphor`).
- **Scanlines**: the gaps between the tube's lines (setting `view.scanlines`).
- **Blinking cursor** (setting `view.cursor_blink`).
- **Aspect**: *The tube's 4:3* or *Square pixels* (setting `view.aspect`).
- **Smooth scaling** (setting `view.smooth`).
- **Enable audio**: open the system audio device (setting `enable_audio`).
- **Volume**: 0 to 100 (setting `sound.volume`).
- **Bell**, **Key click** and **Visual bell**: the H19's bell and key click, and a
  screen flash for the bell (settings `sound.bell`, `sound.click` and
  `sound.visual_bell`).

<!-- context: prefs/fujinet -->
## FujiNet settings

- **Enable FujiNet**: off, the card's link stays down (setting `enable_fujinet`).
- **Use external FujiNet**: dial another instance instead (setting
  `fujinet_external`).
- **Address:port**: where to dial, for example `127.0.0.1:11512` (see
  [An external FujiNet](#an-external-fujinet); setting `fujinet_external_addr`).
- **Web UI**: FujiNet's page, with an **Open** button.
- **Card handshake delay**: the cycles before the ESP32 acknowledges a byte the H89
  writes.

## The settings file

The settings file is `settings.ini` (see [Files and folders](#files-and-folders)).
Each setting above names its key; the machine's configuration is kept in it too.

<!-- context: prefs/windows -->
## Preferences on Windows

On Windows, **Settings…** in the Machine menu opens one window with the same
settings, in the same pages.

# Debugger

The Debugger (<kbd>F12</kbd>) stops the machine and shows what it is doing. It has
six tabs; closing it lets the machine go on as it was. Opening it stops the
machine.

| Button | Does |
|---|---|
| Run or Stop (<kbd>F5</kbd>) | Stops before the next instruction, or runs on |
| Step (<kbd>F7</kbd>) | One instruction |
| Step Over (<kbd>F8</kbd>) | Over a call, `RST`, `DJNZ` or block operation |
| Step Out (<kbd>Shift+F8</kbd>) | On until the routine returns |
| Help | Opens the topic of the tab that shows |

<!-- context: debugger/prompt -->
## Prompt

The Prompt takes commands, with completion on <kbd>Tab</kbd>. Numbers are `$2000`,
`0x2000`, `2000h`, `8192`, split octal `040.000`, `020000Q` or `%1010`, and
expressions use registers, flags, `[addr]` and `{addr}` for memory, labels and C's
operators. `help` lists the commands.

| Command | Does |
|---|---|
| `step`, `over`, `out` | One instruction; over a call; to the return |
| `run`, `stop` | Go on or break |
| `runto` | Runs to an address |
| `ticks` | Runs some of the H89's 2 ms clock ticks |
| `break` | An execute breakpoint |
| `bpr`, `bpw` | Memory reads or writes |
| `bpin`, `bpout` | I/O port reads or writes |
| `delete`, `enable`, `clear` | Manage breakpoints (also `disable`, `breaks`) |
| `print`, `regs`, `set` | Evaluates an expression; shows or sets registers |
| `mem`, `poke` | Dumps memory; writes bytes |
| `radix` | How numbers are shown |
| `terminal`, `memmap`, `timer` | The H19, the memory map, the clock |
| `h17`, `fdc`, `sasi`, `serial`, `card` | The disk controllers, serial ports and the FujiNet card |
| `label`, `labels` | Name an address; load labels |
| `disasm` | Disassembles |
| `trace` | The instruction trace |
| `save` | Saves the disassembly, RAM or registers to a file |

<!-- context: debugger/cpu -->
## CPU

The Z80's registers and flags, editable while stopped, and a memory view.

<!-- context: debugger/disassembly -->
## Disassembly

The Z80 disassembled at the PC, with labels. Click a line to select it; **Follow
PC** keeps the current line in view.

<!-- context: debugger/breakpoints -->
## Breakpoints

Execute breakpoints, memory read and write and port in and out watchpoints, with
address ranges and conditions, each of which can be disabled. **Add** makes one,
**Clear All** removes them.

<!-- context: debugger/h89 -->
## H89

The machine's own state: the H19, the memory map, the clock and the disk and serial
controllers.

<!-- context: debugger/fujinet -->
## FujiNet packets

Every FujiBus packet on the card's link, both ways, recorded from power-on, with the
card's state. **Clear** empties the list.

# Files and folders

The app keeps its settings in one file, and everything else in its data
folder:

<!-- only: linux -->

On Linux, the settings are `~/.config/fujinet-go-h89/settings.ini` and
the data is in `~/.local/share/fujinet-go-h89` (or under
`$XDG_CONFIG_HOME` and `$XDG_DATA_HOME`). The GNOME and KDE apps share them.
As a Flatpak, they are under `~/.var/app/`, in the app's own folder.

<!-- end -->

<!-- only: windows -->

On Windows, the settings are `%APPDATA%\fujinet-go-h89\settings.ini` and
the data is in `%LOCALAPPDATA%\fujinet-go-h89`.

<!-- end -->

<!-- only: macos -->

On a Mac, the settings are `~/.config/fujinet-go-h89/settings.ini` and
the data is in `~/.local/share/fujinet-go-h89`.

<!-- end -->

In the data folder:

| Folder | Has |
|---|---|
| `roms` | the Heath ROMs you imported |
| `disks` | disk images |
| `sasi` | the MMS 77320's hard disk files |
| `fujinet` | FujiNet's settings, its SD folder (`SD`) and its data |

# Troubleshooting

- **The app asks for ROMs**: a ROM is missing. See [ROMs](#roms).
- **The machine does not boot**: no disk is in the boot drive, or the controller is
  not in a slot; check Preferences ▸ Slots & Drives.
- **Programs do not see the FujiNet**: the card is not fitted, or FujiNet is off;
  check the Slots & Drives and FujiNet pages.
- **A port is taken**: another program is using 11512 or 11513. Close the other
  FujiNet, or point the app at it with **Use external FujiNet**.
- **Keys don't reach the machine**: a dialog or another window is in front, or the
  key is one the app keeps (see [Controllers and keys](#controllers-and-keys)).
- **A disk is read-only**: its format is, or it is write protected in the Drives
  menu.

# About and licences

FujiNet Go H89 is free software under the GNU General Public License, version 3 or
later. Copyright © 2026 Thomas Cherryhomes.

- **VirtualH89** (the H89 emulator, BSD-3-Clause), vendored and adapted, is the
  machine.
- **The FujiNet firmware** (GPL-3.0-or-later) runs inside the app.
- No Heath ROM is part of the app; you import your own. "Heathkit", "Zenith" and
  "H89" are trademarks of their owners, used to name the machine emulated; this
  project is not affiliated with them.

[fujinet.online](https://fujinet.online/) ·
[fujinet-go-h89-desktop](https://github.com/FujiNetWIFI/fujinet-go-h89-desktop)
