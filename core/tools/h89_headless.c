/*
 * h89_headless -- the H89 with no window: boots a machine from ROMs found in
 * a directory (loose images or MAME's zips), then works through a script of
 * steps given on the command line, in order:
 *
 *   --until TEXT       wait (in real time, up to --timeout) until the H19's
 *                      screen shows TEXT; fails the run if it does not
 *   --type TEXT        type TEXT on the H19's keyboard (\r, \n, \e, \\ and
 *                      \xHH escapes; newlines are RETURN)
 *   --key NAME         press a special key: f1..f5, erase, blue, red, gray,
 *                      up, down, left, right, home, break, reset
 *   --wait MS          let the machine run MS milliseconds
 *   --screen           print the screen
 *   --ppm FILE         write the H19's picture
 *   --disk SPEC        insert a disk: h17:1=PATH, h37:2=PATH[:ro], ...
 *
 * Machine options (before the steps): --roms DIR (default $H89_TEST_ROM_DIR,
 * then tools/roms), --monitor FILE, --boip HOST:PORT, --warp,
 * --timeout SECONDS (per --until; default 20), --set KEY=VALUE (the
 * session's config keys, e.g. slot.p505=h37).
 *
 * Exit status: 0 when every step passed, 1 on a failed step, 2 on a usage
 * error, 77 when the ROMs it needs are not there (ctest's "skipped").
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "h89config.h"
#include "h89drives.h"
#include "h89keys.h"
#include "h89roms.h"
#include "host.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#define DIRSEP '\\'
#else
#include <dirent.h>
#include <unistd.h>
#define DIRSEP '/'
#endif

#ifndef H89_SOURCE_DIR
#define H89_SOURCE_DIR "."
#endif

typedef struct {
    uint8_t *data;
    size_t size;
} rom_image;

static rom_image s_found[64];

static void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

static void on_rom(void *user, int index, const uint8_t *data, size_t size)
{
    (void)user;
    if (index < 0 || index >= 64 || s_found[index].data)
        return;
    s_found[index].data = malloc(size);
    memcpy(s_found[index].data, data, size);
    s_found[index].size = size;
}

static int load_rom_dir(const char *dir)
{
    char path[2048];
    int n = 0;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(path, sizeof path, "%s\\*", dir);
    h = FindFirstFileA(path, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        snprintf(path, sizeof path, "%s\\%s", dir, fd.cFileName);
        int k = h89_rom_scan_file(path, on_rom, NULL, NULL, 0);
        if (k > 0)
            n += k;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d)
        return 0;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        int k = h89_rom_scan_file(path, on_rom, NULL, NULL, 0);
        if (k > 0)
            n += k;
    }
    closedir(d);
#endif
    return n;
}

static const rom_image *rom_by_file(const char *file)
{
    int i = h89_rom_find_file(file);
    return (i >= 0 && s_found[i].data) ? &s_found[i] : NULL;
}

static void unescape(const char *in, char *out, size_t outsz)
{
    size_t o = 0;
    for (; *in && o + 1 < outsz; in++)
    {
        if (*in != '\\' || !in[1])
        {
            out[o++] = *in;
            continue;
        }
        in++;
        switch (*in)
        {
        case 'r': out[o++] = '\r'; break;
        case 'n': out[o++] = '\r'; break;
        case 'e': out[o++] = 27; break;
        case 'x':
            if (isxdigit((unsigned char)in[1]) && isxdigit((unsigned char)in[2]))
            {
                char hex[3] = {in[1], in[2], 0};
                out[o++] = (char)strtol(hex, NULL, 16);
                in += 2;
            }
            break;
        default: out[o++] = *in; break;
        }
    }
    out[o] = 0;
}

static int screen_has(const char *text)
{
    static char buf[16384];
    h89host_screen_text(buf, sizeof buf, 1);
    return strstr(buf, text) != NULL;
}

static void print_screen(void)
{
    static char buf[16384];
    h89host_screen_text(buf, sizeof buf, 1);
    printf("---- screen ----\n%s----------------\n", buf);
}

static int write_ppm(const char *path)
{
    static uint32_t fb[H89HOST_FB_WIDTH * H89HOST_FB_HEIGHT];
    uint64_t serial = 0;
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    h89host_frame_copy(fb, &serial);
    fprintf(f, "P6\n%d %d\n255\n", H89HOST_FB_WIDTH, H89HOST_FB_HEIGHT);
    for (size_t i = 0; i < sizeof fb / sizeof fb[0]; i++)
    {
        unsigned char c[3] = {(unsigned char)(fb[i] >> 16), (unsigned char)(fb[i] >> 8), (unsigned char)fb[i]};
        fwrite(c, 1, 3, f);
    }
    fclose(f);
    return 0;
}

static int key_by_name(const char *n)
{
    static const struct { const char *name; int code; } keys[] = {
        {"f1", H89K_F1}, {"f2", H89K_F2}, {"f3", H89K_F3}, {"f4", H89K_F4}, {"f5", H89K_F5},
        {"erase", H89K_ERASE}, {"blue", H89K_BLUE}, {"red", H89K_RED}, {"gray", H89K_GRAY},
        {"up", H89K_UP}, {"down", H89K_DOWN}, {"left", H89K_LEFT}, {"right", H89K_RIGHT},
        {"home", H89K_HOME}, {"break", H89K_BREAK}, {"reset", H89K_RESET},
        {"offline", H89K_OFFLINE}, {"linefeed", H89K_LINEFEED},
    };
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (strcmp(n, keys[i].name) == 0)
            return keys[i].code;
    return -1;
}

static int set_option(h89_config *c, const char *kv)
{
    char key[128];
    const char *eq = strchr(kv, '=');
    if (!eq || (size_t)(eq - kv) >= sizeof key)
        return -1;
    snprintf(key, sizeof key, "%.*s", (int)(eq - kv), kv);
    return h89_config_set(c, key, eq + 1);
}

static void usage(void)
{
    fprintf(stderr,
            "usage: h89_headless [--roms DIR] [--monitor FILE] [--boip HOST:PORT] [--warp]\n"
            "                    [--timeout S] [--set KEY=VALUE]...\n"
            "                    [--until TEXT | --type TEXT | --key NAME | --wait MS |\n"
            "                     --screen | --ppm FILE | --disk SPEC]...\n");
}

int main(int argc, char **argv)
{
    const char *romdir = getenv("H89_TEST_ROM_DIR");
    char default_romdir[1024];
    h89_config cfg;
    char boip_host[128] = "127.0.0.1";
    int boip_port = 0, warp = 0, timeout_s = 20, first_step = argc;
    h89host_opts opts;
    char err[256];

    h89_config_default(&cfg);
    snprintf(default_romdir, sizeof default_romdir, "%s%ctools%croms", H89_SOURCE_DIR, DIRSEP, DIRSEP);

    for (int i = 1; i < argc; i++)
    {
        const char *a = argv[i];
        int has = i + 1 < argc;
        if (!strcmp(a, "--roms") && has)
            romdir = argv[++i];
        else if (!strcmp(a, "--monitor") && has)
            snprintf(cfg.monitor, sizeof cfg.monitor, "%s", argv[++i]);
        else if (!strcmp(a, "--boip") && has)
        {
            const char *hp = argv[++i], *colon = strrchr(hp, ':');
            if (!colon)
            {
                usage();
                return 2;
            }
            snprintf(boip_host, sizeof boip_host, "%.*s", (int)(colon - hp), hp);
            boip_port = atoi(colon + 1);
        }
        else if (!strcmp(a, "--warp"))
            warp = 1;
        else if (!strcmp(a, "--timeout") && has)
            timeout_s = atoi(argv[++i]);
        else if (!strcmp(a, "--set") && has)
        {
            if (set_option(&cfg, argv[++i]) != 0)
            {
                fprintf(stderr, "h89_headless: unknown option %s\n", argv[i]);
                return 2;
            }
        }
        else if (!strncmp(a, "--", 2))
        {
            first_step = i;
            break;
        }
        else
        {
            usage();
            return 2;
        }
    }

    if (!romdir || !*romdir)
        romdir = default_romdir;
    load_rom_dir(romdir);

    const rom_image *mon = rom_by_file(cfg.monitor);
    const rom_image *h17 = rom_by_file("2716_444-19_h17.u520");
    const rom_image *font = rom_by_file("2716_444-29_h19font.u420");
    if (!mon || !font)
    {
        printf("h89_headless: SKIP: the monitor %s and the H19 font are not in %s\n", cfg.monitor, romdir);
        return 77;
    }

    memset(&opts, 0, sizeof opts);
    opts.config = cfg;
    opts.monitor = mon->data;
    opts.monitor_size = mon->size;
    opts.h17_rom = h17 ? h17->data : NULL;
    opts.h17_size = h17 ? h17->size : 0;
    opts.font = font->data;
    opts.font_size = font->size;
    opts.boip_host = boip_host;
    opts.boip_port = boip_port;
    if (h89host_start(&opts) != 0)
    {
        fprintf(stderr, "h89_headless: %s\n", h89host_last_error());
        return 1;
    }
    h89host_set_warp(warp);

    int rc = 0;
    for (int i = first_step; i < argc && rc == 0; i++)
    {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        char text[4096];

        if (!strcmp(a, "--until") && v)
        {
            int waited = 0;
            i++;
            while (!screen_has(v) && waited < timeout_s * 1000)
            {
                sleep_ms(20);
                waited += 20;
            }
            if (!screen_has(v))
            {
                fprintf(stderr, "h89_headless: timed out waiting for \"%s\"\n", v);
                print_screen();
                rc = 1;
            }
        }
        else if (!strcmp(a, "--type") && v)
        {
            i++;
            unescape(v, text, sizeof text);
            h89host_paste(text);
            while (h89host_paste_pending())
                sleep_ms(10);
        }
        else if (!strcmp(a, "--key") && v)
        {
            int k = key_by_name(argv[++i]);
            if (k < 0)
            {
                fprintf(stderr, "h89_headless: unknown key %s\n", v);
                rc = 2;
            }
            else
                h89host_key((unsigned)k, k == H89K_RESET);
        }
        else if (!strcmp(a, "--wait") && v)
        {
            i++;
            sleep_ms(atoi(v));
        }
        else if (!strcmp(a, "--screen"))
            print_screen();
        else if (!strcmp(a, "--ppm") && v)
        {
            i++;
            if (write_ppm(v) != 0)
            {
                fprintf(stderr, "h89_headless: cannot write %s\n", v);
                rc = 1;
            }
        }
        else if (!strcmp(a, "--disk") && v)
        {
            i++;
            if (h89drive_insert_spec(v, err, sizeof err) != 0)
            {
                fprintf(stderr, "h89_headless: %s: %s\n", v, err);
                rc = 1;
            }
        }
        else
        {
            usage();
            rc = 2;
        }
    }

    h89host_stop();
    return rc;
}
