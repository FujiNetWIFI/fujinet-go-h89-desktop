/*
 * session.c -- the frontend contract (core/include/h89session.h) over the
 * H89 host (core/machine/host.h).
 *
 * The C modules beside it (settings, paths, roms, media, audio, the beeper,
 * the FujiNet runtime) share the session struct through session_internal.h;
 * this file owns the lifecycle, the configuration, the power cycles, the
 * drives' memory and the input routing. Shaped after the SMS sibling's.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "host.h"
#include "session_internal.h"

/* ---- helpers shared with the C modules ----------------------------------- */

void session_set_error(struct h89session *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->last_error, sizeof s->last_error, fmt, ap);
    va_end(ap);
}

static int file_exists(const char *p)
{
    struct stat st;
    return p && *p && stat(p, &st) == 0;
}

/* ---- lifecycle ------------------------------------------------------------- */

h89session *h89session_new(const h89session_paths *paths)
{
    struct h89session *s;

    paths_capture_launch_dir();
    s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    pthread_mutex_init(&s->settings_mtx, NULL);

    if (paths_init(s, paths ? paths->config_dir : NULL, paths ? paths->data_dir : NULL) != 0) {
        free(s);
        return NULL;
    }
    settings_init(s);
    roms_provision_embedded(s);
    s->volume = h89session_get_int(s, "sound.volume", 70);

    if (paths && paths->fujinet_lib) {
        snprintf(s->fujinet_lib, sizeof s->fujinet_lib, "%s", paths->fujinet_lib);
        s->fujinet_disabled = !paths->fujinet_lib[0];
    }
    if (paths && paths->fujinet_runtime_src)
        snprintf(s->fujinet_runtime_src, sizeof s->fujinet_runtime_src, "%s",
                 paths->fujinet_runtime_src);
    return s;
}

void h89session_free(h89session *s)
{
    if (!s)
        return;
    h89session_stop(s);
    fujinet_stop(s);
    h89session_settings_flush(s);
    h89debug_destroy(s);
    settings_free_all(s);
    free(s);
}

void h89session_default_opts(h89session *s, h89session_start_opts *opts)
{
    memset(opts, 0, sizeof *opts);
    opts->enable_fujinet = h89session_get_int(s, "enable_fujinet", 1);
    opts->enable_audio = h89session_get_int(s, "enable_audio", 1);
}

/* ---- the configuration ------------------------------------------------------ */

void h89session_get_config(h89session *s, h89_config *cfg)
{
    char key[64];
    h89_config_default(cfg);
    for (int i = 0; i < h89_config_key_count(); i++) {
        const char *k = h89_config_key_at(i);
        const char *v;
        snprintf(key, sizeof key, "config.%s", k);
        v = h89session_get_str(s, key, NULL);
        if (v && *v)
            h89_config_set(cfg, k, v);
    }
    /* the monitor must be one that is imported: the default MTR-90 falls back
     * to the first monitor the user has */
    {
        int ix = h89_rom_find_file(cfg->monitor);
        if (ix < 0 || !h89session_rom_available(s, ix)) {
            int mons[32];
            if (h89session_monitors(s, mons, 32) > 0 &&
                !h89session_get_str(s, "config.machine.monitor", NULL))
                snprintf(cfg->monitor, sizeof cfg->monitor, "%s", h89_rom_at(mons[0])->file);
        }
    }
}

void h89session_set_config(h89session *s, const h89_config *cfg)
{
    char key[64], val[1100];
    for (int i = 0; i < h89_config_key_count(); i++) {
        const char *k = h89_config_key_at(i);
        if (h89_config_get(cfg, k, val, sizeof val) != 0)
            continue;
        snprintf(key, sizeof key, "config.%s", k);
        h89session_set_str(s, key, val);
    }
}

int h89session_config_needs_power_cycle(h89session *s, const h89_config *cfg)
{
    char a[1100], b[1100];
    if (!s->running)
        return 0;
    for (int i = 0; i < h89_config_key_count(); i++) {
        const char *k = h89_config_key_at(i);
        h89_config_get(cfg, k, a, sizeof a);
        h89_config_get(&s->running_cfg, k, b, sizeof b);
        if (strcmp(a, b) != 0)
            return 1;
    }
    return 0;
}

void h89session_running_config(h89session *s, h89_config *cfg)
{
    if (s->running)
        *cfg = s->running_cfg;
    else
        h89session_get_config(s, cfg);
}

/* ---- bringing the machine up ---------------------------------------------------- */

static void restore_disks(struct h89session *s)
{
    int n = h89drive_count();
    for (int i = 0; i < n; i++) {
        h89_drive_info d;
        char key[64], err[256];
        const char *path;
        if (h89drive_info_at(i, &d) != 0 || !d.removable)
            continue;
        snprintf(key, sizeof key, "drive.%d.path", d.id);
        path = h89session_get_str(s, key, "");
        if (!path || !*path)
            continue;
        if (!file_exists(path)) {
            fprintf(stderr, "h89: %s: %s is gone; left empty\n", d.label, path);
            continue;
        }
        snprintf(key, sizeof key, "drive.%d.wp", d.id);
        if (h89drive_insert(d.id, path, h89session_get_int(s, key, 0) ? H89DRIVE_READ_ONLY : 0,
                            err, sizeof err) != 0)
            fprintf(stderr, "h89: %s: %s\n", d.label, err);
    }
}

