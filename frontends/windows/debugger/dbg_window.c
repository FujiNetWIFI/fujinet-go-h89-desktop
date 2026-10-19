/*
 * dbg_window.c -- the debugger (Win32) over the session's engine,
 * core/include/h89debug.h: the GNOME and KDE windows' toolbar and tabs.
 *
 * Tabs: the Prompt (the engine's commands, Tab completion, a history), the
 * CPU and memory (registers set by name, any radix), the disassembly
 * (double-click a line for a breakpoint), breakpoints, the H89's parts as
 * the engine reports them, and FujiNet (the FujiBus packets on the card,
 * both ways, beside the card's state). F5 run/stop, F7 step, F8 over,
 * Shift+F8 out, F12 close. Showing it attaches the engine (which stops the
 * machine); hiding it detaches. The FujiNet trace runs from the session's
 * start, so its tab shows what happened before the window opened.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dbg_window.h"

#include <windows.h>
#include <commctrl.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "h89debug.h"
#include "../resource.h"
#include "../win_common.h"

#define NTABS 6
#define TAB_FUJINET 5
#define DIS_ROWS 48
#define TRACE_MAX 512
#define IDT_POLL 1
#define U(v) h89w_px(v)

enum {
    ID_RUN = 100, ID_STEP, ID_OVER, ID_OUT, ID_TICK, ID_RADIX, ID_SYMBOLS, ID_SAVE_DIS, ID_SAVE_RAM, ID_SAVE_REGS,
    ID_PROMPT_OUT = 200, ID_PROMPT_IN,
    ID_REGS = 300, ID_MEM_GOTO, ID_MEM, ID_REG_NAME, ID_REG_VAL, ID_REG_SET,
    ID_DIS = 400, ID_FOLLOW, ID_JUMP,
    ID_BPS = 500, ID_BP_TYPE, ID_BP_START, ID_BP_END, ID_BP_COND, ID_BP_ADD, ID_BP_DEL, ID_BP_CLEAR,
    ID_H89 = 600,
    ID_FUJI_CLEAR = 700, ID_FUJI_TEXT, ID_FUJI_STATE,
};

static HWND g_dw, g_tab, g_page[NTABS], g_status, g_run, g_radix;
static HWND g_out, g_in, g_regs, g_mem_goto, g_mem, g_reg_name, g_reg_val;
static HWND g_dis, g_follow, g_jump, g_bps, g_bp_type, g_bp_start, g_bp_end, g_bp_cond, g_h89;
static HWND g_fuji_title, g_fuji_note, g_fuji_text, g_state_title, g_fuji_state;
static uint64_t g_fuji_last;
static int g_fuji_count = -1;
static HWND g_focus;
static h89session *g_s;
static h89debug *g_d;
static unsigned g_seen;
static int g_was_stopped, g_running_ticks;
static int g_mem_base = 0x2000, g_dis_top;
static uint16_t g_line_addr[DIS_ROWS];
static int g_line_count;
static char *g_history[64];
static int g_nhist, g_hist_pos;

static const struct { const char *name; int reg; } kRegs[] = {
    { "PC", H89_REG_PC }, { "SP", H89_REG_SP }, { "AF", H89_REG_AF }, { "BC", H89_REG_BC },
    { "DE", H89_REG_DE }, { "HL", H89_REG_HL }, { "IX", H89_REG_IX }, { "IY", H89_REG_IY },
    { "AF'", H89_REG_AF2 }, { "BC'", H89_REG_BC2 }, { "DE'", H89_REG_DE2 }, { "HL'", H89_REG_HL2 },
    { "A", H89_REG_A }, { "B", H89_REG_B }, { "C", H89_REG_C }, { "D", H89_REG_D }, { "E", H89_REG_E },
    { "H", H89_REG_H }, { "L", H89_REG_L }, { "I", H89_REG_I }, { "R", H89_REG_R }, { "IM", H89_REG_IM },
    { "IFF1", H89_REG_IFF1 }, { "IFF2", H89_REG_IFF2 },
};

/* ---- helpers ------------------------------------------------------------------------- */

/* "\n" to "\r\n" for an EDIT control */
static void set_edit(HWND e, const char *text)
{
    size_t n = strlen(text), o = 0;
    char *buf = malloc(n * 2 + 1);
    if (!buf)
        return;
    for (size_t i = 0; i < n; i++) {
        if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r'))
            buf[o++] = '\r';
        buf[o++] = text[i];
    }
    buf[o] = '\0';
    {
        int first = (int)SendMessageA(e, EM_GETFIRSTVISIBLELINE, 0, 0);
        SetWindowTextA(e, buf);
        SendMessageA(e, EM_LINESCROLL, 0, first);
    }
    free(buf);
}

