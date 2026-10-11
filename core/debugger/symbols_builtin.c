/*
 * symbols_builtin.c -- the labels every session starts with: the H89's
 * memory map (the monitor ROM, the H17's RAM and ROM, user RAM) and the
 * locations fujinet-lib's H89 port code reads.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "symbols.h"

void symtab_add_builtins(symtab *t)
{
    static const struct { uint16_t addr; const char *name; } fixed[] = {
        { 0x0000, "RESET" },
        { 0x0008, "RST08" },
        { 0x0010, "RST10" },
        { 0x0018, "RST18" },
        { 0x0020, "RST20" },
        { 0x0028, "RST28" },
        { 0x0030, "RST30" },
        { 0x0038, "RST38" },
        { 0x0066, "NMI" },
        { 0x1400, "H17_RAM" },
        { 0x1800, "H17_ROM" },
        { 0x2000, "RAM" },
        { 0x201b, "HDOS_TICCNT" },   /* HDOS's 2 ms counter (fujinet-lib's TIKCNT) */
    };
    for (unsigned i = 0; i < sizeof fixed / sizeof fixed[0]; i++)
        symtab_add(t, fixed[i].addr, SYM_BANK_ANY, fixed[i].name, 1);
}
