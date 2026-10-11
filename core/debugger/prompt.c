/*
 * prompt.c -- the debugger's command line (h89debug_command) and its Tab
 * completion (from the family's Master System sibling, with the H89's own
 * commands and its radix). Every command is a thin layer over the public API, so a
 * window's buttons and the prompt can never disagree.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "debugger_internal.h"

typedef struct {
    char *dst;
    int cap, len;
} outbuf;

static void out(outbuf *o, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (o->len >= o->cap - 1)
        return;
    va_start(ap, fmt);
    n = vsnprintf(o->dst + o->len, (size_t)(o->cap - o->len), fmt, ap);
    va_end(ap);
    if (n > 0)
        o->len += n < o->cap - o->len ? n : o->cap - o->len - 1;
}

static const char *const commands[] = {
    "help", "step", "over", "out", "run", "stop", "runto", "ticks",
    "break", "bpr", "bpw", "bpin", "bpout", "delete", "enable", "disable", "clear",
    "breaks", "print", "regs", "set", "mem", "poke", "radix", "terminal", "memmap",
    "timer", "h17", "fdc", "sasi", "serial", "card", "label", "labels", "disasm",
    "trace", "save", NULL,
};

static const char *help_text =
    "step [n]               run n instructions (F7)\n"
    "over                   step over a CALL/RST/DJNZ/block op (F8)\n"
    "out                    run to the return (Shift+F8)\n"
    "run | stop             go / break\n"
    "runto <addr>           run until PC = addr\n"
    "ticks [n]              run n of the H89's 2 ms clock ticks\n"
    "break <addr>[-<end>] [cond]     execute breakpoint\n"
    "bpr | bpw <addr>[-<end>] [cond] read / write breakpoint\n"
    "bpin | bpout <port>[-<end>] [cond]  I/O breakpoint\n"
    "delete <id> | enable <id> | disable <id> | clear | breaks\n"
    "print <expr>           e.g. print hl, print [040.000], print {sp}\n"
    "regs                   CPU registers\n"
    "set <reg> <expr>       e.g. set pc 0, set zf 1\n"
    "mem <addr> [len]       dump memory (side-effect free)\n"
    "poke <addr> <v...>     write memory\n"
    "radix [hex|octal|split]  how numbers are shown\n"
    "terminal | memmap | timer | h17 | fdc | sasi | serial | card\n"
    "label <addr> <name> | labels <file>\n"
    "disasm [addr] [n]\n"
    "trace [on|off|n]       record / show the last n instructions\n"
    "save <dis|ram|regs> <file>\n"
    "Expressions: $2000 0x2000 2000h 8192 020000Q 040.000 %1010; registers\n"
    "a..l af..iy sp pc i r af' ...; flags cf zf sf hf pf nf; [addr] {addr};\n"
    "labels; cycles ticks; in a condition, value and addr; C operators.\n";

static char *next_tok(char **p)
{
    char *s = *p, *t;
    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
    {
        *p = s;
        return NULL;
    }
    t = s;
    while (*s && !isspace((unsigned char)*s))
        s++;
    if (*s)
        *s++ = '\0';
    *p = s;
    return t;
}

static char *rest_of(char *p)
{
    while (*p && isspace((unsigned char)*p))
        p++;
    return p;
}

static int eval(h89debug *d, outbuf *o, const char *text, long *v)
{
    char err[96];
    if (h89debug_eval(d, text, v, err, sizeof err) != 0)
    {
        out(o, "error: %s\n", err);
        return -1;
    }
    return 0;
}

/* "<addr>" or "<addr>-<end>" */
static int range(h89debug *d, outbuf *o, char *tok, long *a, long *b)
{
    char *dash = strchr(tok + 1, '-');
    if (dash)
    {
        *dash = '\0';
        if (eval(d, o, tok, a) || eval(d, o, dash + 1, b))
            return -1;
        return 0;
    }
    if (eval(d, o, tok, a))
        return -1;
    *b = *a;
    return 0;
}

