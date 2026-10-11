/*
 * h89roms -- the H89 and H19 ROM images this app knows, by size and CRC-32
 * (MAME's h89.zip and heath_tlb.zip), and the means to find them in what the
 * user hands over: loose images, or MAME's zips as they are.
 *
 * None of these images is shipped (COMPLIANCE.md); the session imports them
 * into its ROM directory under MAME's file names (core/src/roms.c).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89ROMS_H
#define H89ROMS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    H89_ROM_MONITOR = 0,    /* at 0x0000, 2K or 4K */
    H89_ROM_H17,            /* the H-88-1's ROM at 0x1800 */
    H89_ROM_FONT,           /* the H19's character generator */
} h89_rom_kind;

typedef struct {
    const char *file;       /* MAME's file name, also the stored name */
    const char *name;       /* short: "MTR-90 (444-142)" */
    const char *desc;       /* long: "Zenith Data Systems MTR-90 (444-142)" */
    uint32_t size, crc;
    int kind;               /* h89_rom_kind */
    const char *needs;      /* hardware it expects that is not emulated, or NULL */
} h89_rom_info;

int h89_rom_count(void);
const h89_rom_info *h89_rom_at(int i);
/* index by file name / by size and CRC; -1 when unknown */
int h89_rom_find_file(const char *file);
int h89_rom_find(uint32_t size, uint32_t crc);

uint32_t h89_crc32(const void *data, size_t size);

/* Calls `found` for every known image in the file at `path` -- the file
 * itself, or every member of a zip. Returns how many were found, or -1 with
 * the reason in err (unreadable, a damaged zip). The data passed to `found`
 * is only valid during the call. */
typedef void (*h89_rom_found_fn)(void *user, int index, const uint8_t *data, size_t size);
int h89_rom_scan_file(const char *path, h89_rom_found_fn found, void *user,
                      char *err, int errsz);

/* Every entry of a zip, inflated (stored and deflate; no zip64, no
 * encryption). Returns the entry count or -1. Shared with the tests. */
typedef void (*h89_zip_entry_fn)(void *user, const char *name, const uint8_t *data,
                                 size_t size, uint32_t crc);
int h89_zip_each(const uint8_t *zip, size_t size, h89_zip_entry_fn fn, void *user);

#ifdef __cplusplus
}
#endif

#endif /* H89ROMS_H */
