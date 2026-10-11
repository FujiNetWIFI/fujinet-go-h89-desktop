/*
 * romset -- see h89roms.h.
 *
 * The zip reader walks the central directory and inflates with Mark Adler's
 * puff (core/third_party/puff, zlib licence): small, and plenty for a few
 * kilobytes of ROM.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "h89roms.h"

#include "puff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* MAME's h89.zip and heath_tlb.zip (src/mame/heathzenith/h89.cpp,
 * src/devices/bus/heathzenith/h19/tlb.cpp). SigmaROM v1.2 is byte-identical
 * to MTR-90 444-142 and is not listed separately. The CDR monitors need
 * CDR's own disk controller, which is not emulated, and are left out. */
static const h89_rom_info s_roms[] = {
    {"2732_444-142_mtr90.u518", "MTR-90 (444-142)", "Zenith Data Systems MTR-90 (444-142)",
     0x1000, 0xC4FF47C5, H89_ROM_MONITOR, NULL},
    {"2732_444-84_mtr90.u518", "MTR-90 (444-84)", "Zenith Data Systems MTR-90 (444-84, superseded by 444-142)",
     0x1000, 0xF10FCA03, H89_ROM_MONITOR, NULL},
    {"2716_444-62_mtr89.u518", "MTR-89 (444-62)", "Heath MTR-89 (444-62)",
     0x0800, 0x8F507972, H89_ROM_MONITOR, NULL},
    {"2716_444-40_mtr88.u518", "MTR-88 (444-40)", "Heath MTR-88 (444-40), for the H88",
     0x0800, 0x093AFB79, H89_ROM_MONITOR, NULL},
    {"2732_444_84b_mms.u518", "MMS 444-84B", "Magnolia MicroSystems 444-84B",
     0x1000, 0x7E75D6F4, H89_ROM_MONITOR, NULL},
    {"2732_444_84a_mms.u518", "MMS 444-84A", "Magnolia MicroSystems 444-84A (superseded by 444-84B)",
     0x1000, 0x0E541A7E, H89_ROM_MONITOR, NULL},
    {"2732_kmr100_v3_a_02.u518", "Kres KMR-100", "Kres KMR-100 V3.a.02",
     0x1000, 0xFD491592, H89_ROM_MONITOR, NULL},
    {"2732_mtrhex_4k.u518", "Ultimeth 4K", "Ultimeth MTRHEX 4K ROM",
     0x1000, 0xE26B29A9, H89_ROM_MONITOR, NULL},
    {"2716_mtrhex.u518", "Ultimeth 2K", "Ultimeth MTRHEX 2K ROM",
     0x0800, 0x842A306A, H89_ROM_MONITOR, NULL},
    {"2732_ets_4k.u518", "ETS 4K", "Extended Technology Systems (ETS) 4K ROM, based on Ultimeth",
     0x1000, 0xED8BDBE9, H89_ROM_MONITOR, NULL},
    {"2732_sigma_rom_v_1.3.bin", "SigmaROM 1.3", "SigmaSoft SigmaROM v1.3",
     0x1000, 0xC5C6B799, H89_ROM_MONITOR, NULL},
    {"2716_444-19_h17.u520", "H-17 ROM (444-19)", "Heath H-88-1 hard-sectored controller ROM (444-19)",
     0x0800, 0x26E80AE3, H89_ROM_H17, NULL},
    {"2716_444-29_h19font.u420", "H19 font (444-29)", "Heath H19 character generator (444-29)",
     0x0800, 0xD595AC1D, H89_ROM_FONT, NULL},
};

int h89_rom_count(void)
{
    return COUNT(s_roms);
}

const h89_rom_info *h89_rom_at(int i)
{
    return (i >= 0 && i < COUNT(s_roms)) ? &s_roms[i] : NULL;
}

int h89_rom_find_file(const char *file)
{
    for (int i = 0; file && i < COUNT(s_roms); i++)
        if (strcmp(s_roms[i].file, file) == 0)
            return i;
    return -1;
}

int h89_rom_find(uint32_t size, uint32_t crc)
{
    for (int i = 0; i < COUNT(s_roms); i++)
        if (s_roms[i].size == size && s_roms[i].crc == crc)
            return i;
    return -1;
}

uint32_t h89_crc32(const void *data, size_t size)
{
    static uint32_t table[256];
    static int ready;
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;

    if (!ready)
    {
        for (uint32_t i = 0; i < 256; i++)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = 1;
    }
    for (size_t i = 0; i < size; i++)
        crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* ---- zip ------------------------------------------------------------------ */

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | (le16(p + 2) << 16); }

