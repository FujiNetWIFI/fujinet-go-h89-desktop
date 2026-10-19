/*
 * debugger_internal -- the engine's side of the session glue and the
 * prompt's view of the engine. Not installed.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_DEBUGGER_INTERNAL_H
#define H89_DEBUGGER_INTERNAL_H

#include "h89debug.h"
#include "symbols.h"

#ifdef __cplusplus
extern "C" {
#endif

h89debug *h89debug_create(void);
void h89debug_free(h89debug *d);
/* The machine is about to go away (a power cycle or a stop): resume and
 * unhook, remembering whether a window held the engine. */
void h89debug_power_down(h89debug *d);
/* A new machine is up: hook it again if a window held the engine (which
 * stops it at its first instruction, so a power cycle can be debugged from
 * the reset vector). */
void h89debug_power_up(h89debug *d);
/* Hook the FujiNet trace into the card, whatever card a power cycle builds;
 * h89debug_free unhooks it. */
void h89debug_fujinet_install(h89debug *d);

/* For the prompt (prompt.c). */
symtab *h89debug_symbols(h89debug *d);
/* Register names the evaluator knows, for completion. */
extern const char *const h89debug_reg_names[];

#ifdef __cplusplus
}
#endif

#endif /* H89_DEBUGGER_INTERNAL_H */
