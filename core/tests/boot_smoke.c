/*
 * boot_smoke -- the paced host with the real MTR-90: the monitor's "H:"
 * prompt appears, and the 2 ms slices run at real time (500 a second).
 * Needs the ROMs (77 without).
 *
 * The rate is printed always and asserted only where the host's sleeps are
 * accurate enough to judge (Linux): a shared CI runner under Windows or
 * macOS can be slow for reasons of its own.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "host.h"
#include "test_util.h"

#include <time.h>
#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep(ms); }
#else
static void sleep_ms(int ms)
{
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
#endif

static double now_s(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(void)
{
    t_romset rs;
    h89host_opts o;
    char screen[8192];

    t_load_roms(&rs);
    memset(&o, 0, sizeof o);
    h89_config_default(&o.config);
    o.monitor = t_rom(&rs, o.config.monitor, &o.monitor_size);
    o.h17_rom = t_rom(&rs, "2716_444-19_h17.u520", &o.h17_size);
    o.font = t_rom(&rs, "2716_444-29_h19font.u420", &o.font_size);
    if (!o.monitor || !o.h17_rom || !o.font)
    {
        printf("boot_smoke: SKIP: the H89 ROMs are not in %s\n", t_rom_dir());
        return 77;
    }

    CHECK(h89host_start(&o) == 0);
    int seen = 0;
    for (int i = 0; i < 200 && !seen; i++)
    {
        sleep_ms(10);
        h89host_screen_text(screen, sizeof screen, 0);
        seen = strstr(screen, "H:") != NULL;
    }
    CHECK(seen);

    /* the picture is published */
    static uint32_t fb[H89HOST_FB_WIDTH * H89HOST_FB_HEIGHT];
    uint64_t serial = 0;
    CHECK(h89host_frame_copy(fb, &serial) == 1);
    CHECK(serial != 0);
    int lit = 0;
    for (size_t i = 0; i < sizeof fb / sizeof fb[0]; i++)
        lit += (fb[i] & 0xff) > 0x80;
    CHECK(lit > 50);   /* "H:" is drawn */

    /* the rate over two seconds */
    uint64_t s0 = h89host_slices();
    double t0 = now_s();
    sleep_ms(2000);
    double rate = (h89host_slices() - s0) / (now_s() - t0);
    h89host_stop();
    h89host_pacing p;
    h89host_get_pacing(&p);
    printf("  %.1f slices/s (500 is real time); %llu sleeps, worst late %ld us, %llu resyncs\n",
           rate, (unsigned long long)p.sleeps, p.worst_late_ns / 1000, (unsigned long long)p.resyncs);
#if defined(__linux__)
    CHECK(rate > 490 && rate < 510);
#endif
    return t_done("boot_smoke");
}
