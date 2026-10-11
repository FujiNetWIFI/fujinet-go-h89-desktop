/*
 * roms_embedded -- the table tools/roms/embed-roms.py generates into the
 * build tree: empty (the shipping configuration, WITH_H89_ROMS=OFF) or the
 * recognised images a developer put in tools/roms/ (NOT redistributable;
 * see COMPLIANCE.md).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_ROMS_EMBEDDED_H
#define H89_ROMS_EMBEDDED_H

#include <stdint.h>

typedef struct {
    const char *file;          /* MAME's file name: "2732_444-142_mtr90.u518" */
    const uint8_t *data;
    uint32_t size;
} h89_embedded_rom;

extern const h89_embedded_rom h89_embedded_roms[];
extern const unsigned h89_embedded_rom_count;

#endif /* H89_ROMS_EMBEDDED_H */
