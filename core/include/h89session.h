/*
 * h89session -- the toolkit-agnostic desktop session for FujiNet Go H89.
 *
 * Owns the emulator (the H89 on its own paced thread -- core/machine/host.h),
 * the in-process FujiNet runtime, the SDL audio for the H19's bell and key
 * click, the shared settings store, the machine configuration, the imported
 * ROMs, the disks in the drives and the path layout. Frontends (GTK4, Qt6,
 * AppKit, Win32) drive this API and do only windowing, painting and event
 * translation. A frontend that needs something which is not one of those
 * three things belongs here instead.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89SESSION_H
#define H89SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "h89config.h"
#include "h89drives.h"
#include "h89keys.h"
#include "h89roms.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The H19's picture: 80 columns of 8 pixels by 25 rows of 10 scanlines,
 * each scanline doubled (640x250 shown as 640x500). Frontends show it at
 * the CRT's 4:3 or with square pixels, per the "view.aspect" setting. */
#define H89SESSION_FB_WIDTH   640
#define H89SESSION_FB_HEIGHT  500

/* FujiNet's BoIP listener and its web admin UI: the family's next pair
 * (Astrocade 11500/11501, ColecoVision 11502/11503, Atari 2600
 * 11504/11505, NES 11506/11507, Master System 11508/11509, Atari 7800
 * 11510/11511), so siblings never collide.
 *
 * Direction: FujiNet LISTENS and the H89's FujiNet card dials in. That
 * decides startup ordering -- see h89session_start. */
#define H89SESSION_BOIP_PORT  11512
#define H89SESSION_WEBUI_PORT 11513

#define H89SESSION_AUDIO_RATE 48000

/* The accent every frontend uses for its highlights (the debugger's current
 * line, the FujiNet status dot, the drive activity light): the gold of the
 * Heathkit nameplate's frame, as on the icon. */
#define H89SESSION_ACCENT_RGB 0xE39B2D

typedef struct h89session h89session;
typedef struct h89debug h89debug;

/* All members optional (NULL = default).
 *  config_dir:  default $XDG_CONFIG_HOME/fujinet-go-h89
 *  data_dir:    default $XDG_DATA_HOME/fujinet-go-h89
 *  fujinet_lib: path to libfujinet.so/.dylib/.dll; default searches
 *               $FUJINET_LIB_DIR, the executable's directory, the dev
 *               build, the install libdir, then $FUJINET_LIB. "" disables
 *               FujiNet.
 *  fujinet_runtime_src: directory holding the pristine fnconfig.ini + data/
 *               + SD/ used to provision the user's runtime tree on first
 *               start (a macOS app passes its bundle's runtime dir). */
typedef struct {
    const char *config_dir;
    const char *data_dir;
    const char *fujinet_lib;
    const char *fujinet_runtime_src;
} h89session_paths;

h89session *h89session_new(const h89session_paths *paths);
void h89session_free(h89session *s);

/* ---- settings (one INI shared by every frontend) ------------------------- */
int         h89session_get_int(h89session *s, const char *key, int def);
void        h89session_set_int(h89session *s, const char *key, int value);
const char *h89session_get_str(h89session *s, const char *key, const char *def);
void        h89session_set_str(h89session *s, const char *key, const char *value);
void        h89session_settings_flush(h89session *s);

/* ---- the machine's configuration -------------------------------------------
 * Kept in the settings store under h89config.h's keys. get fills `cfg` from
 * the settings (defaults for what is not set); set stores it (validated by
 * the caller with h89_config_validate). The running machine is not changed
 * until the next power cycle; h89session_config_needs_power_cycle says
 * whether a configuration differs from the running one in a way that does. */
void h89session_get_config(h89session *s, h89_config *cfg);
void h89session_set_config(h89session *s, const h89_config *cfg);
int  h89session_config_needs_power_cycle(h89session *s, const h89_config *cfg);
/* The configuration the running machine was built with. */
void h89session_running_config(h89session *s, h89_config *cfg);