static void append_out(const char *text)
{
    size_t n = strlen(text), o = 0;
    char *buf = malloc(n * 2 + 3);
    int len;
    if (!buf)
        return;
    for (size_t i = 0; i < n; i++) {
        if (text[i] == '\n')
            buf[o++] = '\r';
        buf[o++] = text[i];
    }
    if (o && buf[o - 1] != '\n') {
        buf[o++] = '\r';
        buf[o++] = '\n';
    }
    buf[o] = '\0';
    len = GetWindowTextLengthA(g_out);
    SendMessageA(g_out, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageA(g_out, EM_REPLACESEL, FALSE, (LPARAM)buf);
    free(buf);
}

/* EDIT controls want CR LF and draw control characters as boxes: engine
 * text (UTF-8) as the wide text a view shows. */
static wchar_t *wide_view_text(const char *text)
{
    size_t n = strlen(text), o = 0;
    char *buf = malloc(n * 2 + 1);
    wchar_t *w = NULL;
    int wn;
    if (!buf)
        return NULL;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\n') {
            buf[o++] = '\r';
            buf[o++] = '\n';
        } else if (c >= 0x20 || c == '\t') {
            buf[o++] = (char)c;
        }
    }
    buf[o] = '\0';
    wn = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
    if (wn > 0 && (w = malloc((size_t)wn * sizeof *w)) != NULL)
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, w, wn);
    free(buf);
    return w;
}

/* A label's UTF-8 text, set only when it changed (no flicker). */
static void set_label(HWND h, const char *utf8)
{
    wchar_t cur[512], w[512];
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, 512) <= 0)
        w[0] = 0;
    w[511] = 0;
    GetWindowTextW(h, cur, 512);
    if (wcscmp(cur, w) != 0)
        SetWindowTextW(h, w);
}

/* The rows an EDIT view in the monospaced font shows. */
static int edit_rows(HWND h)
{
    static int line_h;
    RECT c;
    if (!line_h) {
        HDC dc = GetDC(h);
        HGDIOBJ prev = SelectObject(dc, h89w_mono_font());
        TEXTMETRICA tm;
        line_h = GetTextMetricsA(dc, &tm) && tm.tmHeight > 0 ? tm.tmHeight : U(16);
        SelectObject(dc, prev);
        ReleaseDC(h, dc);
    }
    GetClientRect(h, &c);
    return c.bottom > line_h ? c.bottom / line_h : 1;
}

/* A log (the FujiNet trace) follows its end while its reader is there. */
static int edit_at_end(HWND h)
{
    const int first = (int)SendMessageW(h, EM_GETFIRSTVISIBLELINE, 0, 0);
    const int count = (int)SendMessageW(h, EM_GETLINECOUNT, 0, 0);
    return first + edit_rows(h) >= count;
}

/* Replaces a view's text where the reader left it (a refresh must not throw
 * a scrolled view back to its top), or, following, at its end. */