static void show_regs(h89debug *d, outbuf *o)
{
    h89debug_cpu c;
    char w[12][16], b[2][8];
    h89debug_cpu_get(d, &c);
    out(o, "PC=%s SP=%s AF=%s BC=%s DE=%s HL=%s IX=%s IY=%s\n",
        h89debug_fmt16(d, c.pc, w[0], 16), h89debug_fmt16(d, c.sp, w[1], 16),
        h89debug_fmt16(d, c.af, w[2], 16), h89debug_fmt16(d, c.bc, w[3], 16),
        h89debug_fmt16(d, c.de, w[4], 16), h89debug_fmt16(d, c.hl, w[5], 16),
        h89debug_fmt16(d, c.ix, w[6], 16), h89debug_fmt16(d, c.iy, w[7], 16));
    out(o, "AF'=%s BC'=%s DE'=%s HL'=%s I=%s R=%s IM%d IFF1=%d IFF2=%d%s\n",
        h89debug_fmt16(d, c.af2, w[8], 16), h89debug_fmt16(d, c.bc2, w[9], 16),
        h89debug_fmt16(d, c.de2, w[10], 16), h89debug_fmt16(d, c.hl2, w[11], 16),
        h89debug_fmt8(d, c.i, b[0], 8), h89debug_fmt8(d, c.r, b[1], 8), c.im, c.iff1, c.iff2,
        c.halted ? " HALT" : "");
    out(o, "flags %c%c%c%c%c%c%c%c  %llu cycles  %llu ticks\n",
        c.sf ? 'S' : '-', c.zf ? 'Z' : '-', c.yf ? 'Y' : '-', c.hf ? 'H' : '-',
        c.xf ? 'X' : '-', c.pf ? 'P' : '-', c.nf ? 'N' : '-', c.cf ? 'C' : '-',
        (unsigned long long)c.cycles, (unsigned long long)c.ticks);
}

static void dump(h89debug *d, outbuf *o, uint16_t addr, const uint8_t *b, int n)
{
    const int per = h89debug_radix(d) == H89_RADIX_HEX ? 16 : 8;
    for (int i = 0; i < n; i += per)
    {
        char a[16], v[8];
        out(o, "%-8s", h89debug_fmt16(d, (uint16_t)(addr + i), a, sizeof a));
        for (int j = 0; j < per; j++)
            out(o, j < n - i ? " %s" : (per == 16 ? "   " : "    "),
                j < n - i ? h89debug_fmt8(d, b[i + j], v, sizeof v) : "");
        out(o, "  ");
        for (int j = 0; j < per && i + j < n; j++)
            out(o, "%c", (b[i + j] >= 0x20 && b[i + j] < 0x7f) ? b[i + j] : '.');
        out(o, "\n");
    }
}

static int reg_by_name(const char *n)
{
    static const struct { const char *name; int reg; } map[] = {
        {"pc", H89_REG_PC}, {"sp", H89_REG_SP}, {"af", H89_REG_AF}, {"bc", H89_REG_BC},
        {"de", H89_REG_DE}, {"hl", H89_REG_HL}, {"ix", H89_REG_IX}, {"iy", H89_REG_IY},
        {"af'", H89_REG_AF2}, {"bc'", H89_REG_BC2}, {"de'", H89_REG_DE2}, {"hl'", H89_REG_HL2},
        {"i", H89_REG_I}, {"r", H89_REG_R}, {"im", H89_REG_IM}, {"iff1", H89_REG_IFF1},
        {"iff2", H89_REG_IFF2}, {"a", H89_REG_A}, {"f", H89_REG_F}, {"b", H89_REG_B},
        {"c", H89_REG_C}, {"d", H89_REG_D}, {"e", H89_REG_E}, {"h", H89_REG_H}, {"l", H89_REG_L},
        {"sf", H89_FLAG_S}, {"zf", H89_FLAG_Z}, {"hf", H89_FLAG_H}, {"pf", H89_FLAG_PV},
        {"nf", H89_FLAG_N}, {"cf", H89_FLAG_C},
    };
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
        if (!strcmp(map[i].name, n))
            return map[i].reg;
    return -1;
}

static void list_breaks(h89debug *d, outbuf *o)
{
    h89debug_breakpoint bp[128];
    int n = h89debug_breakpoint_list(d, bp, 128);
    if (!n)
    {
        out(o, "no breakpoints\n");
        return;
    }
    for (int i = 0; i < n; i++)
    {
        char kinds[16] = "";
        if (bp[i].type & H89DEBUG_BP_EXEC)  strcat(kinds, "x");
        if (bp[i].type & H89DEBUG_BP_READ)  strcat(kinds, "r");
        if (bp[i].type & H89DEBUG_BP_WRITE) strcat(kinds, "w");
        if (bp[i].type & H89DEBUG_BP_IN)    strcat(kinds, "i");
        if (bp[i].type & H89DEBUG_BP_OUT)   strcat(kinds, "o");
        {
            char a[16], b[16];
            const int io = (bp[i].type & (H89DEBUG_BP_IN | H89DEBUG_BP_OUT)) != 0;
            if (io)
            {
                h89debug_fmt8(d, bp[i].start, a, sizeof a);
                h89debug_fmt8(d, bp[i].end, b, sizeof b);
            }
            else
            {
                h89debug_fmt16(d, bp[i].start, a, sizeof a);
                h89debug_fmt16(d, bp[i].end, b, sizeof b);
            }
            if (bp[i].start == bp[i].end)
                out(o, "#%d %-3s %s", bp[i].id, kinds, a);
            else
                out(o, "#%d %-3s %s-%s", bp[i].id, kinds, a, b);
        }
        out(o, "%s%s%s  hits %u\n", bp[i].enabled ? "" : " (disabled)",
            bp[i].condition[0] ? " if " : "", bp[i].condition, bp[i].hits);
    }
}