/* ---- lifecycle ------------------------------------------------------------ */
typedef struct {
    int enable_fujinet;       /* start the in-process FujiNet runtime */
    int enable_audio;         /* open the SDL audio device for the beeps */
    int paused;               /* build the machine stopped (the debugger) */
} h89session_start_opts;

/* Fills opts from the settings (keys: enable_fujinet, enable_audio). */
void h89session_default_opts(h89session *s, h89session_start_opts *opts);

/* Starts FujiNet (if enabled), then the machine, then puts back the disks
 * the drives held last time.
 *
 * The order is not arbitrary: FujiNet listens and the card dials in, so the
 * listener has to exist first. FujiNet failing to start is NOT fatal: the
 * machine runs with the card's link down.
 *
 * Returns 0, or -1 with h89session_last_error() set -- notably when the
 * ROMs the configuration needs have not been imported
 * (h89session_missing_roms says which). */
int  h89session_start(h89session *s, const h89session_start_opts *opts);
void h89session_stop(h89session *s);
int  h89session_is_running(const h89session *s);
const char *h89session_last_error(const h89session *s);

/* Power cycle with the configuration in the settings now: every disk is
 * written back, the machine rebuilt, the disks put back. FujiNet stays up. */
int  h89session_power_cycle(h89session *s);
/* SHIFT+RESET on the H19: the H89's /RESET (CPU, cards, terminal). */
void h89session_reset(h89session *s);

/* Run as fast as the host allows (FujiNet traffic still runs at real time,
 * since the H89's FujiNet timeouts count its own clock). */
void h89session_set_warp(h89session *s, int on);
int  h89session_warp(h89session *s);

/* ---- ROMs (h89roms.h's table) ------------------------------------------------
 * Imported into the ROM directory under MAME's file names. */
int  h89session_rom_available(h89session *s, int index);
/* Imports every known image in a file -- MAME's h89.zip or heath_tlb.zip as
 * they are, or a loose image. Returns how many were imported (0: nothing in
 * it this app knows), or -1 with the error; `msg` always says what happened. */
int  h89session_import_roms(h89session *s, const char *path, char *msg, int msgsz);
/* What the configuration in the settings needs that is not imported, as a
 * sentence ("the H19 font (444-29), the H-17 ROM (444-19)"); returns how
 * many images are missing. */
int  h89session_missing_roms(h89session *s, char *what, int whatsz);
/* The imported monitor ROMs, for the Preferences' choice: returns how many;
 * fills files[] (ROM table indexes) up to max. */
int  h89session_monitors(h89session *s, int *indexes, int max);
const char *h89session_roms_path(const h89session *s);

/* ---- video ------------------------------------------------------------------
 * copy_frame copies the latest frame into dst (H89SESSION_FB_WIDTH *
 * H89SESSION_FB_HEIGHT uint32 XRGB8888 pixels) iff its serial differs from
 * *serial_inout, updates it and returns 1; else 0. Pass 0 to force a copy. */
int  h89session_copy_frame(h89session *s, uint32_t *dst, uint64_t *serial_inout);
/* Applies the view settings (view.phosphor: 0 white, 1 green, 2 amber;
 * view.scanlines; view.cursor_blink) to the renderer; call after setting
 * them. */
void h89session_apply_view(h89session *s);
/* Kept for the family's display code; the H89 paces on its own clock. */
void h89session_notify_vsync(h89session *s, int64_t frame_time_ns);

/* ---- audio ----------------------------------------------------------------- */
/* Owned by the session (SDL) when opts.enable_audio was set; a frontend
 * that wants the device itself pulls interleaved stereo float frames at
 * H89SESSION_AUDIO_RATE instead. Returns the frames written. */
int  h89session_render_audio(h89session *s, float *out, int nframes);
void h89session_set_volume(h89session *s, int percent);
/* Bells since the last call: for a frontend's visual bell. */
int  h89session_take_visual_bells(h89session *s);

