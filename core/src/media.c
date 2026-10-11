/*
 * media -- what a file is (by name, size and contents) and where it goes:
 * the ROM import, a drive that takes it, or FujiNet's SD folder.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "h89drives.h"
#include "session_internal.h"

static const char *ext_of(const char *path, char *buf, size_t bufsz)
{
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *bs = strrchr(path, '\\');
    if (bs && (!slash || bs > slash))
        slash = bs;
#endif
    const char *dot = strrchr(slash ? slash : path, '.');
    size_t i = 0;
    buf[0] = '\0';
    if (!dot)
        return buf;
    for (dot++; *dot && i + 1 < bufsz; dot++)
        buf[i++] = (char)tolower((unsigned char)*dot);
    buf[i] = '\0';
    return buf;
}

static long file_size(FILE *f)
{
    long n;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    return n;
}

static int count_roms_cb_n;
static void count_roms_cb(void *user, int index, const uint8_t *data, size_t size)
{
    (void)user; (void)index; (void)data; (void)size;
    count_roms_cb_n++;
}

/* An ImageDisk's first track's mode: 0/3 are 500 kbps (8"), the rest 5.25". */
static int imd_is_8inch(FILE *f)
{
    int c;
    /* the header comment runs to 0x1A */
    while ((c = fgetc(f)) != EOF && c != 0x1a)
        ;
    c = fgetc(f);
    return c == 0 || c == 3;
}

int h89session_media_kind(const char *path)
{
    char ext[16];
    unsigned char head[8];
    FILE *f;
    long size;
    int kind = H89_MEDIA_UNKNOWN;
    int inches = 5;

    if (!path || !*path)
        return H89_MEDIA_UNKNOWN;
    ext_of(path, ext, sizeof ext);
    f = fopen(path, "rb");
    if (!f)
        return H89_MEDIA_UNKNOWN;
    size = file_size(f);
    memset(head, 0, sizeof head);
    if (fread(head, 1, sizeof head, f) < 4) {
        fclose(f);
        return H89_MEDIA_UNKNOWN;
    }
    fseek(f, 0, SEEK_SET);

    if (!memcmp(head, "H17D", 4)) {
        kind = H89_MEDIA_H17;
    } else if (!memcmp(head, "IMD ", 4)) {
        kind = imd_is_8inch(f) ? H89_MEDIA_SOFT_8 : H89_MEDIA_SOFT_5;
    } else if ((head[0] == 'T' && head[1] == 'D') || (head[0] == 't' && head[1] == 'd')) {
        /* Teledisk: byte 5 is the data rate, 2 = 500 kbps */
        kind = (head[5] & 0x03) == 2 ? H89_MEDIA_SOFT_8 : H89_MEDIA_SOFT_5;
    } else if (head[0] == 'P' && head[1] == 'K' && head[2] == 3 && head[3] == 4) {
        fclose(f);
        count_roms_cb_n = 0;
        h89_rom_scan_file(path, count_roms_cb, NULL, NULL, 0);
        return count_roms_cb_n ? H89_MEDIA_ROMS : H89_MEDIA_UNKNOWN;
    } else if (h89image_soft_probe(path, &inches) >= 2) {
        /* VirtualH89's sector and raw floppy images (a raw 5.25" FM image
         * is as long as an H17 raw one: its marks tell them apart) */
        kind = inches == 8 ? H89_MEDIA_SOFT_8 : H89_MEDIA_SOFT_5;
    } else if (size == 102400 || size == 204800 || size == 409600 ||
               size == 128000 || size == 256000 || size == 512000 ||
               !strcmp(ext, "h8d") || !strcmp(ext, "h17raw") ||
               !strcmp(ext, "rawdisk") || !strcmp(ext, "tmpdisk")) {
        kind = H89_MEDIA_H17;
    } else if (size == 2048 || size == 4096) {
        fclose(f);
        count_roms_cb_n = 0;
        h89_rom_scan_file(path, count_roms_cb, NULL, NULL, 0);
        return count_roms_cb_n ? H89_MEDIA_ROMS : H89_MEDIA_UNKNOWN;
    } else if (!strcmp(ext, "mmsdisk") || !strcmp(ext, "logdisk") || !strcmp(ext, "rawfloppy")) {
        kind = H89_MEDIA_SOFT_5;
    }
    fclose(f);
    return kind;
}