static void add_bp(h89debug *d, outbuf *o, int type, char *args, int io)
{
    char *t = next_tok(&args);
    long a, b;
    int id;

    if (!t)
    {
        out(o, "usage: <addr>[-<end>] [condition]\n");
        return;
    }
    if (range(d, o, t, &a, &b))
        return;
    if (io && (a > 255 || b > 255))
    {
        out(o, "error: ports are 0-255\n");
        return;
    }
    id = h89debug_breakpoint_add(d, type, (uint16_t)a, (uint16_t)b, rest_of(args));
    if (id < 0)
        out(o, "error: the condition does not parse\n");
    else
        out(o, "breakpoint #%d\n", id);
}

int h89debug_command(h89debug *d, const char *command, char *dst, int dstsz)
{
    char line[512];
    char *p = line, *cmd;
    outbuf o = { dst, dstsz, 0 };
    long v, w;

    if (!d || !dst || dstsz <= 0)
        return 0;
    dst[0] = '\0';
    snprintf(line, sizeof line, "%s", command ? command : "");
    cmd = next_tok(&p);
    if (!cmd)
        return 0;
    for (char *c = cmd; *c; c++)
        *c = (char)tolower((unsigned char)*c);

    if (!strcmp(cmd, "help") || !strcmp(cmd, "?"))
        out(&o, "%s", help_text);
    else if (!strcmp(cmd, "step") || !strcmp(cmd, "s"))
    {
        char *t = next_tok(&p);
        v = 1;
        if (t && eval(d, &o, t, &v))
            return o.len;
        if (!h89debug_is_stopped(d))
            out(&o, "the machine is running (stop first)\n");
        else if (v <= 1)
            h89debug_step(d);
        else
        {
            /* step n: the hook counts them down */
            h89debug_step(d);
            for (long i = 1; i < v; i++)
            {
                int tries = 0;
                while (!h89debug_is_stopped(d) && tries++ < 2000)
                {
                    struct timespec ts = { 0, 100000L };
                    nanosleep(&ts, NULL);
                }
                h89debug_step(d);
            }
        }
    }
    else if (!strcmp(cmd, "over") || !strcmp(cmd, "o"))
        h89debug_step_over(d);
    else if (!strcmp(cmd, "out"))
        h89debug_step_out(d);
    else if (!strcmp(cmd, "run") || !strcmp(cmd, "go") || !strcmp(cmd, "g"))
        h89debug_resume(d);
    else if (!strcmp(cmd, "stop"))
        h89debug_stop(d);
    else if (!strcmp(cmd, "runto"))
    {
        char *t = next_tok(&p);
        if (!t || eval(d, &o, t, &v))
            return o.len;
        h89debug_run_to(d, (uint16_t)v);
    }
    else if (!strcmp(cmd, "ticks"))
    {
        char *t = next_tok(&p);
        v = 1;
        if (t && eval(d, &o, t, &v))
            return o.len;
        h89debug_ticks(d, (int)v);
    }
    else if (!strcmp(cmd, "break") || !strcmp(cmd, "bp"))
        add_bp(d, &o, H89DEBUG_BP_EXEC, p, 0);
    else if (!strcmp(cmd, "bpr"))
        add_bp(d, &o, H89DEBUG_BP_READ, p, 0);
    else if (!strcmp(cmd, "bpw"))
        add_bp(d, &o, H89DEBUG_BP_WRITE, p, 0);
    else if (!strcmp(cmd, "bpin"))
        add_bp(d, &o, H89DEBUG_BP_IN, p, 1);
    else if (!strcmp(cmd, "bpout"))
        add_bp(d, &o, H89DEBUG_BP_OUT, p, 1);
    else if (!strcmp(cmd, "delete") || !strcmp(cmd, "enable") || !strcmp(cmd, "disable"))
    {
        char *t = next_tok(&p);
        if (!t)
            out(&o, "usage: %s <id>\n", cmd);
        else
        {
            long id = strtol(t[0] == '#' ? t + 1 : t, NULL, 10);
            if (!strcmp(cmd, "delete"))
                h89debug_breakpoint_remove(d, (int)id);
            else
                h89debug_breakpoint_enable(d, (int)id, !strcmp(cmd, "enable"));
        }
    }
    else if (!strcmp(cmd, "clear"))
        h89debug_breakpoint_clear(d);
    else if (!strcmp(cmd, "breaks") || !strcmp(cmd, "bl"))
        list_breaks(d, &o);
    else if (!strcmp(cmd, "print") || !strcmp(cmd, "p"))
    {
        if (eval(d, &o, rest_of(p), &v) == 0)
        {
            char bin[17];
            for (int i = 0; i < 16; i++)
                bin[i] = (char)('0' + ((v >> (15 - i)) & 1));
            bin[16] = '\0';
            out(&o, "$%04lX  %06loQ  %03lo.%03lo  %ld  %%%s\n", (unsigned long)v & 0xffff,
                (unsigned long)v & 0xffff, ((unsigned long)v >> 8) & 0xff,
                (unsigned long)v & 0xff, v, bin);
        }
    }
    else if (!strcmp(cmd, "regs") || !strcmp(cmd, "r"))
        show_regs(d, &o);
    else if (!strcmp(cmd, "set"))
    {
        char *r = next_tok(&p);
        int reg = r ? reg_by_name(r) : -1;
        if (reg < 0)
            out(&o, "usage: set <reg> <expr>\n");
        else if (eval(d, &o, rest_of(p), &v) == 0)
        {
            if (!h89debug_is_stopped(d))
                out(&o, "the machine is running (stop first)\n");
            else
            {
                h89debug_cpu_set(d, reg, (int)v);
                show_regs(d, &o);
            }
        }
    }
    else if (!strcmp(cmd, "mem") || !strcmp(cmd, "m"))
    {
        char *t = next_tok(&p), *l = next_tok(&p);
        uint8_t buf[256];
        w = 64;
        if (!t || eval(d, &o, t, &v) || (l && eval(d, &o, l, &w)))
            return o.len;
        if (w > 256)
            w = 256;
        h89debug_read(d, (uint16_t)v, buf, (int)w);
        dump(d, &o, (uint16_t)v, buf, (int)w);
    }
    else if (!strcmp(cmd, "poke"))
    {
        char *t = next_tok(&p), *b;
        int n = 0;
        if (!t || eval(d, &o, t, &v))
            return o.len;
        while ((b = next_tok(&p)) != NULL)
        {
            if (eval(d, &o, b, &w))
                return o.len;
            h89debug_write(d, (uint16_t)(v + n), (uint8_t)w);
            n++;
        }
        {
            char a[16];
            out(&o, "%d byte%s at %s\n", n, n == 1 ? "" : "s", h89debug_fmt16(d, (uint16_t)v, a, sizeof a));
        }
    }
    else if (!strcmp(cmd, "radix"))
    {
        char *t = next_tok(&p);
        static const char *const names[] = { "hex", "octal", "split" };
        if (t)
        {
            if (!strncmp(t, "hex", 3))
                h89debug_set_radix(d, H89_RADIX_HEX);
            else if (!strncmp(t, "oct", 3))
                h89debug_set_radix(d, H89_RADIX_OCTAL);
            else if (!strncmp(t, "split", 5))
                h89debug_set_radix(d, H89_RADIX_SPLIT_OCTAL);
            else
            {
                out(&o, "usage: radix [hex|octal|split]\n");
                return o.len;
            }
        }
        out(&o, "radix %s\n", names[h89debug_radix(d)]);
    }
    else if (!strcmp(cmd, "terminal") || !strcmp(cmd, "memmap") || !strcmp(cmd, "timer") ||
             !strcmp(cmd, "h17") || !strcmp(cmd, "fdc") || !strcmp(cmd, "sasi") ||
             !strcmp(cmd, "serial") || !strcmp(cmd, "card"))
    {
        static const char *const names[] = { "terminal", "memmap", "timer", "h17", "fdc",
                                             "sasi", "serial", "card" };
        static char text[4096];
        for (int i = 0; i < H89DEBUG_SECTION_COUNT; i++)
            if (!strcmp(cmd, names[i]))
            {
                h89debug_section(d, i, text, sizeof text);
                out(&o, "%s", text);
            }
    }
    else if (!strcmp(cmd, "label"))
    {
        char *t = next_tok(&p), *n = next_tok(&p);
        if (!t || eval(d, &o, t, &v))
            return o.len;
        h89debug_set_label(d, (uint16_t)v, n);
    }
    else if (!strcmp(cmd, "labels"))
    {
        char msg[256];
        char *f = rest_of(p);
        h89debug_load_symbols(d, f, msg, sizeof msg);
        out(&o, "%s\n", msg);
    }
    else if (!strcmp(cmd, "disasm") || !strcmp(cmd, "d"))
    {
        char *t = next_tok(&p), *n = next_tok(&p);
        h89debug_line lines[64];
        h89debug_cpu c;
        int pcl, cnt;
        h89debug_cpu_get(d, &c);
        v = c.pc;
        w = 16;
        if ((t && eval(d, &o, t, &v)) || (n && eval(d, &o, n, &w)))
            return o.len;
        if (w > 64)
            w = 64;
        cnt = h89debug_disassemble(d, (uint16_t)v, lines, (int)w, &pcl);
        for (int i = 0; i < cnt; i++)
        {
            if (lines[i].label[0])
                out(&o, "%s:\n", lines[i].label);
            out(&o, "%c%c%-8s %-16s %-24s%s%s\n", lines[i].is_pc ? '>' : ' ',
                lines[i].has_breakpoint ? '*' : ' ', lines[i].addr_text, lines[i].bytes,
                lines[i].disasm, lines[i].comment[0] ? "; " : "", lines[i].comment);
        }
    }
    else if (!strcmp(cmd, "trace"))
    {
        char *t = next_tok(&p);
        if (t && !strcmp(t, "on"))
            h89debug_trace_enable(d, 1);
        else if (t && !strcmp(t, "off"))
            h89debug_trace_enable(d, 0);
        else if (t && !strcmp(t, "clear"))
            h89debug_trace_clear(d);
        else
        {
            h89debug_trace tr[64];
            int n;
            w = 16;
            if (t && eval(d, &o, t, &w))
                return o.len;
            if (w > 64)
                w = 64;
            n = h89debug_trace_read(d, tr, (int)w);
            if (!n)
                out(&o, "trace is %s and empty\n", h89debug_trace_enabled(d) ? "on" : "off");
            for (int i = n - 1; i >= 0; i--)
            {
                h89debug_line l;
                int pcl;
                h89debug_disassemble(d, tr[i].pc, &l, 1, &pcl);
                out(&o, "%12llu  %-8s %-24s AF=%04X BC=%04X DE=%04X HL=%04X SP=%04X\n",
                    (unsigned long long)tr[i].cycles, l.addr_text, l.disasm, tr[i].af,
                    tr[i].bc, tr[i].de, tr[i].hl, tr[i].sp);
            }
        }
    }
    else if (!strcmp(cmd, "save"))
    {
        char *k = next_tok(&p);
        char *f = rest_of(p);
        char msg[300];
        if (!k || !*f)
            out(&o, "usage: save <dis|ram|regs> <file>\n");
        else
        {
            h89debug_save(d, k, f, msg, sizeof msg);
            out(&o, "%s\n", msg);
        }
    }
    else
        out(&o, "unknown command '%s' (try help)\n", cmd);

    return o.len;
}

