/*
 * debug -- the debugger engine (core/debugger) on the paced host, with a
 * program of our own in place of the monitor ROM (no Heath ROMs):
 *
 *   0000  LD SP,F000h
 *   0003  LD A,0
 *   0005  INC A          ; loop
 *   0006  CALL 0010h
 *   0009  LD (3000h),A
 *   000C  OUT (10h),A
 *   000E  JR 0005h
 *   0010  INC B          ; sub
 *   0011  RET
 *
 * Attach stops it; an exec breakpoint, step, step out, step over, a write
 * and an OUT breakpoint, run-to, ticks, the radix in the disassembly and
 * the evaluator, the sections, and detach letting it run on.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "debugger_internal.h"
#include "host.h"
#include "test_util.h"

#include <string.h>
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

static int wait_stopped(h89debug *d)
{
    for (int i = 0; i < 300; i++)
    {
        if (h89debug_is_stopped(d))
            return 1;
        sleep_ms(5);
    }
    return 0;
}

static int pc_of(h89debug *d)
{
    h89debug_cpu c;
    h89debug_cpu_get(d, &c);
    return c.pc;
}

int main(void)
{
    static const uint8_t prog[] = {
        0x31, 0x00, 0xf0,       /* 0000 LD SP,F000h */
        0x3e, 0x00,             /* 0003 LD A,0 */
        0x3c,                   /* 0005 INC A */
        0xcd, 0x10, 0x00,       /* 0006 CALL 0010h */
        0x32, 0x00, 0x30,       /* 0009 LD (3000h),A */
        0xd3, 0x10,             /* 000C OUT (10h),A */
        0x18, 0xf5,             /* 000E JR 0005h */
        0x04,                   /* 0010 INC B */
        0xc9,                   /* 0011 RET */
    };
    static uint8_t rom[2048], font[2048];
    h89host_opts o;
    char text[4096], why[160];
    h89debug_line lines[8];
    h89debug_cpu c;
    int pcl, addr;

    memcpy(rom, prog, sizeof prog);
    memset(&o, 0, sizeof o);
    h89_config_default(&o.config);
    for (int i = 0; i < H89_SLOTS; i++)
        o.config.slot[i] = H89_CARD_EMPTY;
    o.monitor = rom;
    o.monitor_size = sizeof rom;
    o.font = font;
    o.font_size = sizeof font;
    if (h89host_start(&o) != 0)
    {
        printf("debug: %s\n", h89host_last_error());
        CHECK(0);
        return t_done("debug");
    }

    h89debug *d = h89debug_create();
    h89debug_attach(d);
    CHECK(wait_stopped(d));
    CHECK(pc_of(d) <= 0x11);
    h89debug_stop_reason(d, why, sizeof why, &addr);
    CHECK(strcmp(why, "stopped") == 0);

    /* an exec breakpoint at the subroutine, with a label */
    CHECK(h89debug_set_label(d, 0x10, "sub") >= 0);
    CHECK(h89debug_breakpoint_add(d, H89DEBUG_BP_EXEC, 0x10, 0x10, "") > 0);
    h89debug_resume(d);
    CHECK(wait_stopped(d));
    CHECK_EQ_INT(pc_of(d), 0x10);
    h89debug_stop_reason(d, why, sizeof why, &addr);
    CHECK(strstr(why, "breakpoint") != NULL);
    h89debug_breakpoint_clear(d);

    /* step out to the return address; step one */
    h89debug_step_out(d);
    CHECK(wait_stopped(d));
    CHECK_EQ_INT(pc_of(d), 0x09);
    h89debug_step(d);
    CHECK(wait_stopped(d));
    CHECK_EQ_INT(pc_of(d), 0x0c);

    /* step over the CALL: B goes up by one, PC to the next instruction */
    h89debug_cpu_set(d, H89_REG_PC, 0x06);
    h89debug_cpu_get(d, &c);
    int b0 = (c.bc >> 8) & 0xff;
    h89debug_step_over(d);
    CHECK(wait_stopped(d));
    h89debug_cpu_get(d, &c);
    CHECK_EQ_INT(c.pc, 0x09);
    CHECK_EQ_INT((c.bc >> 8) & 0xff, (b0 + 1) & 0xff);

    /* a write breakpoint, then an OUT breakpoint with a condition */
    CHECK(h89debug_breakpoint_add(d, H89DEBUG_BP_WRITE, 0x3000, 0x3000, "") > 0);
    h89debug_resume(d);
    CHECK(wait_stopped(d));
    h89debug_stop_reason(d, why, sizeof why, &addr);
    CHECK(strncmp(why, "write 3000 = ", 13) == 0);
    h89debug_breakpoint_clear(d);
    CHECK(h89debug_breakpoint_add(d, H89DEBUG_BP_OUT, 0x10, 0x10, "value == 5") > 0);
    CHECK(h89debug_breakpoint_add(d, H89DEBUG_BP_OUT, 0x10, 0x10, "value ==") < 0);
    h89debug_resume(d);
    CHECK(wait_stopped(d));
    h89debug_stop_reason(d, why, sizeof why, &addr);
    CHECK(strcmp(why, "out 10 <- 05") == 0);
    h89debug_breakpoint_clear(d);

    /* run to */
    h89debug_run_to(d, 0x10);
    CHECK(wait_stopped(d));
    CHECK_EQ_INT(pc_of(d), 0x10);

    /* the disassembly in each radix, with the target's label */
    CHECK_EQ_INT(h89debug_disassemble(d, 0, lines, 4, &pcl), 4);
    CHECK(strcmp(lines[0].disasm, "LD SP,$F000") == 0);
    CHECK(strcmp(lines[3].disasm, "CALL $0010") == 0 && strcmp(lines[3].comment, "sub") == 0);
    h89debug_set_radix(d, H89_RADIX_OCTAL);
    h89debug_disassemble(d, 0, lines, 1, &pcl);
    CHECK(strcmp(lines[0].disasm, "LD SP,170000Q") == 0);
    CHECK(strcmp(lines[0].bytes, "061 000 360") == 0);
    h89debug_set_radix(d, H89_RADIX_SPLIT_OCTAL);
    h89debug_disassemble(d, 0, lines, 1, &pcl);
    CHECK(strcmp(lines[0].disasm, "LD SP,360.000") == 0);
    CHECK(strcmp(lines[0].addr_text, "000.000") == 0);
    h89debug_set_radix(d, H89_RADIX_HEX);

    /* the evaluator reads every radix */
    long v;
    char err[64];
    CHECK(h89debug_eval(d, "040.000", &v, err, sizeof err) == 0 && v == 0x2000);
    CHECK(h89debug_eval(d, "377Q + 1", &v, err, sizeof err) == 0 && v == 0x100);
    CHECK(h89debug_eval(d, "sub + $10", &v, err, sizeof err) == 0 && v == 0x20);
    CHECK(h89debug_eval(d, "pc", &v, err, sizeof err) == 0 && v == 0x10);

    /* the prompt */
    h89debug_command(d, "radix split", text, sizeof text);
    CHECK(strstr(text, "radix split") != NULL);
    h89debug_command(d, "regs", text, sizeof text);
    CHECK(strstr(text, "PC=000.020") != NULL);
    h89debug_command(d, "radix hex", text, sizeof text);
    h89debug_command(d, "memmap", text, sizeof text);
    CHECK(strstr(text, "SW501") != NULL);
    h89debug_command(d, "card", text, sizeof text);
    CHECK(strstr(text, "no FujiNet card") != NULL);

    /* ticks: the 2 ms clock runs on */
    h89debug_cpu_get(d, &c);
    uint64_t t0 = c.ticks;
    h89debug_ticks(d, 5);
    CHECK(wait_stopped(d));
    h89debug_cpu_get(d, &c);
    CHECK(c.ticks >= t0 + 5);

    /* detached, it runs */
    h89debug_detach(d);
    CHECK(!h89debug_is_stopped(d));
    uint64_t s0 = h89host_slices();
    sleep_ms(100);
    CHECK(h89host_slices() > s0);

    h89debug_free(d);
    h89host_stop();
    return t_done("debug");
}
