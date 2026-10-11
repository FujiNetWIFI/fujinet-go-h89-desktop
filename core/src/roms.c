/*
 * roms -- the imported H89 and H19 ROMs: importing them (MAME's zips as they
 * are, or loose images, recognised by size and CRC-32 against h89roms.h's
 * table), loading them for a power-up, and saying which the configuration
 * still lacks.
 *
 * Nothing here is shipped (COMPLIANCE.md); a WITH_H89_ROMS=ON developer
 * build writes the images it embedded into the ROM directory on first use.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "roms_embedded.h"
#include "session_internal.h"

static void rom_path(struct h89session *s, int index, char *dst, size_t dstsz)
{
    const h89_rom_info *r = h89_rom_at(index);
    snprintf(dst, dstsz, "%s/%s", s->roms_dir, r ? r->file : "-");
}

static int write_image(struct h89session *s, int index, const uint8_t *data, size_t size)
{
    char path[H89_PATH_MAX], tmp[H89_PATH_MAX + 8];
    FILE *f;

    rom_path(s, index, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f)
        return -1;
    if (fwrite(data, 1, size, f) != size) {
        fclose(f);
        remove(tmp);
        return -1;
    }
    fclose(f);
#if defined(_WIN32)
    remove(path);
#endif
    return rename(tmp, path);
}

uint8_t *roms_load(struct h89session *s, int index, size_t *size)
{
    const h89_rom_info *r = h89_rom_at(index);
    char path[H89_PATH_MAX];
    uint8_t *buf;
    FILE *f;
    size_t got;

    if (!r)
        return NULL;
    rom_path(s, index, path, sizeof path);
    f = fopen(path, "rb");
    if (!f)
        return NULL;
    buf = malloc(r->size + 1);
    got = buf ? fread(buf, 1, r->size + 1, f) : 0;
    fclose(f);
    /* the right size and the right CRC, or it is not the image it claims */
    if (got != r->size || h89_crc32(buf, got) != r->crc) {
        free(buf);
        return NULL;
    }
    *size = got;
    return buf;
}

int h89session_rom_available(h89session *s, int index)
{
    size_t size;
    uint8_t *b = roms_load(s, index, &size);
    free(b);
    return b != NULL;
}

void roms_provision_embedded(struct h89session *s)
{
    for (unsigned i = 0; i < h89_embedded_rom_count; i++) {
        int index = h89_rom_find_file(h89_embedded_roms[i].file);
        if (index < 0 || h89session_rom_available(s, index))
            continue;
        write_image(s, index, h89_embedded_roms[i].data, h89_embedded_roms[i].size);
    }
}

typedef struct {
    struct h89session *s;
    int imported, failed;
    char names[512];
} import_ctx;

static void on_found(void *user, int index, const uint8_t *data, size_t size)
{
    import_ctx *c = user;
    const h89_rom_info *r = h89_rom_at(index);
    if (write_image(c->s, index, data, size) != 0) {
        c->failed++;
        return;
    }
    c->imported++;
    if (strlen(c->names) + strlen(r->name) + 3 < sizeof c->names) {
        if (c->names[0])
            strcat(c->names, ", ");
        strcat(c->names, r->name);
    }
}

int h89session_import_roms(h89session *s, const char *path, char *msg, int msgsz)
{
    import_ctx c;
    char err[256];
    int n;

    memset(&c, 0, sizeof c);
    c.s = s;
    n = h89_rom_scan_file(path, on_found, &c, err, sizeof err);
    if (n < 0) {
        if (msg && msgsz > 0)
            snprintf(msg, (size_t)msgsz, "%s", err);
        return -1;
    }
    if (c.failed) {
        if (msg && msgsz > 0)
            snprintf(msg, (size_t)msgsz, "could not write to the ROM folder %s", s->roms_dir);
        return -1;
    }
    if (msg && msgsz > 0) {
        if (c.imported)
            snprintf(msg, (size_t)msgsz, "Imported %d ROM%s: %s", c.imported,
                     c.imported == 1 ? "" : "s", c.names);
        else
            snprintf(msg, (size_t)msgsz,
                     "No H89 or H19 ROM this app knows is in that file "
                     "(MAME's h89.zip and heath_tlb.zip hold them)");
    }
    return c.imported;
}

int h89session_missing_roms(h89session *s, char *what, int whatsz)
{
    h89_config cfg;
    int missing = 0, has_disk = 0;
    char buf[512] = "";

    h89session_get_config(s, &cfg);
    /* 444-19 is on the CPU board, and the monitors' disk boots call into
     * it -- MTR-90 boots the Z-89-37 through its routines -- so any floppy
     * controller needs it */
    for (int i = 0; i < H89_SLOTS; i++)
        has_disk |= cfg.slot[i] == H89_CARD_H17 || cfg.slot[i] == H89_CARD_H37 ||
                    cfg.slot[i] == H89_CARD_MMS77316;

#define NEED(file, label)                                                         \
    do {                                                                          \
        int ix_ = h89_rom_find_file(file);                                        \
        if (ix_ < 0 || !h89session_rom_available(s, ix_)) {                       \
            missing++;                                                            \
            if (strlen(buf) + strlen(label) + 3 < sizeof buf) {                   \
                if (buf[0]) strcat(buf, ", ");                                    \
                strcat(buf, label);                                               \
            }                                                                     \
        }                                                                         \
    } while (0)

    {
        int mon = h89_rom_find_file(cfg.monitor);
        const char *label = mon >= 0 ? h89_rom_at(mon)->name : "a monitor ROM";
        char l[96];
        snprintf(l, sizeof l, "the monitor %s", label);
        NEED(cfg.monitor, l);
    }
    if (has_disk)
        NEED("2716_444-19_h17.u520", "the H-17 ROM (444-19)");
    NEED("2716_444-29_h19font.u420", "the H19 character ROM (444-29)");
#undef NEED

    if (what && whatsz > 0)
        snprintf(what, (size_t)whatsz, "%s", buf);
    return missing;
}

int h89session_monitors(h89session *s, int *indexes, int max)
{
    int n = 0;
    for (int i = 0; i < h89_rom_count(); i++) {
        if (h89_rom_at(i)->kind != H89_ROM_MONITOR || !h89session_rom_available(s, i))
            continue;
        if (indexes && n < max)
            indexes[n] = i;
        n++;
    }
    return n;
}

const char *h89session_roms_path(const h89session *s)
{
    return s->roms_dir;
}
