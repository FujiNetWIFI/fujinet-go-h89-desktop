/*
 * host -- the emulator host: owns the machine, runs it on its own thread at
 * real speed, and publishes the H19's picture for the session layer.
 *
 * Process-singleton by design, like every core in this family:
 * h89host_start refuses a second concurrent machine.
 *
 * Pacing: the H89's own clock is the 2 ms timer, so the machine runs in
 * 2 ms slices (4096 cycles at 2.048 MHz, times the speed-up factor), eight to
 * a batch, against an absolute monotonic deadline ladder; a batch that falls
 * more than four batches behind resyncs instead of fast-forwarding. The
 * picture is re-rendered at the end of a batch when the screen or the
 * cursor blink changed (at most every 16 ms). H89_PACE_LOG=1 prints the
 * ladder's health once a second; boot_smoke measures it.
 *
 * C API over C++ (Host.cpp) so the C session layer can use it.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_HOST_H
#define H89_HOST_H

#include <stddef.h>
#include <stdint.h>

#include "h89config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define H89HOST_FB_WIDTH  640
#define H89HOST_FB_HEIGHT 500

typedef struct {
    h89_config config;
    const uint8_t *monitor;      /* copied */
    size_t monitor_size;
    const uint8_t *h17_rom;      /* 444-19; may be NULL without an H17 */
    size_t h17_size;
    const uint8_t *font;         /* 444-29 */
    size_t font_size;
    const char *boip_host;       /* FujiNet's BoIP listener */
    int boip_port;               /* 0: the card has nowhere to dial */
    const char *sasi_dir;        /* the MMS 77320's unit files */
    int paused;                  /* build stopped (the debugger attaches) */
} h89host_opts;

/* Builds the machine (on this thread; errors are reported here) and starts
 * its thread. Returns 0, or -1 with h89host_last_error(). */
int  h89host_start(const h89host_opts *opts);
/* Stops and joins the thread and destroys the machine. Safe when stopped. */
void h89host_stop(void);
int  h89host_is_running(void);
const char *h89host_last_error(void);

/* SHIFT+RESET: latched, applied between batches. */
void h89host_reset(void);

/* ---- video ----
 * Copies the newest frame (H89HOST_FB_WIDTH x H89HOST_FB_HEIGHT XRGB) when
 * its serial differs from *serial_inout; returns 1 and updates the serial,
 * else 0. Pass serial 0 to force a copy. */
int  h89host_frame_copy(uint32_t *dst, uint64_t *serial_inout);
/* phosphor: H19Render::Phosphor; takes effect at the next frame. */
void h89host_set_view(int phosphor, int scanlines, int cursor_blink);

/* ---- the keyboard (any thread) ---- */
void   h89host_key(unsigned code, int shift);
size_t h89host_paste(const char *utf8);
void   h89host_paste_cancel(void);
int    h89host_paste_pending(void);

/* ---- sound events since the last call (any thread) ---- */
void h89host_take_sounds(unsigned *bells, unsigned *clicks);

/* The screen as UTF-8 text; returns the length (0 when stopped). */
size_t h89host_screen_text(char *dst, size_t dstsz, int line25);
/* The cursor's line up to the cursor (graphics characters as '?'): what a
 * script waits on for a prompt. Returns the length. */
size_t h89host_text_before_cursor(char *dst, size_t dstsz);

/* Runs without sleeping; FujiNet traffic still runs at real time (the
 * H89's FujiNet timeouts count its own 2 ms ticks). */
void h89host_set_warp(int on);
int  h89host_warp(void);

/* Emulated time since start, in 2 ms slices. */
uint64_t h89host_slices(void);

typedef struct {
    uint64_t batches, sleeps, late_ns, resyncs;
    long worst_late_ns;
} h89host_pacing;
/* Read after h89host_stop. */
void h89host_get_pacing(h89host_pacing *out);

/* The FujiNet card's link (FujiNetCard::Link), or -1 without a card. */
int h89host_card_link(void);

/* ---- the run lock ----
 * Held by the machine thread while it runs a batch. Any other thread that
 * reads or edits the machine holds it around the access (and is inside the
 * machine's context while it does). Re-entrant per thread; a no-op on the
 * machine thread. Never call h89host_stop while holding it. */
void h89host_lock(void);
void h89host_unlock(void);
/* The debugger's park: the machine thread lets go of the run lock while it
 * is stopped inside the instruction hook, and takes it back to run on. */
void h89host_park_release(void);
void h89host_park_reacquire(void);
/* 1 when called on the machine thread. */
int  h89host_on_machine_thread(void);

/* Pause/run the slice loop (the debugger's "stopped before start"); the
 * machine thread keeps the run lock released while paused. */
void h89host_set_paused(int paused);
int  h89host_paused(void);

#ifdef __cplusplus
}

class Machine;
/* The live machine; NULL when stopped. Only touched under the run lock or
 * on the machine thread. */
Machine *h89host_machine(void);
#endif

#endif /* H89_HOST_H */
