/*
 * h89drives -- the running machine's disk drives: what each holds, and
 * inserting, ejecting and write-protecting disks while it runs.
 *
 * A drive is named by its controller and unit (H89_DRIVE_ID). The drives a
 * machine has are the ones its configuration fits (h89config.h); disks are
 * not part of the configuration. Every call takes the host's run lock, so
 * any thread may make them; all of them fail (-1, "not running") when the
 * machine is stopped.
 *
 * Image formats, by controller:
 *   H17           VirtualH89 raw tracks (.h17raw/.rawdisk/.tmpdisk), .h8d
 *                 sector dumps and .h17 (H17Disk) containers
 *   H37, MMS77316 ImageDisk (.imd), Teledisk (.td0, read-only), and
 *                 VirtualH89's sector (.mmsdisk/.logdisk) and raw images
 *   MMS77320      fixed media: unit files the controller creates itself
 * Every writable format is written back to its own file as it changes.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89DRIVES_H
#define H89DRIVES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    H89_CTRL_H17 = 0,
    H89_CTRL_H37,
    H89_CTRL_MMS77316,
    H89_CTRL_MMS77320,
    H89_CTRL_COUNT
} h89_ctrl;

#define H89_DRIVE_ID(ctrl, unit) ((ctrl) * 16 + (unit))
#define H89_DRIVE_CTRL(id)       ((id) / 16)
#define H89_DRIVE_UNIT(id)       ((id) % 16)

/* insert flags */
#define H89DRIVE_READ_ONLY 0x01   /* mount write-protected */

typedef struct {
    int id;                 /* H89_DRIVE_ID */
    int ctrl, unit;
    char label[64];         /* "H17 drive 1", "MMS 77316 drive 5 (5.25\")" */
    char type[48];          /* "H-17-1 (SS, 40 tracks)" */
    int removable;          /* 0: the MMS 77320's fixed disks */
    int loaded;
    char path[1024];        /* the image's file, "" when empty */
    char format[32];        /* "H8D", "H17D", "IMD", ... */
    int write_protected;    /* the tab (or a read-only file or format) */
    int read_only_format;   /* TD0: cannot be written at all */
    int dirty;              /* writes not yet in the file */
    int active;             /* selected with its motor on (an activity light) */
} h89_drive_info;

/* The drives the running machine has, in controller then unit order. */
int h89drive_count(void);
int h89drive_info_at(int index, h89_drive_info *out);
int h89drive_info(int id, h89_drive_info *out);

int h89drive_insert(int id, const char *path, int flags, char *err, int errsz);
int h89drive_eject(int id, char *err, int errsz);
int h89drive_set_write_protect(int id, int protect);

/* Writes every dirty image back now (Power Cycle, Quit). */
int h89drive_flush_all(void);

/* "h17:1=PATH", "h37:2=PATH:ro", "mms77316:5=PATH" -- the units are the
 * ones the drive labels show (1-based; the MMS 77320's are 0-based). */
int h89drive_insert_spec(const char *spec, char *err, int errsz);

const char *h89drive_ctrl_name(int ctrl);

#ifdef __cplusplus
}
#endif

#endif /* H89DRIVES_H */