int h89_zip_each(const uint8_t *zip, size_t size, h89_zip_entry_fn fn, void *user)
{
    size_t eocd = (size_t)-1;
    int count = 0;

    if (size < 22)
        return -1;
    /* the end-of-central-directory record, behind an optional comment */
    for (size_t i = size - 22;; i--)
    {
        if (le32(zip + i) == 0x06054b50)
        {
            eocd = i;
            break;
        }
        if (i == 0 || size - i > 22 + 65535)
            break;
    }
    if (eocd == (size_t)-1)
        return -1;

    uint32_t entries = le16(zip + eocd + 10);
    uint32_t cd_size = le32(zip + eocd + 12);
    uint32_t cd_off = le32(zip + eocd + 16);
    if ((size_t)cd_off + cd_size > size)
        return -1;

    size_t p = cd_off;
    for (uint32_t e = 0; e < entries; e++)
    {
        if (p + 46 > size || le32(zip + p) != 0x02014b50)
            return -1;
        uint32_t flags = le16(zip + p + 8);
        uint32_t method = le16(zip + p + 10);
        uint32_t crc = le32(zip + p + 16);
        uint32_t csize = le32(zip + p + 20);
        uint32_t usize = le32(zip + p + 24);
        uint32_t nlen = le16(zip + p + 28);
        uint32_t xlen = le16(zip + p + 30);
        uint32_t clen = le16(zip + p + 32);
        uint32_t loff = le32(zip + p + 42);
        char name[256];

        if (p + 46 + nlen > size)
            return -1;
        snprintf(name, sizeof name, "%.*s", (int)nlen, (const char *)zip + p + 46);
        p += 46 + nlen + xlen + clen;

        if (nlen && name[nlen - 1] == '/')
            continue;                      /* a directory */
        if ((flags & 1) || usize > 16u * 1024 * 1024)
            continue;                      /* encrypted, or not a ROM anyway */

        if ((size_t)loff + 30 > size || le32(zip + loff) != 0x04034b50)
            return -1;
        size_t data = loff + 30 + le16(zip + loff + 26) + le16(zip + loff + 28);
        if (data + csize > size)
            return -1;

        uint8_t *out = malloc(usize ? usize : 1);
        if (!out)
            return -1;
        int ok = 0;
        if (method == 0 && csize == usize)
        {
            memcpy(out, zip + data, usize);
            ok = 1;
        }
        else if (method == 8)
        {
            unsigned long dlen = usize, slen = csize;
            ok = puff(out, &dlen, zip + data, &slen) == 0 && dlen == usize;
        }
        if (ok && h89_crc32(out, usize) == crc)
        {
            fn(user, name, out, usize, crc);
            count++;
        }
        free(out);
    }
    return count;
}

/* ---- scanning a file ---------------------------------------------------- */

typedef struct {
    h89_rom_found_fn found;
    void *user;
    int n;
} scan_ctx;

static void scan_entry(void *user, const char *name, const uint8_t *data, size_t size, uint32_t crc)
{
    scan_ctx *ctx = user;
    int i = h89_rom_find((uint32_t)size, crc);
    (void)name;
    if (i >= 0)
    {
        ctx->found(ctx->user, i, data, size);
        ctx->n++;
    }
}

int h89_rom_scan_file(const char *path, h89_rom_found_fn found, void *user, char *err, int errsz)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long size;
    scan_ctx ctx = {found, user, 0};

    if (!f)
    {
        if (err)
            snprintf(err, errsz, "cannot open %s", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 64L * 1024 * 1024)
    {
        fclose(f);
        if (err)
            snprintf(err, errsz, "%s is empty or too large to be a ROM set", path);
        return -1;
    }
    buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size)
    {
        free(buf);
        fclose(f);
        if (err)
            snprintf(err, errsz, "cannot read %s", path);
        return -1;
    }
    fclose(f);

    if (size >= 4 && le32(buf) == 0x04034b50)
    {
        if (h89_zip_each(buf, (size_t)size, scan_entry, &ctx) < 0)
        {
            free(buf);
            if (err)
                snprintf(err, errsz, "%s is not a zip this app can read", path);
            return -1;
        }
    }
    else
    {
        scan_entry(&ctx, path, buf, (size_t)size, h89_crc32(buf, (size_t)size));
    }
    free(buf);
    return ctx.n;
}
