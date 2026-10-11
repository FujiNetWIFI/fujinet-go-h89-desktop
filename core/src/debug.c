/*
 * debug.c -- the session's debugger glue: the engine (core/debugger,
 * core/include/h89debug.h) is created when a window first asks for it and
 * lives as long as the session; a power cycle unhooks it from the old
 * machine and hooks the new one (stopping at its first instruction when a
 * window holds it).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "session_internal.h"

#include "debugger_internal.h"

h89debug *h89session_debugger(h89session *s)
{
    if (!s)
        return NULL;
    if (!s->debugger) {
        h89debug *d = h89debug_create();
        if (!d)
            return NULL;
        h89debug_set_radix(d, h89session_get_int(s, "debugger.radix", H89_RADIX_HEX));
        s->debugger = d;
        /* a window attaching before the machine starts is hooked when it does */
    }
    return (h89debug *)s->debugger;
}

void h89debug_before_power_cycle(struct h89session *s)
{
    if (s && s->debugger)
        h89debug_power_down((h89debug *)s->debugger);
}

void h89debug_after_power_cycle(struct h89session *s)
{
    if (s && s->debugger)
        h89debug_power_up((h89debug *)s->debugger);
}

void h89debug_destroy(struct h89session *s)
{
    if (s && s->debugger) {
        h89debug_free((h89debug *)s->debugger);
        s->debugger = NULL;
    }
}
