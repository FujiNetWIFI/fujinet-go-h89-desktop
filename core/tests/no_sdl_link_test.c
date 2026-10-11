/*
 * no_sdl_link -- the core library links and runs on its own: no SDL, no
 * session layer (a session symbol here would fail the link).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>

#include "h89config.h"
#include "h89roms.h"
#include "host.h"

int main(void)
{
    h89_config cfg;
    h89_config_default(&cfg);
    if (h89host_is_running() || !h89_config_validate(&cfg, NULL, 0) || h89_rom_count() <= 0)
    {
        fprintf(stderr, "no_sdl_link: unexpected state\n");
        return 1;
    }
    printf("no_sdl_link: ok\n");
    return 0;
}
