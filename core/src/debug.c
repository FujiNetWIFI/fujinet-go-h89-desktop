/*
 * debug.c -- the session's debugger glue (M5 fills it in: the engine is
 * core/debugger, the contract core/include/h89debug.h).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "session_internal.h"

h89debug *h89session_debugger(h89session *s)
{
    (void)s;
    return NULL;
}

void h89debug_before_power_cycle(struct h89session *s) { (void)s; }
void h89debug_after_power_cycle(struct h89session *s) { (void)s; }
void h89debug_destroy(struct h89session *s) { (void)s; }