static void set_text_at(HWND h, const char *text, int follow)
{
    const int first = (int)SendMessageW(h, EM_GETFIRSTVISIBLELINE, 0, 0);
    wchar_t *w = wide_view_text(text);
    if (!w)
        return;
    SendMessageW(h, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(h, w);
    SendMessageW(h, EM_LINESCROLL, 0, follow ? SendMessageW(h, EM_GETLINECOUNT, 0, 0) : first);
    SendMessageW(h, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(h, NULL, TRUE);
    free(w);
}

static size_t addf(char *s, size_t len, size_t cap, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (len >= cap)
        return len;
    va_start(ap, fmt);
    n = vsnprintf(s + len, cap - len, fmt, ap);
    va_end(ap);
    if (n < 0)
        return len;
    return len + (size_t)n < cap ? len + (size_t)n : cap - 1;
}

static int parse(HWND e, long *out)
{
    char text[256], err[96];
    GetWindowTextA(e, text, sizeof text);
    if (!text[0])
        return 0;
    return h89debug_eval(g_d, text, out, err, sizeof err) == 0;
}

/* ---- refresh --------------------------------------------------------------------------- */

static void refresh_status(void)
{
    char reason[160], text[200];
    int addr;
    int stopped = h89debug_is_stopped(g_d);
    h89debug_stop_reason(g_d, reason, sizeof reason, &addr);
    snprintf(text, sizeof text, "%s%s", stopped ? "Stopped: " : "",
             stopped ? reason : (h89debug_is_attached(g_d) ? "Running" : "The machine is off"));
    SetWindowTextA(g_status, text);
    SetWindowTextA(g_run, stopped ? "Run (F5)" : "Stop (F5)");
}

static void refresh_cpu(void)
{
    static char text[16384];
    char regs[2048];
    int len, per = h89debug_radix(g_d) == H89_RADIX_HEX ? 16 : 8;
    h89debug_command(g_d, "regs", regs, sizeof regs);
    set_edit(g_regs, regs);
    len = 0;
    for (int r = 0; r < 32; r++) {
        uint8_t row[16];
        char at[16], v[8];
        uint16_t a = (uint16_t)(g_mem_base + r * per);
        h89debug_read(g_d, a, row, per);
        len += snprintf(text + len, sizeof text - len, "%-8s ", h89debug_fmt16(g_d, a, at, sizeof at));
        for (int i = 0; i < per; i++)
            len += snprintf(text + len, sizeof text - len, "%s ", h89debug_fmt8(g_d, row[i], v, sizeof v));
        len += snprintf(text + len, sizeof text - len, " ");
        for (int i = 0; i < per; i++)
            len += snprintf(text + len, sizeof text - len, "%c", row[i] >= 0x20 && row[i] < 0x7f ? row[i] : '.');
        len += snprintf(text + len, sizeof text - len, "\n");
    }
    set_edit(g_mem, text);
}

static void refresh_dis(void)
{
    static h89debug_line lines[DIS_ROWS];
    int pc_line = -1, n;
    if (SendMessageA(g_follow, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        h89debug_cpu c;
        h89debug_cpu_get(g_d, &c);
        g_dis_top = h89debug_row_address(g_d, (uint16_t)c.pc, -(DIS_ROWS / 3));
    }
    n = h89debug_disassemble(g_d, (uint16_t)g_dis_top, lines, DIS_ROWS, &pc_line);
    g_line_count = n;
    SendMessageA(g_dis, WM_SETREDRAW, FALSE, 0);
    SendMessageA(g_dis, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < n; i++) {
        char text[300];
        g_line_addr[i] = lines[i].address;
        snprintf(text, sizeof text, "%c%c %-8s %-16s %-14s %s%s%s", lines[i].has_breakpoint ? '*' : ' ',
                 lines[i].is_pc ? '>' : ' ', lines[i].addr_text, lines[i].bytes, lines[i].label, lines[i].disasm,
                 lines[i].comment[0] ? "  ; " : "", lines[i].comment);
        SendMessageA(g_dis, LB_ADDSTRING, 0, (LPARAM)text);
    }
    if (pc_line >= 0)
        SendMessageA(g_dis, LB_SETCURSEL, (WPARAM)pc_line, 0);
    SendMessageA(g_dis, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_dis, NULL, TRUE);
}

static void refresh_bps(void)
{
    h89debug_breakpoint bps[128];
    int n = h89debug_breakpoint_list(g_d, bps, 128);
    SendMessageA(g_bps, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < n; i++) {
        char a[16], b[16], range[40], text[320];
        int port = (bps[i].type & (H89DEBUG_BP_IN | H89DEBUG_BP_OUT)) != 0;
        const char *type = (bps[i].type & H89DEBUG_BP_EXEC) ? "Execute"
                         : (bps[i].type & H89DEBUG_BP_IN) ? "Port In"
                         : (bps[i].type & H89DEBUG_BP_OUT) ? "Port Out"
                         : ((bps[i].type & H89DEBUG_BP_READ) && (bps[i].type & H89DEBUG_BP_WRITE)) ? "Read/Write"
                         : (bps[i].type & H89DEBUG_BP_READ) ? "Read" : "Write";
        if (port) {
            h89debug_fmt8(g_d, bps[i].start, a, sizeof a);
            h89debug_fmt8(g_d, bps[i].end, b, sizeof b);
        } else {
            h89debug_fmt16(g_d, bps[i].start, a, sizeof a);
            h89debug_fmt16(g_d, bps[i].end, b, sizeof b);
        }
        if (bps[i].start == bps[i].end)
            snprintf(range, sizeof range, "%s", a);
        else
            snprintf(range, sizeof range, "%s-%s", a, b);
        snprintf(text, sizeof text, "#%-3d %-10s %-16s hits %-6u %s%s%s", bps[i].id, type, range, bps[i].hits,
                 bps[i].enabled ? "" : "(disabled) ", bps[i].condition[0] ? "if " : "", bps[i].condition);
        SendMessageA(g_bps, LB_ADDSTRING, 0, (LPARAM)text);
        SendMessageA(g_bps, LB_SETITEMDATA, (WPARAM)i, (LPARAM)bps[i].id);
    }
}

static void refresh_h89(void)
{
    static char text[16384], part[4096];
    int len = 0;
    for (int s = 0; s < H89DEBUG_SECTION_COUNT; s++) {
        h89debug_section(g_d, s, part, sizeof part);
        len += snprintf(text + len, sizeof text - len, "%s%s\n%s", s ? "\n" : "", h89debug_section_name(s), part);
    }
    set_edit(g_h89, text);
}

static void refresh_fujinet(int force)
{
    static h89_fuji_packet p[TRACE_MAX];
    static char t[TRACE_MAX * 320 + 128];
    static char state[4096];
    const int link = h89session_card_link(g_s);
    size_t len = 0;
    int n = h89debug_fujinet_trace(g_d, p, TRACE_MAX), follow;

    if (!h89session_fujinet_running(g_s) && link != 2)
        set_label(g_fuji_note, "FujiNet is off (Preferences \xe2\x96\xb8 FujiNet).");
    else
        set_label(g_fuji_note, link < 0 ? "No FujiNet card" : h89session_card_link_text(link));
    state[0] = '\0';
    h89debug_fujinet_state(g_d, state, sizeof state);
    set_text_at(g_fuji_state, state, 0);

    if (!force && n == g_fuji_count && (n == 0 || p[n - 1].serial == g_fuji_last))
        return;
    follow = force || g_fuji_count < 0 || edit_at_end(g_fuji_text);
    g_fuji_count = n;
    g_fuji_last = n > 0 ? p[n - 1].serial : 0;
    t[0] = '\0';
    for (int i = 0; i < n; i++) {
        len = addf(t, len, sizeof t, "%s%s\n     ", i ? "\n" : "", p[i].text);
        for (int k = 0; k < p[i].nbytes && k < (int)sizeof p[i].data; k++)
            len = addf(t, len, sizeof t, "%02X ", p[i].data[k]);
        if (p[i].nbytes > (int)sizeof p[i].data)
            len = addf(t, len, sizeof t, "\xe2\x80\xa6");
    }
    if (n == 0)
        len = addf(t, len, sizeof t, "(no FujiBus packets since the trace was cleared)");
    set_text_at(g_fuji_text, t, follow);
}

static void refresh_all(void)
{
    refresh_status();
    refresh_cpu();
    refresh_dis();
    refresh_bps();
    refresh_h89();
    if (TabCtrl_GetCurSel(g_tab) == TAB_FUJINET)
        refresh_fujinet(0);
}

static void attach(void)
{
    if (h89session_is_running(g_s))
        h89debug_attach(g_d);
    g_seen = h89debug_generation(g_d) - 1;
    refresh_all();
}

static void poll(void)
{
    unsigned gen;
    int stopped;
    if (!IsWindowVisible(g_dw))
        return;
    if (!h89debug_is_attached(g_d) && h89session_is_running(g_s)) {
        attach();
        return;
    }
    gen = h89debug_generation(g_d);
    stopped = h89debug_is_stopped(g_d);
    if (gen != g_seen || stopped != g_was_stopped) {
        g_seen = gen;
        g_was_stopped = stopped;
        refresh_all();
    } else if (!stopped && ++g_running_ticks >= 5) {
        g_running_ticks = 0;
        refresh_status();
        refresh_cpu();
        refresh_h89();
        if (TabCtrl_GetCurSel(g_tab) == TAB_FUJINET)
            refresh_fujinet(0);
    }
}

/* ---- actions ---------------------------------------------------------------------------- */

static void run_prompt(void)
{
    static char out[65536];
    char cmd[512], line[600];
    GetWindowTextA(g_in, cmd, sizeof cmd);
    if (!cmd[0])
        return;
    if (g_nhist == 0 || strcmp(g_history[g_nhist - 1], cmd) != 0) {
        if (g_nhist == 64) {
            free(g_history[0]);
            memmove(g_history, g_history + 1, sizeof g_history[0] * 63);
            g_nhist--;
        }
        g_history[g_nhist++] = _strdup(cmd);
    }
    g_hist_pos = g_nhist;
    snprintf(line, sizeof line, "> %s", cmd);
    append_out(line);
    h89debug_command(g_d, cmd, out, sizeof out);
    if (out[0])
        append_out(out);
    SetWindowTextA(g_in, "");
    if ((int)SendMessageA(g_radix, CB_GETCURSEL, 0, 0) != h89debug_radix(g_d))
        SendMessageA(g_radix, CB_SETCURSEL, (WPARAM)h89debug_radix(g_d), 0);
    refresh_all();
}

static void complete(void)
{
    char text[512], comps[4096];
    int n;
    const char *word;
    GetWindowTextA(g_in, text, sizeof text);
    word = strrchr(text, ' ');
    word = word ? word + 1 : text;
    n = h89debug_completions(g_d, text, comps, sizeof comps);
    if (n == 1) {
        char merged[700];
        char *nl = strchr(comps, '\n');
        if (nl)
            *nl = '\0';
        snprintf(merged, sizeof merged, "%.*s%.150s ", (int)(word - text), text, comps);
        SetWindowTextA(g_in, merged);
        SendMessageA(g_in, EM_SETSEL, (WPARAM)strlen(merged), (LPARAM)strlen(merged));
    } else if (n > 1) {
        append_out(comps);
    }
}

static void say(const char *msg)
{
    append_out(msg);
    SetWindowTextA(g_status, msg);
}

static void save(const char *kind, const char *file)
{
    char path[MAX_PATH], msg[512];
    OPENFILENAMEA ofn;
    snprintf(path, sizeof path, "%s", file);
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_dw;
    ofn.lpstrFilter = "All files\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof path;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameA(&ofn))
        return;
    h89debug_save(g_d, kind, path, msg, sizeof msg);
    say(msg);
}

static void hide_window(void)
{
    h89debug_detach(g_d);
    ShowWindow(g_dw, SW_HIDE);
}

static void on_command(int id, int notify)
{
    long a, b;
    switch (id) {
    case ID_RUN:
        if (h89debug_is_stopped(g_d)) h89debug_resume(g_d); else h89debug_stop(g_d);
        break;
    case ID_STEP: h89debug_step(g_d); break;
    case ID_OVER: h89debug_step_over(g_d); break;
    case ID_OUT: h89debug_step_out(g_d); break;
    case ID_TICK: h89debug_ticks(g_d, 1); break;
    case ID_RADIX:
        if (notify != CBN_SELCHANGE)
            return;
        h89debug_set_radix(g_d, (int)SendMessageA(g_radix, CB_GETCURSEL, 0, 0));
        h89session_set_int(g_s, "debugger.radix", h89debug_radix(g_d));
        break;
    case ID_SYMBOLS: {
        char path[MAX_PATH] = "", msg[512];
        OPENFILENAMEA ofn;
        memset(&ofn, 0, sizeof ofn);
        ofn.lStructSize = sizeof ofn;
        ofn.hwndOwner = g_dw;
        ofn.lpstrFilter = "Symbol files (*.map;*.sym;*.noi;*.txt)\0*.map;*.sym;*.noi;*.txt\0All files\0*.*\0\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = sizeof path;
        ofn.lpstrTitle = "Load Symbols";
        ofn.Flags = OFN_FILEMUSTEXIST;
        if (!GetOpenFileNameA(&ofn))
            return;
        h89debug_load_symbols(g_d, path, msg, sizeof msg);
        say(msg);
        break;
    }
    case ID_SAVE_DIS: save("dis", "disassembly.asm"); return;
    case ID_SAVE_RAM: save("ram", "memory.bin"); return;
    case ID_SAVE_REGS: save("regs", "h89-state.txt"); return;
    case ID_REG_SET: {
        int r = (int)SendMessageA(g_reg_name, CB_GETCURSEL, 0, 0);
        if (r >= 0 && parse(g_reg_val, &a))
            h89debug_cpu_set(g_d, kRegs[r].reg, (int)a);
        break;
    }
    case ID_DIS:
        if (notify == LBN_DBLCLK) {
            int line = (int)SendMessageA(g_dis, LB_GETCURSEL, 0, 0);
            if (line >= 0 && line < g_line_count)
                h89debug_breakpoint_toggle(g_d, g_line_addr[line]);
            break;
        }
        return;
    case ID_FOLLOW: break;
    case ID_BP_ADD: {
        static const int types[6] = { H89DEBUG_BP_EXEC, H89DEBUG_BP_READ, H89DEBUG_BP_WRITE,
                                      H89DEBUG_BP_READ | H89DEBUG_BP_WRITE, H89DEBUG_BP_IN, H89DEBUG_BP_OUT };
        char cond[256];
        int t = (int)SendMessageA(g_bp_type, CB_GETCURSEL, 0, 0);
        if (!parse(g_bp_start, &a)) {
            say("The start is not an address, a label or an expression");
            return;
        }
        b = a;
        if (GetWindowTextLengthA(g_bp_end) > 0 && !parse(g_bp_end, &b)) {
            say("The end is not an address");
            return;
        }
        GetWindowTextA(g_bp_cond, cond, sizeof cond);
        if (h89debug_breakpoint_add(g_d, types[t < 0 ? 0 : t], (uint16_t)a, (uint16_t)b, cond) < 0) {
            say("The condition does not parse");
            return;
        }
        SetWindowTextA(g_bp_start, "");
        SetWindowTextA(g_bp_end, "");
        SetWindowTextA(g_bp_cond, "");
        break;
    }
    case ID_BP_DEL: {
        int sel = (int)SendMessageA(g_bps, LB_GETCURSEL, 0, 0);
        if (sel >= 0)
            h89debug_breakpoint_remove(g_d, (int)SendMessageA(g_bps, LB_GETITEMDATA, (WPARAM)sel, 0));
        break;
    }
    case ID_BP_CLEAR: h89debug_breakpoint_clear(g_d); break;
    case ID_FUJI_CLEAR:
        h89debug_fujinet_trace_clear(g_d);
        refresh_fujinet(1);
        return;
    default:
        return;
    }
    refresh_all();
}

/* the Enter keys of the address boxes */
static void on_enter(HWND from)
{
    long a;
    if (from == g_in) {
        run_prompt();
    } else if (from == g_mem_goto) {
        if (parse(g_mem_goto, &a))
            g_mem_base = (int)(a & 0xfff0);
        refresh_cpu();
    } else if (from == g_jump) {
        if (parse(g_jump, &a)) {
            SendMessageA(g_follow, BM_SETCHECK, BST_UNCHECKED, 0);
            g_dis_top = (int)(a & 0xffff);
            refresh_dis();
        }
    } else if (from == g_reg_val) {
        on_command(ID_REG_SET, 0);
    } else if (from == g_bp_cond || from == g_bp_start || from == g_bp_end) {
        on_command(ID_BP_ADD, 0);
    }
}

int h89_debugger_pretranslate(MSG *msg)
{
    if (!g_dw || !IsWindowVisible(g_dw) || (msg->hwnd != g_dw && !IsChild(g_dw, msg->hwnd)))
        return 0;
    if (msg->message != WM_KEYDOWN)
        return 0;
    switch (msg->wParam) {
    case VK_F5: on_command(ID_RUN, 0); return 1;
    case VK_F7: on_command(ID_STEP, 0); return 1;
    case VK_F8: on_command((GetKeyState(VK_SHIFT) & 0x8000) ? ID_OUT : ID_OVER, 0); return 1;
    case VK_F12: hide_window(); return 1;
    case VK_RETURN:
        if (GetClassNameA(msg->hwnd, (char[16]){ 0 }, 16) && msg->hwnd != g_out) {
            on_enter(msg->hwnd);
            return 1;
        }
        return 0;
    case VK_TAB:
        if (msg->hwnd == g_in) {
            complete();
            return 1;
        }
        return 0;
    case VK_UP:
    case VK_DOWN:
        if (msg->hwnd == g_in) {
            if (msg->wParam == VK_UP && g_hist_pos > 0)
                g_hist_pos--;
            else if (msg->wParam == VK_DOWN && g_hist_pos < g_nhist)
                g_hist_pos++;
            SetWindowTextA(g_in, g_hist_pos < g_nhist ? g_history[g_hist_pos] : "");
            SendMessageA(g_in, EM_SETSEL, 0x7fff, 0x7fff);
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

/* ---- layout ------------------------------------------------------------------------------- */

static LRESULT CALLBACK page_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND) {
        on_command(LOWORD(wp), HIWORD(wp));
        return 0;
    }
    if (msg == WM_CTLCOLORSTATIC) {
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static HWND ctl(HWND parent, const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id,
                HFONT font)
{
    HWND c = CreateWindowExA(!strcmp(cls, "EDIT") || !strcmp(cls, "LISTBOX") ? WS_EX_CLIENTEDGE : 0, cls, text,
                             WS_CHILD | WS_VISIBLE | style, U(x), U(y), U(w), U(h), parent, (HMENU)(INT_PTR)id,
                             NULL, NULL);
    h89w_set_font(c, font);
    return c;
}

static void layout(void)
{
    RECT rc, tr;
    int w, h;
    GetClientRect(g_dw, &rc);
    w = rc.right;
    h = rc.bottom;
    MoveWindow(g_status, U(408), U(48), w > U(420) ? w - U(416) : U(10), U(18), TRUE);
    MoveWindow(g_tab, U(8), U(76), w - U(16), h - U(84), TRUE);
    GetClientRect(g_tab, &tr);
    TabCtrl_AdjustRect(g_tab, FALSE, &tr);
    for (int i = 0; i < NTABS; i++)
        MoveWindow(g_page[i], U(8) + tr.left, U(76) + tr.top, tr.right - tr.left, tr.bottom - tr.top, TRUE);
    w = tr.right - tr.left;
    h = tr.bottom - tr.top;
    MoveWindow(g_out, U(6), U(6), w - U(12), h - U(44), TRUE);
    MoveWindow(g_in, U(6), h - U(32), w - U(12), U(26), TRUE);
    MoveWindow(g_regs, U(6), U(6), w - U(12), U(64), TRUE);
    MoveWindow(g_mem, U(6), U(112), w - U(12), h - U(118), TRUE);
    MoveWindow(g_dis, U(6), U(40), w - U(12), h - U(46), TRUE);
    MoveWindow(g_bps, U(6), U(6), w - U(12), h - U(80), TRUE);
    MoveWindow(g_h89, U(6), U(6), w - U(12), h - U(12), TRUE);
    {
        int y = h - U(66);
        MoveWindow(g_bp_type, U(6), y, U(110), U(200), TRUE);
        MoveWindow(g_bp_start, U(122), y, U(110), U(24), TRUE);
        MoveWindow(g_bp_end, U(238), y, U(110), U(24), TRUE);
        MoveWindow(g_bp_cond, U(354), y, w - U(360), U(24), TRUE);
    }
    SetWindowPos(GetDlgItem(g_page[3], ID_BP_ADD), NULL, U(6), h - U(34), U(80), U(26), SWP_NOZORDER);
    SetWindowPos(GetDlgItem(g_page[3], ID_BP_DEL), NULL, U(92), h - U(34), U(80), U(26), SWP_NOZORDER);
    SetWindowPos(GetDlgItem(g_page[3], ID_BP_CLEAR), NULL, U(178), h - U(34), U(80), U(26), SWP_NOZORDER);
    /* FujiNet: the packets left (a note and Clear over them), the card right */
    {
        const int rw = w / 3 > U(360) ? U(360) : w / 3 > U(240) ? w / 3 : U(240);
        const int lw = w - rw - U(18);
        MoveWindow(g_fuji_title, U(6), U(8), lw, U(18), TRUE);
        MoveWindow(g_fuji_note, U(6), U(36), lw - U(92), U(18), TRUE);
        SetWindowPos(GetDlgItem(g_page[TAB_FUJINET], ID_FUJI_CLEAR), NULL, U(6) + lw - U(80), U(30), U(80), U(26),
                     SWP_NOZORDER);
        MoveWindow(g_fuji_text, U(6), U(62), lw, h - U(68), TRUE);
        MoveWindow(g_state_title, U(12) + lw, U(8), rw, U(18), TRUE);
        MoveWindow(g_fuji_state, U(12) + lw, U(30), rw, h - U(36), TRUE);
    }
}

static LRESULT CALLBACK win_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (g_tab)
            layout();
        return 0;
    case WM_NOTIFY:
        if (((NMHDR *)lp)->hwndFrom == g_tab && ((NMHDR *)lp)->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(g_tab);
            for (int i = 0; i < NTABS; i++)
                ShowWindow(g_page[i], i == sel ? SW_SHOW : SW_HIDE);
            if (sel == 0)
                SetFocus(g_in);
            if (sel == TAB_FUJINET)
                refresh_fujinet(0);
        }
        return 0;
    case WM_COMMAND:
        on_command(LOWORD(wp), HIWORD(wp));
        return 0;
    case WM_TIMER:
        poll();
        return 0;
    case WM_ACTIVATE:
        /* a plain window does not remember its focused control as a dialog
         * does: keep it across deactivation, the prompt's input by default */
        if (LOWORD(wp) == WA_INACTIVE) {
            HWND f = GetFocus();
            if (f && IsChild(hwnd, f))
                g_focus = f;
        } else {
            SetFocus(g_focus && IsWindow(g_focus) && IsWindowVisible(g_focus) ? g_focus
                     : TabCtrl_GetCurSel(g_tab) == 0 ? g_in : hwnd);
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_CLOSE:
        hide_window();
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void build(HWND owner)
{
    static const char *const tabs[NTABS] = { "Prompt", "CPU && Memory", "Disassembly", "Breakpoints", "H89",
                                             "FujiNet" };
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtr(owner, GWLP_HINSTANCE);
    HFONT f = h89w_font(), m = h89w_mono_font();
    WNDCLASSA wc;
    int x = 8;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = win_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "H89Debugger";
    wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(IDI_APPICON));
    RegisterClassA(&wc);
    wc.lpfnWndProc = page_proc;
    wc.lpszClassName = "H89DebuggerPage";
    wc.hIcon = NULL;
    RegisterClassA(&wc);

    g_dw = CreateWindowA("H89Debugger", "Debugger", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, U(1000), U(760),
                         NULL, NULL, inst, NULL);
    g_run = ctl(g_dw, "BUTTON", "Stop (F5)", 0, x, 8, 90, 28, ID_RUN, f); x += 96;
    ctl(g_dw, "BUTTON", "Step (F7)", 0, x, 8, 90, 28, ID_STEP, f); x += 96;
    ctl(g_dw, "BUTTON", "Over (F8)", 0, x, 8, 90, 28, ID_OVER, f); x += 96;
    ctl(g_dw, "BUTTON", "Out (Shift+F8)", 0, x, 8, 110, 28, ID_OUT, f); x += 116;
    ctl(g_dw, "BUTTON", "Tick", 0, x, 8, 60, 28, ID_TICK, f); x += 66;
    g_radix = ctl(g_dw, "COMBOBOX", "", CBS_DROPDOWNLIST, x, 10, 110, 200, ID_RADIX, f); x += 120;
    SendMessageA(g_radix, CB_ADDSTRING, 0, (LPARAM)"Hex");
    SendMessageA(g_radix, CB_ADDSTRING, 0, (LPARAM)"Octal");
    SendMessageA(g_radix, CB_ADDSTRING, 0, (LPARAM)"Split octal");
    SendMessageA(g_radix, CB_SETCURSEL, (WPARAM)h89debug_radix(g_d), 0);
    /* the files on a second row, the state beside them */
    x = 8;
    ctl(g_dw, "BUTTON", "Symbols...", 0, x, 42, 84, 28, ID_SYMBOLS, f); x += 90;
    ctl(g_dw, "BUTTON", "Save Dis...", 0, x, 42, 84, 28, ID_SAVE_DIS, f); x += 90;
    ctl(g_dw, "BUTTON", "Save RAM...", 0, x, 42, 84, 28, ID_SAVE_RAM, f); x += 90;
    ctl(g_dw, "BUTTON", "Save State...", 0, x, 42, 90, 28, ID_SAVE_REGS, f); x += 100;
    g_status = ctl(g_dw, "STATIC", "", SS_ENDELLIPSIS, x, 48, 600, 18, 0, f);

    g_tab = ctl(g_dw, WC_TABCONTROLA, "", WS_CLIPSIBLINGS, 8, 76, 980, 660, 0, f);
    for (int i = 0; i < NTABS; i++) {
        TCITEMA item;
        memset(&item, 0, sizeof item);
        item.mask = TCIF_TEXT;
        item.pszText = (char *)tabs[i];
        TabCtrl_InsertItem(g_tab, i, &item);
        g_page[i] = CreateWindowA("H89DebuggerPage", "", WS_CHILD, 0, 0, 10, 10, g_dw, NULL, inst, NULL);
    }
    /* Prompt */
    g_out = ctl(g_page[0], "EDIT", "FujiNet Go H89 debugger. Type help for the commands.\r\n",
                ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 0, 0, 10, 10, ID_PROMPT_OUT, m);
    g_in = ctl(g_page[0], "EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, 0, 0, 10, 10, ID_PROMPT_IN, m);
    /* CPU & Memory */
    g_regs = ctl(g_page[1], "EDIT", "", ES_MULTILINE | ES_READONLY, 0, 0, 10, 10, ID_REGS, m);
    ctl(g_page[1], "STATIC", "Memory at", 0, 6, 80, 70, 18, 0, f);
    g_mem_goto = ctl(g_page[1], "EDIT", "", ES_AUTOHSCROLL, 80, 76, 160, 24, ID_MEM_GOTO, f);
    ctl(g_page[1], "STATIC", "Set", 0, 260, 80, 30, 18, 0, f);
    g_reg_name = ctl(g_page[1], "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, 290, 76, 70, 300, ID_REG_NAME, f);
    for (unsigned i = 0; i < sizeof kRegs / sizeof kRegs[0]; i++)
        SendMessageA(g_reg_name, CB_ADDSTRING, 0, (LPARAM)kRegs[i].name);
    SendMessageA(g_reg_name, CB_SETCURSEL, 0, 0);
    g_reg_val = ctl(g_page[1], "EDIT", "", ES_AUTOHSCROLL, 366, 76, 120, 24, ID_REG_VAL, f);
    ctl(g_page[1], "BUTTON", "Set", 0, 492, 75, 60, 26, ID_REG_SET, f);
    g_mem = ctl(g_page[1], "EDIT", "", ES_MULTILINE | ES_READONLY | WS_VSCROLL, 0, 0, 10, 10, ID_MEM, m);
    /* Disassembly */
    g_follow = ctl(g_page[2], "BUTTON", "Follow PC", BS_AUTOCHECKBOX, 6, 8, 100, 22, ID_FOLLOW, f);
    SendMessageA(g_follow, BM_SETCHECK, BST_CHECKED, 0);
    g_jump = ctl(g_page[2], "EDIT", "", ES_AUTOHSCROLL, 112, 6, 200, 24, ID_JUMP, f);
    ctl(g_page[2], "STATIC", "Enter goes there; double-click a line to toggle its breakpoint", 0, 320, 10, 500, 18,
        0, f);
    g_dis = ctl(g_page[2], "LISTBOX", "", LBS_NOTIFY | WS_VSCROLL, 0, 0, 10, 10, ID_DIS, m);
    /* Breakpoints */
    g_bps = ctl(g_page[3], "LISTBOX", "", LBS_NOTIFY | WS_VSCROLL, 0, 0, 10, 10, ID_BPS, m);
    g_bp_type = ctl(g_page[3], "COMBOBOX", "", CBS_DROPDOWNLIST, 0, 0, 10, 10, ID_BP_TYPE, f);
    {
        static const char *const types[] = { "Execute", "Read", "Write", "Read/Write", "Port In", "Port Out" };
        for (int i = 0; i < 6; i++)
            SendMessageA(g_bp_type, CB_ADDSTRING, 0, (LPARAM)types[i]);
        SendMessageA(g_bp_type, CB_SETCURSEL, 0, 0);
    }
    g_bp_start = ctl(g_page[3], "EDIT", "", ES_AUTOHSCROLL, 0, 0, 10, 10, ID_BP_START, f);
    g_bp_end = ctl(g_page[3], "EDIT", "", ES_AUTOHSCROLL, 0, 0, 10, 10, ID_BP_END, f);
    g_bp_cond = ctl(g_page[3], "EDIT", "", ES_AUTOHSCROLL, 0, 0, 10, 10, ID_BP_COND, f);
    SendMessageA(g_bp_start, EM_SETCUEBANNER, 0, (LPARAM)L"start");
    SendMessageA(g_bp_end, EM_SETCUEBANNER, 0, (LPARAM)L"end (optional)");
    SendMessageA(g_bp_cond, EM_SETCUEBANNER, 0, (LPARAM)L"condition, e.g. a == 0");
    ctl(g_page[3], "BUTTON", "Add", 0, 0, 0, 10, 10, ID_BP_ADD, f);
    ctl(g_page[3], "BUTTON", "Remove", 0, 0, 0, 10, 10, ID_BP_DEL, f);
    ctl(g_page[3], "BUTTON", "Clear All", 0, 0, 0, 10, 10, ID_BP_CLEAR, f);
    /* H89 */
    g_h89 = ctl(g_page[4], "EDIT", "", ES_MULTILINE | ES_READONLY | WS_VSCROLL, 0, 0, 10, 10, ID_H89, m);
    /* FujiNet */
    g_fuji_title = ctl(g_page[5], "STATIC", "FujiBus packets (SLIP-decoded)", SS_NOPREFIX, 0, 0, 10, 10, 0, f);
    g_fuji_note = ctl(g_page[5], "STATIC", "", SS_NOPREFIX | SS_ENDELLIPSIS, 0, 0, 10, 10, 0, f);
    ctl(g_page[5], "BUTTON", "Clear", 0, 0, 0, 10, 10, ID_FUJI_CLEAR, f);
    g_fuji_text = ctl(g_page[5], "EDIT", "", ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL,
                      0, 0, 10, 10, ID_FUJI_TEXT, m);
    SendMessageA(g_fuji_text, EM_SETLIMITTEXT, 0, 0);
    g_state_title = ctl(g_page[5], "STATIC", "FujiNet card (82C55)", SS_NOPREFIX, 0, 0, 10, 10, 0, f);
    g_fuji_state = ctl(g_page[5], "EDIT", "", ES_MULTILINE | ES_READONLY | WS_VSCROLL, 0, 0, 10, 10, ID_FUJI_STATE, m);

    {
        const char *tab = getenv("H89_DEBUGGER_TAB");
        static const char *const keys[NTABS] = { "prompt", "cpu", "disassembly", "breakpoints", "h89", "fujinet" };
        int first = 0;
        for (int i = 0; tab && i < NTABS; i++)
            if (!_stricmp(tab, keys[i]))
                first = i;
        TabCtrl_SetCurSel(g_tab, first);
        ShowWindow(g_page[first], SW_SHOW);
        if (first == 0)
            SetFocus(g_in);
    }
    layout();
    SetTimer(g_dw, IDT_POLL, 100, NULL);
}

void h89_debugger_show(HWND owner, h89session *session)
{
    g_s = session;
    g_d = h89session_debugger(session);
    if (!g_dw)
        build(owner);
    ShowWindow(g_dw, SW_SHOW);
    SetForegroundWindow(g_dw);
    if (TabCtrl_GetCurSel(g_tab) == 0)
        SetFocus(g_in);
    attach();
}

void h89_debugger_toggle(HWND owner, h89session *session)
{
    if (g_dw && IsWindowVisible(g_dw)) {
        hide_window();
        return;
    }
    h89_debugger_show(owner, session);
}