/* Is the drive of this configuration able to take this kind? */
static int drive_takes(const h89_config *cfg, int id, int kind)
{
    int ctrl = H89_DRIVE_CTRL(id), unit = H89_DRIVE_UNIT(id);
    int fdd;

    if (kind == H89_MEDIA_H17)
        return ctrl == H89_CTRL_H17;
    if (ctrl == H89_CTRL_H37)
        fdd = cfg->h37_drive[unit];
    else if (ctrl == H89_CTRL_MMS77316)
        fdd = cfg->mms316_drive[unit];
    else
        return 0;
    if (kind == H89_MEDIA_SOFT_8)
        return fdd == H89_FDD_8_SS || fdd == H89_FDD_8_DS;
    if (kind == H89_MEDIA_SOFT_5)
        return fdd >= H89_FDD_5_SS_ST && fdd <= H89_FDD_5_DS_DT;
    return 0;
}

int h89session_drives_for_image(h89session *s, const char *path, int *ids, int max)
{
    int kind = h89session_media_kind(path);
    int n = 0, count = h89session_drive_count(s);
    h89_config cfg;

    if (kind == H89_MEDIA_UNKNOWN || kind == H89_MEDIA_ROMS)
        return 0;
    h89session_running_config(s, &cfg);
    /* empty drives first, then the loaded ones */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < count; i++) {
            h89_drive_info d;
            if (h89session_drive_info_at(s, i, &d) != 0 || !d.removable)
                continue;
            if (!drive_takes(&cfg, d.id, kind) || (pass == 0) == (d.loaded != 0))
                continue;
            if (ids && n < max)
                ids[n] = d.id;
            n++;
        }
    }
    return n;
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *bs = strrchr(path, '\\');
    if (bs && (!slash || bs > slash))
        slash = bs;
#endif
    return slash ? slash + 1 : path;
}

int h89session_import_media(h89session *s, const char *path, char *msg, int msgsz,
                             int *ids, int maxids, int *nids)
{
    int kind = h89session_media_kind(path);
    char err[256];

    if (nids)
        *nids = 0;
    if (kind == H89_MEDIA_ROMS)
        return h89session_import_roms(s, path, msg, msgsz) > 0 ? 0 : -1;

    if (kind != H89_MEDIA_UNKNOWN) {
        int cand[32];
        int n = h89session_drives_for_image(s, path, cand, 32);
        if (n == 0) {
            snprintf(msg, (size_t)msgsz,
                     "This machine has no drive for %s; fit the controller in "
                     "Preferences > Slots", base_name(path));
            return -1;
        }
        h89_drive_info d;
        if (h89session_drive_info(s, cand[0], &d) == 0 && !d.loaded) {
            if (h89session_drive_insert(s, cand[0], path, 0, err, sizeof err) != 0) {
                snprintf(msg, (size_t)msgsz, "%s", err);
                return -1;
            }
            snprintf(msg, (size_t)msgsz, "%s is in %s", base_name(path), d.label);
            return 0;
        }
        /* every drive that takes it is full: the frontend asks which */
        if (ids && nids) {
            int k = n < maxids ? n : maxids;
            memcpy(ids, cand, (size_t)k * sizeof ids[0]);
            *nids = k;
        }
        snprintf(msg, (size_t)msgsz, "Which drive should %s go in?", base_name(path));
        return 1;
    }

    /* anything else: FujiNet's SD folder, where its web UI can mount it */
    {
        char dst[H89_PATH_MAX];
        paths_mkdir_p(s->fujinet_sd);
        snprintf(dst, sizeof dst, "%s/%s", s->fujinet_sd, base_name(path));
        if (paths_copy_file(path, dst) != 0) {
            snprintf(msg, (size_t)msgsz, "cannot copy %s to FujiNet's SD folder", base_name(path));
            return -1;
        }
        snprintf(msg, (size_t)msgsz, "Copied %s to FujiNet's SD folder", base_name(path));
        return 0;
    }
}
