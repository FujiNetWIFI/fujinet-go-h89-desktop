/*
 * h89debug -- the debugger contract: a Z80 debugger engine over the H89
 * (core/debugger) behind a C API the four native debugger windows share.
 * The family's shape (smsdebug.h, nesdebug.h), with the H89's own parts in
 * place of a console's video chip: the terminal, the memory map, the timer
 * and interrupts, the disk controllers, the serial ports, the FujiNet card
 * and CP/NET -- as text sections the windows show and the prompt prints.
 *
 * The engine only engages while a window holds it attached:
 * h89debug_attach() when the window opens -- which also stops the machine,
 * as on every sibling -- and h89debug_detach() when it closes, which lets
 * the machine run on at full speed with no hook installed.
 *
 * Numbers are shown in the radix chosen (h89debug_set_radix): hexadecimal,
 * octal, or the split octal Heath's own tools use for addresses (two bytes
 * of three octal digits, 041.000). The prompt reads every form back.
 *
 * Calls are safe from the UI thread at any time. Inspection is meant for
 * the stopped state; while running it answers with whatever is current.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89DEBUG_H
#define H89DEBUG_H

#include <stdint.h>

#include "h89session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* h89session_debugger(): lazily created; lives as long as the session. */

/* ---- attach / stop / go ---------------------------------------------------- */
/* Engage the engine and stop at the next instruction. Idempotent. */
void h89debug_attach(h89debug *d);
/* Disengage and let the machine run. Breakpoints are kept. */
void h89debug_detach(h89debug *d);
int  h89debug_is_attached(h89debug *d);

int  h89debug_is_stopped(h89debug *d);
void h89debug_stop(h89debug *d);          /* break at the next instruction */
void h89debug_resume(h89debug *d);        /* "run" */
/* Why the machine last stopped ("stopped", "breakpoint: exec 0038", "write
 * 2000 = 12", "step", ...) and the PC, -1 if none. Returns length. */
int  h89debug_stop_reason(h89debug *d, char *dst, int dstsz, int *address);
/* Bumped whenever a command ran or the machine stopped or resumed, so a
 * window knows when to refresh. */
unsigned h89debug_generation(h89debug *d);

/* ---- the radix ---------------------------------------------------------------- */
typedef enum {
    H89_RADIX_HEX = 0,        /* 2000, 3F */
    H89_RADIX_OCTAL,          /* 020000Q, 077Q */
    H89_RADIX_SPLIT_OCTAL,    /* 040.000 for words, 077Q for bytes */
} h89_radix;
int  h89debug_radix(h89debug *d);
void h89debug_set_radix(h89debug *d, int radix);
/* A word / a byte in the radix ("2000" / "020000Q" / "040.000"). */
const char *h89debug_fmt16(h89debug *d, unsigned value, char *buf, int bufsz);
const char *h89debug_fmt8(h89debug *d, unsigned value, char *buf, int bufsz);

/* ---- the prompt ------------------------------------------------------------ */
/* Run one command; "help" lists them. Expressions: numbers ($2000, 0x2000,
 * 2000h, 8192, 020000Q, 040.000, %1010), registers, flags, [addr] byte and
 * {addr} word reads, labels, cycles and slices, and in a breakpoint
 * condition `value` and `addr` for the access; C operators with C
 * precedence. Output text into dst; returns its length. */
int  h89debug_command(h89debug *d, const char *command, char *dst, int dstsz);
/* Completions for a prefix (commands, registers and labels), one per line.
 * Returns the count. */
int  h89debug_completions(h89debug *d, const char *prefix, char *dst, int dstsz);
/* Evaluate an expression the way the prompt does (what a window's address
 * and value boxes take); 0 and *out, or -1 with the reason in err. */
int  h89debug_eval(h89debug *d, const char *text, long *out, char *err, int errsz);

/* ---- stepping ------------------------------------------------------------------ */
void h89debug_step(h89debug *d);          /* F7: one instruction */
void h89debug_step_over(h89debug *d);     /* F8: over a CALL/RST/DJNZ/block op */
void h89debug_step_out(h89debug *d);      /* Shift+F8: to the RET */
/* Run n of the H89's 2 ms clock ticks, then stop. */
void h89debug_ticks(h89debug *d, int n);
/* Run until PC == addr. */
void h89debug_run_to(h89debug *d, uint16_t addr);

/* ---- CPU ----------------------------------------------------------------------- */
typedef struct {
    int pc, sp, af, bc, de, hl, ix, iy, wz;
    int af2, bc2, de2, hl2;
    int i, r, im, iff1, iff2, halted;
    int sf, zf, yf, hf, xf, pf, nf, cf;   /* the flags in F */
    uint64_t cycles;                      /* T-states since power-on */
    uint64_t ticks;                       /* 2 ms clock ticks since power-on */
} h89debug_cpu;
void h89debug_cpu_get(h89debug *d, h89debug_cpu *out);
typedef enum {
    H89_REG_PC = 0, H89_REG_SP, H89_REG_AF, H89_REG_BC, H89_REG_DE, H89_REG_HL,
    H89_REG_IX, H89_REG_IY, H89_REG_AF2, H89_REG_BC2, H89_REG_DE2, H89_REG_HL2,
    H89_REG_I, H89_REG_R, H89_REG_IM, H89_REG_IFF1, H89_REG_IFF2,
    H89_REG_A, H89_REG_F, H89_REG_B, H89_REG_C, H89_REG_D, H89_REG_E,
    H89_REG_H, H89_REG_L,
    H89_FLAG_S, H89_FLAG_Z, H89_FLAG_H, H89_FLAG_PV, H89_FLAG_N, H89_FLAG_C,
    H89_REG_COUNT
} h89debug_reg;
void h89debug_cpu_set(h89debug *d, int reg, int value);

