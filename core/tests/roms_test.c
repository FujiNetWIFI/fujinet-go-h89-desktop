/*
 * roms -- the ROM table and the zip reader (h89roms.h): a zip this test
 * builds itself (stored and deflated members, a comment, a directory), and
 * MAME's own zips when they are in tools/roms.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "h89roms.h"
#include "test_tmpdir.h"
#include "test_util.h"

static int s_entries;
static char s_names[8][64];

static void on_entry(void *user, const char *name, const uint8_t *data, size_t size, uint32_t crc)
{
    (void)user;
    if (s_entries < 8)
        snprintf(s_names[s_entries], sizeof s_names[0], "%s", name);
    s_entries++;
    CHECK(h89_crc32(data, size) == crc);
}

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); }

static int s_found[64];
static void on_rom(void *user, int index, const uint8_t *data, size_t size)
{
    (void)user; (void)data; (void)size;
    if (index >= 0 && index < 64)
        s_found[index]++;
}

int main(void)
{
    /* the CRC-32 everyone uses */
    CHECK(h89_crc32("123456789", 9) == 0xCBF43926u);
    CHECK(h89_rom_find(0x1000, 0xC4FF47C5) == h89_rom_find_file("2732_444-142_mtr90.u518"));
    CHECK(h89_rom_find(0x1000, 0x12345678) < 0);
    for (int i = 0; i < h89_rom_count(); i++)
        CHECK(h89_rom_at(i)->size == 0x800 || h89_rom_at(i)->size == 0x1000);

    /* a stored zip with two members, one a directory, and a comment */
    static uint8_t zip[1024];
    const char *body = "hello, zip";
    size_t n = 0, bl = strlen(body);
    uint32_t crc = h89_crc32(body, bl);
    /* local header */
    put32(zip + n, 0x04034b50); put16(zip + n + 4, 20); put16(zip + n + 6, 0);
    put16(zip + n + 8, 0); put32(zip + n + 14, crc); put32(zip + n + 18, (uint32_t)bl);
    put32(zip + n + 22, (uint32_t)bl); put16(zip + n + 26, 5); put16(zip + n + 28, 0);
    memcpy(zip + n + 30, "a.txt", 5);
    memcpy(zip + n + 35, body, bl);
    size_t local = n;
    n += 35 + bl;
    size_t cd = n;
    put32(zip + n, 0x02014b50); put16(zip + n + 10, 0); put32(zip + n + 16, crc);
    put32(zip + n + 20, (uint32_t)bl); put32(zip + n + 24, (uint32_t)bl);
    put16(zip + n + 28, 5); put16(zip + n + 30, 0); put16(zip + n + 32, 0);
    put32(zip + n + 42, (uint32_t)local);
    memcpy(zip + n + 46, "a.txt", 5);
    n += 51;
    put32(zip + n, 0x02014b50); put16(zip + n + 28, 4); put32(zip + n + 42, 0);
    memcpy(zip + n + 46, "dir/", 4);
    n += 50;
    size_t cdlen = n - cd;
    put32(zip + n, 0x06054b50); put16(zip + n + 8, 2); put16(zip + n + 10, 2);
    put32(zip + n + 12, (uint32_t)cdlen); put32(zip + n + 16, (uint32_t)cd);
    put16(zip + n + 20, 7);
    memcpy(zip + n + 22, "comment", 7);
    n += 29;
    CHECK(h89_zip_each(zip, n, on_entry, NULL) == 1);
    CHECK(s_entries == 1 && strcmp(s_names[0], "a.txt") == 0);
    CHECK(h89_zip_each(zip, 10, on_entry, NULL) < 0);

    /* MAME's zips, deflated */
    char path[2048];
    snprintf(path, sizeof path, "%s/h89.zip", t_rom_dir());
    int k = h89_rom_scan_file(path, on_rom, NULL, NULL, 0);
    if (k < 0)
    {
        printf("  (no h89.zip in %s; MAME's zips not checked)\n", t_rom_dir());
    }
    else
    {
        CHECK(k >= 11);
        CHECK(s_found[h89_rom_find_file("2716_444-19_h17.u520")] == 1);
        CHECK(s_found[h89_rom_find_file("2716_444-40_mtr88.u518")] == 1);   /* h88/ subfolder */
        snprintf(path, sizeof path, "%s/heath_tlb.zip", t_rom_dir());
        CHECK(h89_rom_scan_file(path, on_rom, NULL, NULL, 0) == 1);
        CHECK(s_found[h89_rom_find_file("2716_444-29_h19font.u420")] == 1);
    }
    return t_done("roms");
}