static int host_up(struct h89session *s)
{
    h89host_opts o;
    h89_config cfg;
    uint8_t *mon = NULL, *h17 = NULL, *font = NULL;
    size_t mon_size = 0, h17_size = 0, font_size = 0;
    char why[256];
    int rc, has_h17 = 0;

    h89session_get_config(s, &cfg);
    if (!h89_config_validate(&cfg, why, sizeof why)) {
        session_set_error(s, "%s", why);
        return -1;
    }
    if (h89session_missing_roms(s, why, sizeof why) > 0) {
        session_set_error(s, "Import the H89 ROMs first: this machine needs %s.", why);
        return -1;
    }
    for (int i = 0; i < H89_SLOTS; i++)
        has_h17 |= cfg.slot[i] == H89_CARD_H17;

    mon = roms_load(s, h89_rom_find_file(cfg.monitor), &mon_size);
    font = roms_load(s, h89_rom_find_file("2716_444-29_h19font.u420"), &font_size);
    if (has_h17)
        h17 = roms_load(s, h89_rom_find_file("2716_444-19_h17.u520"), &h17_size);

    memset(&o, 0, sizeof o);
    o.config = cfg;
    o.monitor = mon;
    o.monitor_size = mon_size;
    o.h17_rom = h17;
    o.h17_size = h17_size;
    o.font = font;
    o.font_size = font_size;
    o.boip_host = "127.0.0.1";
    /* The card dials FujiNet's BoIP port -- the in-process runtime's, or a
     * standalone fujinet-pc's when the runtime is switched off. A session
     * with FujiNet disabled outright (fujinet_lib "", the tests) has a card
     * with nowhere to dial, so it can never pick up someone else's FujiNet. */
    o.boip_port = s->fujinet_disabled ? 0 : H89SESSION_BOIP_PORT;
    o.sasi_dir = s->sasi_dir;
    o.paused = s->opts.paused;

    rc = h89host_start(&o);
    if (rc != 0)
        session_set_error(s, "%s", h89host_last_error());
    else
        s->running_cfg = cfg;
    free(mon);
    free(h17);
    free(font);
    if (rc == 0) {
        h89session_apply_view(s);
        h89host_set_warp(h89session_get_int(s, "machine.warp", 0));
        restore_disks(s);
    }
    return rc;
}

int h89session_start(h89session *s, const h89session_start_opts *opts)
{
    h89session_start_opts local;

    if (s->running)
        return 0;
    s->last_error[0] = '\0';
    if (!opts) {
        h89session_default_opts(s, &local);
        opts = &local;
    }
    s->opts = *opts;

    /* FujiNet FIRST: it listens and the card dials in. Failing to start is
     * NOT fatal (the card redials whenever the link is down). */
    if (s->opts.enable_fujinet && !s->fujinet_disabled) {
        if (fujinet_start(s) == 0)
            fujinet_wait_for_boip(s, 3000);
    }

    if (host_up(s) != 0)
        return -1;
    s->running = 1;
    h89debug_after_power_cycle(s);

    if (s->opts.enable_audio && audio_start(s) != 0)
        fprintf(stderr, "h89: no audio (%s)\n", s->last_error);
    return 0;
}

void h89session_stop(h89session *s)
{
    if (!s->running)
        return;
    audio_stop(s);
    h89debug_before_power_cycle(s);
    h89host_stop();          /* writes every disk back first */
    s->running = 0;
}

int h89session_is_running(const h89session *s)
{
    return s->running;
}

const char *h89session_last_error(const h89session *s)
{
    return s->last_error;
}

int h89session_power_cycle(h89session *s)
{
    int rc;
    if (!s->running)
        return h89session_start(s, NULL);
    h89debug_before_power_cycle(s);
    h89host_stop();
    rc = host_up(s);
    if (rc != 0) {
        /* the new configuration does not build: put the last one back */
        char err[256];
        snprintf(err, sizeof err, "%s", s->last_error);
        h89session_set_config(s, &s->running_cfg);
        if (host_up(s) != 0)
            s->running = 0;
        snprintf(s->last_error, sizeof s->last_error, "%s", err);
    }
    h89debug_after_power_cycle(s);
    return rc;
}

void h89session_reset(h89session *s)
{
    if (s->running)
        h89host_reset();
}

void h89session_set_warp(h89session *s, int on)
{
    h89session_set_int(s, "machine.warp", on ? 1 : 0);
    h89host_set_warp(on);
}

int h89session_warp(h89session *s)
{
    (void)s;
    return h89host_warp();
}

/* ---- video ------------------------------------------------------------------ */