/* ---- the keyboard ------------------------------------------------------------
 * keysym is an X11/xkb keysym (a GDK keyval; Qt, Win32 and AppKit map
 * through h89session_keysym_from_*), unicode the character the toolkit says
 * the key types (0 for none). Returns the H19 key (an ASCII code or an
 * H89K_* code from h89keys.h), or -1 for a key the H19 has no use for.
 * Pure: the same everywhere, and unit-tested. */
int  h89session_key_from_event(uint32_t keysym, uint32_t unicode, int ctrl, int shift);
/* Press an H19 key (h89session_key_from_event's result, or a key from an
 * H19 Keys panel). `shift` matters for the keypad and SCROLL. */
void h89session_key(h89session *s, int key, int shift);

/* Native key codes for the toolkits that do not deliver keysyms: a Win32
 * virtual-key (with the extended flag: the dedicated Home/arrow/Insert
 * keys have it, the keypad's do not), a macOS virtual key code, a Qt
 * Qt::Key (with Qt::KeypadModifier). 0 when the key is not one the H19
 * maps (characters arrive as unicode instead). */
uint32_t h89session_keysym_from_win_vk(unsigned vk, int extended);
uint32_t h89session_keysym_from_macos_keycode(unsigned keycode);
uint32_t h89session_keysym_from_qt_key(unsigned qtkey, int keypad);

/* The keysyms the session uses (X11 values). */
enum {
    H89_KEYSYM_BACKSPACE = 0xff08, H89_KEYSYM_TAB = 0xff09,
    H89_KEYSYM_LINEFEED = 0xff0a, H89_KEYSYM_RETURN = 0xff0d,
    H89_KEYSYM_PAUSE = 0xff13, H89_KEYSYM_SCROLL_LOCK = 0xff14,
    H89_KEYSYM_ESCAPE = 0xff1b, H89_KEYSYM_HOME = 0xff50,
    H89_KEYSYM_LEFT = 0xff51, H89_KEYSYM_UP = 0xff52,
    H89_KEYSYM_RIGHT = 0xff53, H89_KEYSYM_DOWN = 0xff54,
    H89_KEYSYM_PAGE_UP = 0xff55, H89_KEYSYM_PAGE_DOWN = 0xff56,
    H89_KEYSYM_END = 0xff57, H89_KEYSYM_INSERT = 0xff63,
    H89_KEYSYM_BREAK = 0xff6b,
    H89_KEYSYM_KP_ENTER = 0xff8d, H89_KEYSYM_KP_HOME = 0xff95,
    H89_KEYSYM_KP_LEFT = 0xff96, H89_KEYSYM_KP_UP = 0xff97,
    H89_KEYSYM_KP_RIGHT = 0xff98, H89_KEYSYM_KP_DOWN = 0xff99,
    H89_KEYSYM_KP_PAGE_UP = 0xff9a, H89_KEYSYM_KP_PAGE_DOWN = 0xff9b,
    H89_KEYSYM_KP_END = 0xff9c, H89_KEYSYM_KP_BEGIN = 0xff9d,
    H89_KEYSYM_KP_INSERT = 0xff9e, H89_KEYSYM_KP_DELETE = 0xff9f,
    H89_KEYSYM_KP_DECIMAL = 0xffae,
    H89_KEYSYM_KP_0 = 0xffb0, H89_KEYSYM_KP_9 = 0xffb9,
    H89_KEYSYM_F1 = 0xffbe, H89_KEYSYM_F12 = 0xffc9,
    H89_KEYSYM_DELETE = 0xffff
};

/* ---- text in and out ----------------------------------------------------------
 * Paste types the text on the H19's keyboard at the line's pace (newlines
 * become RETURN; anything the H19 cannot type becomes '?'). */
size_t h89session_paste(h89session *s, const char *utf8);
void   h89session_paste_cancel(h89session *s);
int    h89session_paste_pending(h89session *s);
/* The screen as UTF-8 (graphics characters as their nearest Unicode); the
 * length, 0 when stopped. */