/* ---- memory --------------------------------------------------------------------- */
/* Reads as the CPU sees memory now (the ROMs, the H17's RAM, the MMS 77318's
 * banks as mapped), without side effects. */
int  h89debug_read(h89debug *d, uint16_t addr, uint8_t *dst, int n);
/* A debugger edit, as the CPU would write it (ROM ignores it). */
void h89debug_write(h89debug *d, uint16_t addr, uint8_t value);

/* ---- the H89's parts, as text ------------------------------------------------------ */
typedef enum {
    H89DEBUG_SECTION_TERMINAL = 0,  /* the H19: cursor, modes, switches, line */
    H89DEBUG_SECTION_MEMORY,        /* the map: model, ORG-0, GPP, MMS 77318 */
    H89DEBUG_SECTION_TIMER,         /* the 2 ms clock, interrupt lines, NMI */
    H89DEBUG_SECTION_H17,           /* the H-88-1 and its drives */
    H89DEBUG_SECTION_FDC,           /* the Z-89-37 / MMS 77316 WD1797s and drives */
    H89DEBUG_SECTION_SASI,          /* the MMS 77320 */
    H89DEBUG_SECTION_SERIAL,        /* the 8250s */
    H89DEBUG_SECTION_CARD,          /* the FujiNet card's 82C55 and link */
    H89DEBUG_SECTION_COUNT
} h89debug_section_id;
const char *h89debug_section_name(int section);
/* The section as lines of text ("(not fitted)" for a card that is not in a
 * slot). Returns the length. */
int  h89debug_section(h89debug *d, int section, char *dst, int dstsz);

/* ---- disassembly -------------------------------------------------------------------- */
typedef struct {
    uint16_t address;
    int is_pc;
    int has_breakpoint;
    int length;
    char addr_text[16];      /* the address in the radix */
    char bytes[24];
    char label[48];
    char disasm[64];
    char comment[64];        /* a jump/call target's label, a port's name */
} h89debug_line;
/* Up to `max` lines starting at `addr`. Returns the count; *pc_line is the
 * index of the PC's line in `out`, -1 when not among them. */
int  h89debug_disassemble(h89debug *d, uint16_t addr, h89debug_line *out, int max, int *pc_line);
/* The address `rows` disassembly rows before (negative) or after `addr`.
 * Backwards is a best guess, as on every Z80 debugger. */
int  h89debug_row_address(h89debug *d, uint16_t addr, int rows);
int  h89debug_label_address(h89debug *d, const char *label);
int  h89debug_address_label(h89debug *d, uint16_t addr, char *dst, int dstsz);
int  h89debug_set_label(h89debug *d, uint16_t addr, const char *label);
/* Load a symbol file: a z88dk .map ("name = $AAAA ; ..."), SDCC .noi,
 * WLA-DX .sym or a plain "AAAA name" list. Returns labels loaded or -1;
 * message into msg. */
int  h89debug_load_symbols(h89debug *d, const char *path, char *msg, int msgsz);

/* ---- breakpoints ---------------------------------------------------------------------- */
typedef enum {
    H89DEBUG_BP_EXEC = 1, H89DEBUG_BP_READ = 2, H89DEBUG_BP_WRITE = 4,
    H89DEBUG_BP_IN = 8, H89DEBUG_BP_OUT = 16
} h89debug_bp_type;
typedef struct {
    int id;
    int type;                /* h89debug_bp_type bits */
    uint16_t start, end;     /* a port range for IN/OUT (0-255) */
    int enabled;
    unsigned hits;
    char condition[128];
} h89debug_breakpoint;
int  h89debug_breakpoint_toggle(h89debug *d, uint16_t addr);
int  h89debug_breakpoint_check(h89debug *d, uint16_t addr);
/* Returns the new id, or -1 if the condition does not parse. */
int  h89debug_breakpoint_add(h89debug *d, int type, uint16_t start, uint16_t end,
                             const char *condition);
void h89debug_breakpoint_remove(h89debug *d, int id);
void h89debug_breakpoint_enable(h89debug *d, int id, int enabled);
int  h89debug_breakpoint_list(h89debug *d, h89debug_breakpoint *out, int max);
void h89debug_breakpoint_clear(h89debug *d);

/* ---- the trace ring ---------------------------------------------------------------------- */
typedef struct {
    uint16_t pc, sp, af, bc, de, hl;
    uint64_t cycles;
} h89debug_trace;
void h89debug_trace_enable(h89debug *d, int on);
int  h89debug_trace_enabled(h89debug *d);
/* Newest first; returns entries copied. */
int  h89debug_trace_read(h89debug *d, h89debug_trace *out, int max);
void h89debug_trace_clear(h89debug *d);

/* ---- files ---------------------------------------------------------------------------------- */
/* kind: "dis" (the disassembly of memory as mapped now), "ram" (the 64K as
 * the CPU sees it), "regs" (the CPU and every section as text). Returns 0
 * or -1; message into msg. */
int  h89debug_save(h89debug *d, const char *kind, const char *path, char *msg, int msgsz);

#ifdef __cplusplus
}
#endif

#endif /* H89DEBUG_H */
