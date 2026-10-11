/*
 * The core tests' little helpers: a CHECK that counts and reports, the ROM
 * directory (H89_TEST_ROM_DIR, else tools/roms), and loading the ROMs out
 * of whatever is in it (MAME's zips or loose images).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef H89_TEST_UTIL_H
#define H89_TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "h89roms.h"

#ifndef H89_SOURCE_DIR
#define H89_SOURCE_DIR "."
#endif

static int t_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            t_failures++;                                                        \
        }                                                                        \
    } while (0)

#define CHECK_EQ_INT(a, b)                                                      \
    do {                                                                        \
        long _a = (long)(a), _b = (long)(b);                                    \
        if (_a != _b) {                                                         \
            fprintf(stderr, "%s:%d: CHECK failed: %s == %s (%ld vs %ld)\n",     \
                    __FILE__, __LINE__, #a, #b, _a, _b);                        \
            t_failures++;                                                       \
        }                                                                       \
    } while (0)

static int t_done(const char *name)
{
    if (t_failures)
    {
        fprintf(stderr, "%s: %d check(s) failed\n", name, t_failures);
        return 1;
    }
    printf("%s: ok\n", name);
    return 0;
}

static const char *t_rom_dir(void)
{
    static char dir[1024];
    const char *e = getenv("H89_TEST_ROM_DIR");
    if (e && *e)
        return e;
    snprintf(dir, sizeof dir, "%s/tools/roms", H89_SOURCE_DIR);
    return dir;
}

typedef struct {
    unsigned char *data[64];
    size_t size[64];
} t_romset;

static void t_on_rom(void *user, int index, const uint8_t *data, size_t size)
{
    t_romset *s = (t_romset *)user;
    if (index < 0 || index >= 64 || s->data[index])
        return;
    s->data[index] = (unsigned char *)malloc(size);
    memcpy(s->data[index], data, size);
    s->size[index] = size;
}

/* Scans the ROM directory's zips and images. */
static void t_load_roms(t_romset *s)
{
    static const char *const names[] = {"h89.zip", "heath_tlb.zip",
        "2732_444-142_mtr90.u518", "2716_444-19_h17.u520", "2716_444-29_h19font.u420", NULL};
    char path[2048];
    memset(s, 0, sizeof *s);
    for (int i = 0; names[i]; i++)
    {
        snprintf(path, sizeof path, "%s/%s", t_rom_dir(), names[i]);
        h89_rom_scan_file(path, t_on_rom, s, NULL, 0);
    }
}

static const unsigned char *t_rom(t_romset *s, const char *file, size_t *size)
{
    int i = h89_rom_find_file(file);
    if (i < 0 || !s->data[i])
        return NULL;
    if (size)
        *size = s->size[i];
    return s->data[i];
}

#endif