size_t h89session_screen_text(h89session *s, char *dst, size_t dstsz, int line25);

/* ---- drives ---------------------------------------------------------------------
 * The running machine's drives (h89drives.h), with the session's memory:
 * whatever a drive holds is remembered (drive.<id>.path / .wp) and put back
 * at the next start or power cycle. */
int  h89session_drive_count(h89session *s);
int  h89session_drive_info_at(h89session *s, int index, h89_drive_info *out);
int  h89session_drive_info(h89session *s, int id, h89_drive_info *out);
int  h89session_drive_insert(h89session *s, int id, const char *path, int flags,
                             char *err, int errsz);
int  h89session_drive_eject(h89session *s, int id, char *err, int errsz);
int  h89session_drive_set_write_protect(h89session *s, int id, int protect);
/* The drives a disk image would go in, best first (an empty one first): a
 * hard-sectored image the H17's, a 5.25" soft-sectored one the H37's then
 * the MMS 77316's 5.25" units, an 8" one the MMS 77316's 8" units. Returns
 * how many (0: the machine has no drive for it). */
int  h89session_drives_for_image(h89session *s, const char *path, int *ids, int max);

/* What a file is, by name, size and contents. */
typedef enum {
    H89_MEDIA_UNKNOWN = 0,
    H89_MEDIA_H17,            /* a hard-sectored disk */
    H89_MEDIA_SOFT_5,         /* a 5.25" soft-sectored disk */
    H89_MEDIA_SOFT_8,         /* an 8" soft-sectored disk */
    H89_MEDIA_ROMS,           /* ROMs this app knows (a zip or an image) */
} h89_media_kind;
int  h89session_media_kind(const char *path);

/* A dropped file, routed: ROMs are imported, a disk image goes in the first
 * empty drive that takes it, anything else is copied to FujiNet's SD folder
 * (where the web UI can mount it). Returns 0 with what was done in `msg`,
 * or -1 with why not. A disk whose drives are all full returns 1 with the
 * candidate drives in ids[] for the frontend to ask which. */
int  h89session_import_media(h89session *s, const char *path, char *msg, int msgsz,
                             int *ids, int maxids, int *nids);

/* New blank disks (the "New Blank Disk..." dialog). */
typedef enum {
    H89_BLANK_H17_H8D = 0,    /* sectors present, zeroed: HDOS INIT/CP/M FORMAT not needed for CP/M */
    H89_BLANK_H17_H17D,       /* the same, as H17Disk */
    H89_BLANK_H17_RAW,        /* unformatted raw tracks: format it in the H89 */
    H89_BLANK_IMD,            /* an empty ImageDisk: format it in the H89 */
    H89_BLANK_KIND_COUNT
} h89_blank_kind;
int  h89session_new_blank_disk(h89session *s, const char *path, int kind, int sides,
                               int tracks, char *err, int errsz);
const char *h89session_disks_path(const h89session *s);

/* ---- FujiNet ----------------------------------------------------------------- */
int         h89session_fujinet_running(const h89session *s);
const char *h89session_fujinet_webui_url(const h89session *s);
int         h89session_fujinet_copy_log(h89session *s, char *dst, int max);
int         h89session_fujinet_restart(h89session *s);
/* The card's link: -1 no card, else FujiNetCard::Link (0 no runtime,
 * 1 waiting, 2 up, 3 the H89 holds the ESP32 in reset, 4 ESP32 in its ROM
 * loader). */
int         h89session_card_link(h89session *s);
const char *h89session_card_link_text(int link);

const char *h89session_config_path(const h89session *s);
const char *h89session_data_path(const h89session *s);
const char *h89session_sd_path(const h89session *s);

/* ---- debugger --------------------------------------------------------------- */
h89debug *h89session_debugger(h89session *s);

#ifdef __cplusplus
}
#endif

#endif /* H89SESSION_H */