int h89session_copy_frame(h89session *s, uint32_t *dst, uint64_t *serial_inout)
{
    (void)s;
    return h89host_frame_copy(dst, serial_inout);
}

void h89session_apply_view(h89session *s)
{
    h89host_set_view(h89session_get_int(s, "view.phosphor", 0),
                     h89session_get_int(s, "view.scanlines", 0),
                     h89session_get_int(s, "view.cursor_blink", 1));
}

void h89session_notify_vsync(h89session *s, int64_t frame_time_ns)
{
    (void)s;
    (void)frame_time_ns;
}

/* ---- the keyboard and text ------------------------------------------------- */

void h89session_key(h89session *s, int key, int shift)
{
    if (!s->running || key < 0)
        return;
    h89host_key((unsigned)key, shift);
}

size_t h89session_paste(h89session *s, const char *utf8)
{
    return s->running ? h89host_paste(utf8) : 0;
}

void h89session_paste_cancel(h89session *s)
{
    if (s->running)
        h89host_paste_cancel();
}

int h89session_paste_pending(h89session *s)
{
    return s->running ? h89host_paste_pending() : 0;
}

size_t h89session_screen_text(h89session *s, char *dst, size_t dstsz, int line25)
{
    (void)s;
    return h89host_screen_text(dst, dstsz, line25);
}

unsigned h89session_h19_modes(h89session *s)
{
    (void)s;
    return h89host_h19_modes();
}

/* ---- drives -------------------------------------------------------------------- */

int h89session_drive_count(h89session *s)
{
    return s->running ? h89drive_count() : 0;
}

int h89session_drive_info_at(h89session *s, int index, h89_drive_info *out)
{
    return s->running ? h89drive_info_at(index, out) : -1;
}

int h89session_drive_info(h89session *s, int id, h89_drive_info *out)
{
    return s->running ? h89drive_info(id, out) : -1;
}

int h89session_drive_insert(h89session *s, int id, const char *path, int flags, char *err,
                            int errsz)
{
    char abs[H89_PATH_MAX], key[64];
    if (!s->running) {
        snprintf(err, (size_t)errsz, "the machine is not running");
        return -1;
    }
    path = paths_resolve(path, abs, sizeof abs);
    if (h89drive_insert(id, path, flags, err, errsz) != 0)
        return -1;
    snprintf(key, sizeof key, "drive.%d.path", id);
    h89session_set_str(s, key, path);
    snprintf(key, sizeof key, "drive.%d.wp", id);
    h89session_set_int(s, key, (flags & H89DRIVE_READ_ONLY) ? 1 : 0);
    return 0;
}

int h89session_drive_eject(h89session *s, int id, char *err, int errsz)
{
    char key[64];
    int rc;
    if (!s->running) {
        snprintf(err, (size_t)errsz, "the machine is not running");
        return -1;
    }
    rc = h89drive_eject(id, err, errsz);
    snprintf(key, sizeof key, "drive.%d.path", id);
    h89session_set_str(s, key, "");
    return rc;
}

int h89session_drive_set_write_protect(h89session *s, int id, int protect)
{
    char key[64];
    if (!s->running || h89drive_set_write_protect(id, protect) != 0)
        return -1;
    snprintf(key, sizeof key, "drive.%d.wp", id);
    h89session_set_int(s, key, protect ? 1 : 0);
    return 0;
}

int h89session_new_blank_disk(h89session *s, const char *path, int kind, int sides, int tracks,
                              char *err, int errsz)
{
    (void)s;
    return h89image_create_blank(path, kind, sides, tracks, err, errsz);
}

const char *h89session_disks_path(const h89session *s)
{
    return s->disks_dir;
}

/* ---- FujiNet --------------------------------------------------------------------- */

int h89session_fujinet_running(const h89session *s)
{
    return s->fujinet_running;
}

const char *h89session_fujinet_webui_url(const h89session *s)
{
    return s->webui_url;
}

int h89session_fujinet_restart(h89session *s)
{
    if (s->fujinet_disabled)
        return -1;
    fujinet_stop(s);
    if (fujinet_start(s) != 0)
        return -1;
    return fujinet_wait_for_boip(s, 3000);
}

int h89session_card_link(h89session *s)
{
    return s->running ? h89host_card_link() : -1;
}

const char *h89session_card_link_text(int link)
{
    switch (link) {
    case 0: return "FujiNet is not running";
    case 1: return "FujiNet: connecting\xe2\x80\xa6";
    case 2: return "FujiNet: connected";
    /* the 82C55 holds the ESP32's EN low from power-on until a program
     * runs fujinet-lib's port_init: idle, not broken */
    case 3: return "FujiNet card idle until a program starts it";
    case 4: return "FujiNet card: ESP32 in its ROM loader";
    default: return "No FujiNet card";
    }
}

const char *h89session_config_path(const h89session *s)
{
    return s->config_dir;
}

const char *h89session_data_path(const h89session *s)
{
    return s->data_dir;
}

const char *h89session_sd_path(const h89session *s)
{
    return s->fujinet_sd;
}
