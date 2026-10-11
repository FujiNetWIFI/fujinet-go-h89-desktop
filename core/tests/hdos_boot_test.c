/*
 * hdos_boot -- HDOS 2.0 boots from its distribution disk to its date
 * prompt: MTR-90's B command, the H17 ROM reading track 0, HDOS's console
 * baud-rate detection (it wants a SPACE, and tries 2400 baud before 9600),
 * then BOOT at its ACTION? prompt.
 *
 * Needs the ROMs and tools/disks/images/HDOS_2_0_Operating_System.h17disk
 * (tools/disks/fetch-disks.sh); 77 without either.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "h89drives.h"
#include "h89keys.h"
#include "host.h"
#include "test_util.h"

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep(ms); }
#else
#include <time.h>
#include <unistd.h>
static void sleep_ms(int ms)
{
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
#endif

static char s_screen[8192];

static int wait_for(const char *text, int ms)
{
    for (int t = 0; t < ms; t += 20)
    {
        h89host_screen_text(s_screen, sizeof s_screen, 0);
        if (strstr(s_screen, text))
            return 1;
        sleep_ms(20);
    }
    fprintf(stderr, "hdos_boot: no \"%s\" on the screen:\n%s\n", text, s_screen);
    return 0;
}

int main(void)
{
    t_romset rs;
    h89host_opts o;
    char disk[2048], err[256];
    FILE *f;

    snprintf(disk, sizeof disk, "%s/tools/disks/images/HDOS_2_0_Operating_System.h17disk",
             H89_SOURCE_DIR);
    t_load_roms(&rs);
    memset(&o, 0, sizeof o);
    h89_config_default(&o.config);
    o.monitor = t_rom(&rs, o.config.monitor, &o.monitor_size);
    o.h17_rom = t_rom(&rs, "2716_444-19_h17.u520", &o.h17_size);
    o.font = t_rom(&rs, "2716_444-29_h19font.u420", &o.font_size);
    if (!o.monitor || !o.h17_rom || !o.font || !(f = fopen(disk, "rb")))
    {
        printf("hdos_boot: SKIP: needs the H89 ROMs and %s\n", disk);
        return 77;
    }
    fclose(f);

    CHECK(h89host_start(&o) == 0);
    h89host_set_warp(1);
    CHECK(h89drive_insert(H89_DRIVE_ID(H89_CTRL_H17, 0), disk, H89DRIVE_READ_ONLY, err, sizeof err) == 0);
    CHECK(wait_for("H:", 5000));
    h89host_paste("B\r");
    sleep_ms(1500);
    /* the baud-rate detection: SPACE until it takes */
    int ok = 0;
    for (int i = 0; i < 6 && !ok; i++)
    {
        h89host_key(' ', 0);
        sleep_ms(400);
        h89host_screen_text(s_screen, sizeof s_screen, 0);
        ok = strstr(s_screen, "ACTION?") != NULL;
    }
    CHECK(wait_for("ACTION?", 5000));
    h89host_paste("\r");
    CHECK(wait_for("HDOS VERSION 2.0", 20000));
    CHECK(wait_for("DATE (DD-MMM-YY)?", 20000));

    h89_drive_info info;
    CHECK(h89drive_info(H89_DRIVE_ID(H89_CTRL_H17, 0), &info) == 0);
    CHECK(info.loaded && info.write_protected && !info.dirty);
    CHECK(strcmp(info.format, "H17D (draft)") == 0);

    h89host_stop();
    return t_done("hdos_boot");
}
