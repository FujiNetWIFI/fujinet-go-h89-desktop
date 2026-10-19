/*
 * h89session's private state. Not installed; only the core/src sources
 * include it. One struct shared by the session's C modules (settings,
 * paths, roms, media, audio, the FujiNet runtime).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_SESSION_INTERNAL_H
#define H89_SESSION_INTERNAL_H

#include <pthread.h>
#include <stdint.h>

#include "h89session.h"

#ifdef __cplusplus
extern "C" {
#endif

#define H89_PATH_MAX 1024

typedef struct setting_kv {
    char *key;
    char *val;
    struct setting_kv *next;
} setting_kv;

struct h89session {
    char config_dir[H89_PATH_MAX];
    char data_dir[H89_PATH_MAX];
    char roms_dir[H89_PATH_MAX];       /* <data>/roms: the imported ROMs */
    char disks_dir[H89_PATH_MAX];      /* <data>/disks: New Blank Disk's default */
    char sasi_dir[H89_PATH_MAX];       /* <data>/sasi: the MMS 77320's unit files */
    char settings_file[H89_PATH_MAX];

    setting_kv *settings;
    pthread_mutex_t settings_mtx;
    int settings_dirty;

    char last_error[256];

    /* ---- FujiNet runtime (fujinet_runtime.c) ---- */
    char fujinet_root[H89_PATH_MAX];    /* <data>/fujinet */
    char fujinet_config[H89_PATH_MAX];  /* .../fnconfig.ini */
    char fujinet_sd[H89_PATH_MAX];      /* .../SD */
    char fujinet_data[H89_PATH_MAX];    /* .../data */
    char fujinet_lib[H89_PATH_MAX];     /* resolved libfujinet path, "" until then */
    int  fujinet_disabled;              /* the caller passed fujinet_lib "" */
    char fujinet_runtime_src[H89_PATH_MAX]; /* caller-given pristine tree, or "" */
    char webui_url[64];                 /* http://127.0.0.1:11513/ */
    int  fujinet_running;

    /* the running configuration */
    h89session_start_opts opts;
    h89_config running_cfg;
    int running;

    void *audio;              /* audio_sdl.c state, NULL until started */
    void *debugger;           /* h89debug, lazily created */
    int volume;               /* percent */
    unsigned visual_bells;
};

void settings_init(struct h89session *s);
void settings_free_all(struct h89session *s);

int paths_init(struct h89session *s, const char *config_dir, const char *data_dir);
/* The in-process FujiNet makes its runtime root the process's working
 * directory, so a path given relative to where the app started (a
 * command-line argument) is resolved against the directory the first
 * session was created in. paths_resolve returns `path` itself when it is
 * absolute, else `buf`. */
void paths_capture_launch_dir(void);
const char *paths_resolve(const char *path, char *buf, size_t bufsz);
int paths_mkdir_p(const char *path);
int paths_copy_file(const char *src, const char *dst);
/* Locate libfujinet and provision the runtime tree (fnconfig.ini + data/ +
 * SD/) into <data>/fujinet on first run. Returns 0, or -1 if no runtime is
 * available (not fatal to the session -- see fujinet_start). */
int paths_provision_fujinet(struct h89session *s);

void session_set_error(struct h89session *s, const char *fmt, ...);

/* fujinet_runtime.c */
int  fujinet_start(struct h89session *s);
void fujinet_stop(struct h89session *s);
/* Block (up to timeout_ms) until the BoIP port accepts, so the card's first
 * dial-out finds the listener. Returns 0 once up, -1 on timeout. */
int  fujinet_wait_for_boip(struct h89session *s, int timeout_ms);

/* roms.c */
/* Write any images compiled in with WITH_H89_ROMS into the ROM directory
 * (once; an existing file is left alone). */
void roms_provision_embedded(struct h89session *s);
/* Read ROM table entry `index` from the ROM directory; a malloc'd buffer
 * (caller frees) and its size, or NULL. */
uint8_t *roms_load(struct h89session *s, int index, size_t *size);

/* audio_sdl.c */
int  audio_start(struct h89session *s);
void audio_stop(struct h89session *s);

/* debug.c: the session tells the debugger when the machine goes away and
 * comes back (a power cycle replaces it). */
void h89debug_before_power_cycle(struct h89session *s);
void h89debug_after_power_cycle(struct h89session *s);
/* The FujiNet trace, from the session's start (the engine is created for it). */
void h89debug_fujinet_hook(struct h89session *s);
void h89debug_destroy(struct h89session *s);

#ifdef __cplusplus
}
#endif

#endif /* H89_SESSION_INTERNAL_H */