int h89debug_completions(h89debug *d, const char *prefix, char *dst, int dstsz)
{
    outbuf o = { dst, dstsz, 0 };
    const char *word;
    size_t n;
    int count = 0;
    const int first_word = !strchr(prefix ? prefix : "", ' ');

    if (!d || !dst || dstsz <= 0)
        return 0;
    dst[0] = '\0';
    if (!prefix)
        prefix = "";
    word = strrchr(prefix, ' ');
    word = word ? word + 1 : prefix;
    n = strlen(word);

    if (first_word)
    {
        for (int i = 0; commands[i]; i++)
            if (!strncmp(commands[i], word, n))
            {
                out(&o, "%s\n", commands[i]);
                count++;
            }
        return count;
    }
    if (!strncmp(prefix, "save ", 5) && !strchr(prefix + 5, ' '))
    {
        static const char *const kinds[] = { "dis", "ram", "regs", NULL };
        for (int i = 0; kinds[i]; i++)
            if (!strncmp(kinds[i], word, n))
            {
                out(&o, "%s\n", kinds[i]);
                count++;
            }
        return count;
    }
    for (int i = 0; h89debug_reg_names[i]; i++)
        if (n && !strncmp(h89debug_reg_names[i], word, n))
        {
            out(&o, "%s\n", h89debug_reg_names[i]);
            count++;
        }
    {
        symtab *t = h89debug_symbols(d);
        const char *name;
        for (int i = 0; (name = symtab_name_at(t, i)) != NULL && count < 200; i++)
            if (n && !strncasecmp(name, word, n))
            {
                out(&o, "%s\n", name);
                count++;
            }
    }
    return count;
}
