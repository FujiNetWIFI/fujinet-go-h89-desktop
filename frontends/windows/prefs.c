/*
 * prefs.c -- the Settings window: the GNOME and KDE Preferences' six pages
 * on a tab control. Rows are bound to fields of a local h89_config (or to
 * settings keys for the live view and sound options) through one table, so
 * every control is handled by the same few lines. The machine is checked
 * as a whole (h89_config_validate) on every change -- the line at the top
 * says whether it can be built, and OK is only enabled when it can -- and
 * stored on OK, with a power cycle if one is asked for.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "resource.h"
#include "win_common.h"

#define NPAGES 6
#define MAX_ROWS 160
#define ID_BASE 5000
#define ID_OK 4001
#define ID_CANCEL 4002
#define ID_IMPORT 4003
#define ID_CPNET_DIR 4004
#define ID_WEBUI 4005
#define WM_REBUILD_SW501 (WM_APP + 20)

enum { K_COMBO, K_BIT, K_FLAG, K_NUMBER, K_SET_COMBO, K_SET_CHECK, K_MONITOR, K_BAUD, K_SW501 };
enum { G_NONE, G_H17, G_H37, G_MMS316, G_MMS320, G_SW501, G_FUJI };
enum { S_LIVE, S_VIEW, S_HOST };

typedef struct {
    HWND ctl, label;
    int kind, group;
    int *field;
    const int *values;
    int nvalues;
    uint8_t *byte;
    uint8_t mask;
    const char *key;
    int def, setkind;
    int sw_field;
} Binding;

static HWND g_win, g_tab, g_pages[NPAGES], g_status, g_ok, g_roms;
static h89_config g_cfg, g_orig;
static int g_host_changed;
static Binding g_rows[MAX_ROWS];
static int g_nrows;
static int g_monitors[16], g_nmon;
static HWND g_cpnet_dir;
static int g_sw501_y;
static int g_syncing;

/* ---- building rows ------------------------------------------------------------------- */

static HWND mk_label(HWND page, const char *text, int x, int y, int w)
{
    HWND l = CreateWindowA("STATIC", text, WS_CHILD | WS_VISIBLE, h89w_px(x), h89w_px(y + 4), h89w_px(w),
                           h89w_px(18), page, NULL, NULL, NULL);
    h89w_set_font(l, h89w_font());
    return l;
}

static Binding *add_row(HWND page, int kind, const char *label, int x, int y, int lw, int cw)
{
    Binding *b = &g_rows[g_nrows];
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    const char *cls = "COMBOBOX";
    memset(b, 0, sizeof *b);
    if (kind == K_BIT || kind == K_FLAG || kind == K_SET_CHECK) {
        cls = "BUTTON";
        style |= BS_AUTOCHECKBOX;
        b->ctl = CreateWindowA(cls, label, style, h89w_px(x), h89w_px(y), h89w_px(lw + cw), h89w_px(22), page,
                               (HMENU)(INT_PTR)(ID_BASE + g_nrows), NULL, NULL);
    } else if (kind == K_NUMBER) {
        b->label = mk_label(page, label, x, y, lw);
        b->ctl = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", style | ES_NUMBER, h89w_px(x + lw), h89w_px(y),
                                 h89w_px(cw), h89w_px(22), page,
                                 (HMENU)(INT_PTR)(ID_BASE + g_nrows), NULL, NULL);
    } else {
        b->label = mk_label(page, label, x, y, lw);
        b->ctl = CreateWindowA(cls, "", style | CBS_DROPDOWNLIST | WS_VSCROLL, h89w_px(x + lw), h89w_px(y),
                               h89w_px(cw), h89w_px(300), page,
                               (HMENU)(INT_PTR)(ID_BASE + g_nrows), NULL, NULL);
    }
    h89w_set_font(b->ctl, h89w_font());
    b->kind = kind;
    g_nrows++;
    return b;
}

static void fill_names(HWND c, const char *(*names)(int), const char *const *strs)
{
    for (int i = 0; names ? names(i) != NULL : strs[i] != NULL; i++)
        SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)(names ? names(i) : strs[i]));
}

static Binding *combo(HWND page, const char *label, int x, int y, int lw, int cw, int *field,
                      const char *(*names)(int), const char *const *strs, const int *values, int nvalues)
{
    Binding *b = add_row(page, K_COMBO, label, x, y, lw, cw);
    int sel = *field;
    b->field = field;
    b->values = values;
    b->nvalues = nvalues;
    fill_names(b->ctl, names, strs);
    if (values)
        for (int i = 0; i < nvalues; i++)
            if (values[i] == *field)
                sel = i;
    SendMessageA(b->ctl, CB_SETCURSEL, (WPARAM)sel, 0);
    return b;
}

static Binding *bit(HWND page, const char *label, int x, int y, int w, uint8_t *byte, uint8_t mask)
{
    Binding *b = add_row(page, K_BIT, label, x, y, w, 0);
    b->byte = byte;
    b->mask = mask;
    SendMessageA(b->ctl, BM_SETCHECK, (*byte & mask) ? BST_CHECKED : BST_UNCHECKED, 0);
    return b;
}

static Binding *flag(HWND page, const char *label, int x, int y, int w, int *field)
{
    Binding *b = add_row(page, K_FLAG, label, x, y, w, 0);
    b->field = field;
    SendMessageA(b->ctl, BM_SETCHECK, *field ? BST_CHECKED : BST_UNCHECKED, 0);
    return b;
}

static Binding *number(HWND page, const char *label, int x, int y, int lw, int *field)
{
    Binding *b = add_row(page, K_NUMBER, label, x, y, lw, 80);
    char text[16];
    b->field = field;
    snprintf(text, sizeof text, "%d", *field);
    g_syncing = 1;
    SetWindowTextA(b->ctl, text);
    g_syncing = 0;
    return b;
}

static Binding *set_check(HWND page, const char *label, int x, int y, int w, const char *key, int def, int sk)
{
    Binding *b = add_row(page, K_SET_CHECK, label, x, y, w, 0);
    b->key = key;
    b->def = def;
    b->setkind = sk;
    SendMessageA(b->ctl, BM_SETCHECK, h89session_get_int(g_session, key, def) ? BST_CHECKED : BST_UNCHECKED, 0);
    return b;
}

static Binding *set_combo(HWND page, const char *label, int x, int y, int lw, int cw, const char *key, int def,
                          const char *const *strs, int sk)
{
    Binding *b = add_row(page, K_SET_COMBO, label, x, y, lw, cw);
    b->key = key;
    b->def = def;
    b->setkind = sk;
    fill_names(b->ctl, NULL, strs);
    SendMessageA(b->ctl, CB_SETCURSEL, (WPARAM)h89session_get_int(g_session, key, def), 0);
    return b;
}

static void set_group(Binding *from, int group)
{
    for (Binding *b = from; b < g_rows + g_nrows; b++)
        b->group = group;
}

/* ---- the state of the whole ------------------------------------------------------------ */

static int has_card(int card)
{
    for (int i = 0; i < H89_SLOTS; i++)
        if (g_cfg.slot[i] == card)
            return 1;
    return 0;
}

static void show_row(Binding *b, int on)
{
    ShowWindow(b->ctl, on ? SW_SHOW : SW_HIDE);
    if (b->label)
        ShowWindow(b->label, on ? SW_SHOW : SW_HIDE);
}

static void changed(void)
{
    char why[256], text[320];
    int ok = h89_config_validate(&g_cfg, why, sizeof why);
    if (!ok)
        snprintf(text, sizeof text, "This machine cannot be built: %s", why);
    else if (memcmp(&g_cfg, &g_orig, sizeof g_cfg) == 0)
        snprintf(text, sizeof text, "Configuration: as it is now");
    else
        snprintf(text, sizeof text, "Configuration: changed -- applied by a power cycle");
    SetWindowTextA(g_status, text);
    EnableWindow(g_ok, ok);
    for (int i = 0; i < g_nrows; i++) {
        Binding *b = &g_rows[i];
        switch (b->group) {
        case G_H17: show_row(b, has_card(H89_CARD_H17)); break;
        case G_H37: show_row(b, has_card(H89_CARD_H37)); break;
        case G_MMS316: show_row(b, has_card(H89_CARD_MMS77316)); break;
        case G_MMS320: show_row(b, has_card(H89_CARD_MMS77320)); break;
        case G_FUJI: EnableWindow(b->ctl, has_card(H89_CARD_FUJINET)); break;
        default: break;
        }
    }
    PostMessageA(g_win, WM_REBUILD_SW501, 0, 0);
}

/* SW501's fields are the monitor's; with "from the slots" on, the device
 * fields show what the slots make them and cannot be changed. */
static void rebuild_sw501(void)
{
    const char *mon = g_cfg.monitor;
    uint8_t eff = h89_config_effective_sw501(&g_cfg);
    uint8_t automask = g_cfg.sw501_auto ? h89_sw501_auto_mask(mon) : 0;
    int n = h89_sw501_field_count(mon), w = 0;
    HWND page = g_pages[2];

    for (int i = 0; i < g_nrows; i++) {
        if (g_rows[i].group == G_SW501) {
            DestroyWindow(g_rows[i].ctl);
            if (g_rows[i].label)
                DestroyWindow(g_rows[i].label);
            continue;
        }
        g_rows[w++] = g_rows[i];
    }
    /* the controls keep their IDs: renumber them to their new rows */
    g_nrows = w;
    for (int i = 0; i < g_nrows; i++)
        SetWindowLongPtrA(g_rows[i].ctl, GWLP_ID, ID_BASE + i);
    g_syncing = 1;
    for (int f = 0; f < n && g_nrows < MAX_ROWS; f++) {
        Binding *b = add_row(page, K_SW501, h89_sw501_field_name(mon, f), 16, g_sw501_y + f * 28, 220, 260);
        uint8_t mask = h89_sw501_field_mask(mon, f);
        b->group = G_SW501;
        b->sw_field = f;
        for (int o = 0; o < h89_sw501_option_count(mon, f); o++) {
            SendMessageA(b->ctl, CB_ADDSTRING, 0, (LPARAM)h89_sw501_option_name(mon, f, o));
            if ((eff & mask) == h89_sw501_option_value(mon, f, o))
                SendMessageA(b->ctl, CB_SETCURSEL, (WPARAM)o, 0);
        }
        EnableWindow(b->ctl, (mask & automask) == 0);
    }
    g_syncing = 0;
    /* what the old rows covered */
    InvalidateRect(page, NULL, TRUE);
}

static void fill_monitors(Binding *b)
{
    int sel = -1;
    SendMessageA(b->ctl, CB_RESETCONTENT, 0, 0);
    g_nmon = h89session_monitors(g_session, g_monitors, 15);
    for (int i = 0; i < g_nmon; i++) {
        const h89_rom_info *r = h89_rom_at(g_monitors[i]);
        SendMessageA(b->ctl, CB_ADDSTRING, 0, (LPARAM)r->desc);
        if (!strcmp(r->file, g_cfg.monitor))
            sel = i;
    }
    if (sel < 0 && g_cfg.monitor[0]) {
        int idx = h89_rom_find_file(g_cfg.monitor);
        char text[200];
        snprintf(text, sizeof text, "%s (not imported)", idx >= 0 ? h89_rom_at(idx)->desc : g_cfg.monitor);
        SendMessageA(b->ctl, CB_ADDSTRING, 0, (LPARAM)text);
        g_monitors[g_nmon] = idx;
        sel = g_nmon++;
    }
    SendMessageA(b->ctl, CB_SETCURSEL, (WPARAM)sel, 0);
}

static void refresh_roms_text(void)
{
    char what[512], text[700];
    if (h89session_missing_roms(g_session, what, sizeof what) > 0)
        snprintf(text, sizeof text, "Still needed: %s", what);
    else
        snprintf(text, sizeof text, "Everything this machine needs is imported");
    SetWindowTextA(g_roms, text);
}

/* ---- handlers ------------------------------------------------------------------------------- */

static void on_row(Binding *b, int notify)
{
    LRESULT sel;
    if (g_syncing)
        return;
    switch (b->kind) {
    case K_COMBO:
        if (notify != CBN_SELCHANGE)
            return;
        sel = SendMessageA(b->ctl, CB_GETCURSEL, 0, 0);
        if (sel < 0)
            return;
        *b->field = b->values ? (sel < b->nvalues ? b->values[sel] : *b->field) : (int)sel;
        changed();
        return;
    case K_MONITOR:
        if (notify != CBN_SELCHANGE)
            return;
        sel = SendMessageA(b->ctl, CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < g_nmon && g_monitors[sel] >= 0)
            snprintf(g_cfg.monitor, sizeof g_cfg.monitor, "%s", h89_rom_at(g_monitors[sel])->file);
        changed();
        return;
    case K_BAUD:
        if (notify != CBN_SELCHANGE)
            return;
        sel = SendMessageA(b->ctl, CB_GETCURSEL, 0, 0);
        if (sel >= 0)
            g_cfg.sw401 = (uint8_t)((g_cfg.sw401 & 0xf0) | (sel + 1));
        changed();
        return;
    case K_SW501: {
        uint8_t mask = h89_sw501_field_mask(g_cfg.monitor, b->sw_field);
        if (notify != CBN_SELCHANGE)
            return;
        sel = SendMessageA(b->ctl, CB_GETCURSEL, 0, 0);
        if (sel >= 0)
            g_cfg.sw501 = (uint8_t)((g_cfg.sw501 & ~mask) |
                                    h89_sw501_option_value(g_cfg.monitor, b->sw_field, (int)sel));
        changed();
        return;
    }
    case K_BIT:
        if (SendMessageA(b->ctl, BM_GETCHECK, 0, 0) == BST_CHECKED)
            *b->byte |= b->mask;
        else
            *b->byte &= (uint8_t)~b->mask;
        changed();
        return;
    case K_FLAG:
        *b->field = SendMessageA(b->ctl, BM_GETCHECK, 0, 0) == BST_CHECKED;
        changed();
        return;
    case K_NUMBER: {
        char text[16];
        if (notify != EN_CHANGE)
            return;
        GetWindowTextA(b->ctl, text, sizeof text);
        *b->field = atoi(text);
        changed();
        return;
    }
    case K_SET_CHECK:
    case K_SET_COMBO: {
        int v = b->kind == K_SET_CHECK ? SendMessageA(b->ctl, BM_GETCHECK, 0, 0) == BST_CHECKED
                                       : (int)SendMessageA(b->ctl, CB_GETCURSEL, 0, 0);
        if (b->kind == K_SET_COMBO && notify != CBN_SELCHANGE)
            return;
        if (v < 0 || h89session_get_int(g_session, b->key, b->def) == v)
            return;
        h89session_set_int(g_session, b->key, v);
        if (b->setkind == S_HOST)
            g_host_changed = 1;
        else if (b->setkind == S_VIEW) {
            h89session_apply_view(g_session);
            h89w_apply_view();
        }
        return;
    }
    default:
        return;
    }
}

static void finish(int accepted)
{
    int outcome_cycle = 0, host = g_host_changed;
    int changed_cfg = memcmp(&g_cfg, &g_orig, sizeof g_cfg) != 0;
    int running = h89session_is_running(g_session);
    HWND w = g_win;

    if (accepted && changed_cfg) {
        int needs = running && h89session_config_needs_power_cycle(g_session, &g_cfg);
        h89session_set_config(g_session, &g_cfg);
        if (!running)
            outcome_cycle = 1;
        else if (needs &&
                 MessageBoxA(w, "The new configuration takes effect when the H89 is switched off and on. "
                                "The disks are written back first, and go back in their drives.\n\n"
                                "Power cycle the H89 now?",
                             "Power Cycle the H89?", MB_ICONQUESTION | MB_YESNO) == IDYES)
            outcome_cycle = 1;
    }
    h89session_settings_flush(g_session);
    g_win = NULL;
    EnableWindow(g_hwnd, TRUE);
    DestroyWindow(w);
    SetForegroundWindow(g_hwnd);
    if (host)
        h89w_restart(1);
    else if (outcome_cycle)
        h89w_restart(0);
}

static LRESULT CALLBACK page_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id >= ID_BASE && id < ID_BASE + g_nrows) {
            on_row(&g_rows[id - ID_BASE], HIWORD(wp));
            return 0;
        }
        if (id == ID_IMPORT) {
            h89w_import_roms(g_win);
            for (int i = 0; i < g_nrows; i++)
                if (g_rows[i].kind == K_MONITOR)
                    fill_monitors(&g_rows[i]);
            refresh_roms_text();
            changed();
            return 0;
        }
        if (id == ID_CPNET_DIR) {
            BROWSEINFOA bi;
            char path[MAX_PATH];
            LPITEMIDLIST pidl;
            memset(&bi, 0, sizeof bi);
            bi.hwndOwner = g_win;
            bi.lpszTitle = "The CP/NET host folder";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            pidl = SHBrowseForFolderA(&bi);
            if (pidl && SHGetPathFromIDListA(pidl, path)) {
                snprintf(g_cfg.cpnet_root, sizeof g_cfg.cpnet_root, "%s", path);
                SetWindowTextA(g_cpnet_dir, path);
                changed();
            }
            if (pidl)
                CoTaskMemFree(pidl);
            return 0;
        }
        if (id == ID_WEBUI) {
            if (h89session_fujinet_running(g_session))
                ShellExecuteA(g_win, "open", h89session_fujinet_webui_url(g_session), NULL, NULL, SW_SHOWNORMAL);
            return 0;
        }
    }
    if (msg == WM_HSCROLL && lp) {
        h89session_set_volume(g_session, (int)SendMessageA((HWND)lp, TBM_GETPOS, 0, 0));
        return 0;
    }
    if (msg == WM_CTLCOLORSTATIC) {
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK win_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NOTIFY:
        if (((NMHDR *)lp)->hwndFrom == g_tab && ((NMHDR *)lp)->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(g_tab);
            for (int i = 0; i < NPAGES; i++)
                ShowWindow(g_pages[i], i == sel ? SW_SHOW : SW_HIDE);
            changed();
        }
        return 0;
    case WM_REBUILD_SW501:
        rebuild_sw501();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_OK || LOWORD(wp) == IDOK) {
            char why[256];
            if (!h89_config_validate(&g_cfg, why, sizeof why)) {
                MessageBoxA(hwnd, why, "This Machine Cannot Be Built", MB_ICONWARNING | MB_OK);
                return 0;
            }
            finish(1);
            return 0;
        }
        if (LOWORD(wp) == ID_CANCEL || LOWORD(wp) == IDCANCEL) {
            finish(0);
            return 0;
        }
        break;
    case WM_CLOSE:
        finish(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* ---- the pages ----------------------------------------------------------------------------------- */

static const int kSpeedups[] = { 0, 2, 3, 4, 6, 8, 10, 16, 20, 40 };
static const char *const kSpeedupNames[] = { "None (2.048 MHz)", "2x", "3x", "4x", "6x", "8x", "10x", "16x", "20x",
                                             "40x", NULL };
static const int kPorts[] = { 0x78, 0x7c };
static const char *const kPortNames[] = { "170Q (0x78, JP1A)", "174Q (0x7C, JP1B)", NULL };
static const int kInts[] = { 0, 3, 4, 5 };
static const char *const kIntNames[] = { "None", "INT 3 (JP2A)", "INT 4 (JP2B)", "INT 5 (JP2C)", NULL };

static void build_machine(HWND p)
{
    Binding *b;
    int y = 12;
    combo(p, "Model", 16, y, 160, 300, &g_cfg.model, h89_model_name, NULL, NULL, 0); y += 30;
    combo(p, "Memory", 16, y, 160, 300, &g_cfg.memory, h89_memory_name, NULL, NULL, 0); y += 30;
    combo(p, "Memory slot P503", 16, y, 160, 300, &g_cfg.p503, h89_p503_name, NULL, NULL, 0); y += 30;
    b = add_row(p, K_MONITOR, "Monitor ROM", 16, y, 160, 420);
    fill_monitors(b);
    y += 30;
    combo(p, "Speed-up", 16, y, 160, 300, &g_cfg.speedup, NULL, kSpeedupNames, kSpeedups, 10); y += 40;
    mk_label(p, "System ROMs (imported from MAME's h89.zip and heath_tlb.zip):", 16, y, 560); y += 24;
    g_roms = mk_label(p, "", 16, y, 560);
    {
        HWND btn = CreateWindowA("BUTTON", "Import...", WS_CHILD | WS_VISIBLE | WS_TABSTOP, h89w_px(590), h89w_px(y - 2),
                                 h89w_px(90), h89w_px(26), p,
                                 (HMENU)ID_IMPORT, NULL, NULL);
        h89w_set_font(btn, h89w_font());
    }
    refresh_roms_text();
}

static void build_slots(HWND p)
{
    static const char *const slot[] = { "Slot P504", "Slot P505", "Slot P506" };
    char t[40];
    int y = 12, start;
    for (int i = 0; i < H89_SLOTS; i++, y += 28)
        combo(p, slot[i], 16, y, 110, 220, &g_cfg.slot[i], h89_card_name, NULL, NULL, 0);
    y += 8;
    start = g_nrows;
    for (int i = 0; i < H89_H17_DRIVES; i++, y += 28) {
        snprintf(t, sizeof t, "H17 drive %d", i + 1);
        combo(p, strdup(t), 16, y, 110, 220, &g_cfg.h17_drive[i], h89_h17_drive_name, NULL, NULL, 0);
    }
    set_group(&g_rows[start], G_H17);
    y += 8;
    start = g_nrows;
    for (int i = 0; i < H89_H37_DRIVES; i++, y += 28) {
        snprintf(t, sizeof t, "Z-89-37 drive %d", i + 1);
        combo(p, strdup(t), 16, y, 110, 220, &g_cfg.h37_drive[i], h89_fdd_name, NULL, NULL, 0);
    }
    set_group(&g_rows[start], G_H37);
    y = 12;
    start = g_nrows;
    for (int i = 0; i < H89_MMS316_DRIVES; i++, y += 28) {
        snprintf(t, sizeof t, "77316 unit %d (%s)", i + 1, i < 4 ? "8\"" : "5\"");
        combo(p, strdup(t), 370, y, 130, 200, &g_cfg.mms316_drive[i], h89_fdd_name, NULL, NULL, 0);
    }
    set_group(&g_rows[start], G_MMS316);
    y += 8;
    start = g_nrows;
    combo(p, "77320 port", 370, y, 130, 200, &g_cfg.mms320_port, NULL, kPortNames, kPorts, 2); y += 28;
    combo(p, "77320 interrupt", 370, y, 130, 200, &g_cfg.mms320_intr, NULL, kIntNames, kInts, 4); y += 28;
    for (int i = 0; i < H89_MMS320_DRIVES; i++, y += 28) {
        snprintf(t, sizeof t, "77320 unit %d", i);
        combo(p, strdup(t), 370, y, 130, 200, &g_cfg.mms320_drive[i], h89_sasi_name, NULL, NULL, 0);
    }
    set_group(&g_rows[start], G_MMS320);
}

static void build_switches(HWND p)
{
    static const char *const bauds[] = { "110", "150", "300", "600", "1200", "1800", "2000", "2400", "3600",
                                         "4800", "7200", "9600", "19200", NULL };
    static const struct { const char *name; uint8_t mask; } sw402[] = {
        { "Block cursor", 0x01 }, { "No key click", 0x02 }, { "Wrap at the end of a line", 0x04 },
        { "Line feed on carriage return", 0x08 }, { "Carriage return on line feed", 0x10 },
        { "ANSI mode (asked for)", 0x20 }, { "Keypad shifted", 0x40 }, { "50 Hz refresh", 0x80 },
    };
    Binding *b;
    int y = 12;
    mk_label(p, "SW501 (CPU board): read by the monitor at power-up", 16, y, 600); y += 24;
    flag(p, "Set the device switches from the slots", 16, y, 400, &g_cfg.sw501_auto); y += 28;
    g_sw501_y = y;
    y += 8 * 28;
    mk_label(p, "SW401 (H19: the line)", 16, y, 300);
    mk_label(p, "SW402 (H19: the terminal)", 370, y, 300);
    y += 24;
    b = add_row(p, K_BAUD, "Baud rate", 16, y, 110, 120);
    fill_names(b->ctl, NULL, bauds);
    {
        int code = g_cfg.sw401 & 0x0f;
        SendMessageA(b->ctl, CB_SETCURSEL, (WPARAM)(code >= 1 && code <= 13 ? code - 1 : 11), 0);
    }
    bit(p, "Parity", 16, y + 28, 300, &g_cfg.sw401, 0x10);
    bit(p, "Even parity", 16, y + 52, 300, &g_cfg.sw401, 0x20);
    bit(p, "Stick parity", 16, y + 76, 300, &g_cfg.sw401, 0x40);
    bit(p, "Full duplex", 16, y + 100, 300, &g_cfg.sw401, 0x80);
    for (int i = 0; i < 8; i++)
        bit(p, sw402[i].name, 370, y + i * 24, 300, &g_cfg.sw402, sw402[i].mask);
}

static void build_cpnet(HWND p)
{
    HWND btn;
    int y = 12;
    mk_label(p, "A host folder served to CP/NET as its drives: a, b, ... are folders inside it", 16, y, 640);
    y += 28;
    flag(p, "CP/NET device", 16, y, 300, &g_cfg.cpnet_enabled); y += 30;
    number(p, "Port", 16, y, 110, &g_cfg.cpnet_port); y += 30;
    number(p, "Client ID", 16, y, 110, &g_cfg.cpnet_clientid); y += 30;
    mk_label(p, "Host folder", 16, y, 110);
    g_cpnet_dir = mk_label(p, g_cfg.cpnet_root[0] ? g_cfg.cpnet_root : "(the default)", 126, y, 440);
    btn = CreateWindowA("BUTTON", "Choose...", WS_CHILD | WS_VISIBLE | WS_TABSTOP, h89w_px(590), h89w_px(y), h89w_px(90),
                        h89w_px(26), p,
                        (HMENU)ID_CPNET_DIR, NULL, NULL);
    h89w_set_font(btn, h89w_font());
}

static void build_display(HWND p)
{
    static const char *const phosphors[] = { "White (P4)", "Green (P1)", "Amber (P3)", NULL };
    static const char *const aspects[] = { "The tube's 4:3", "Square pixels", NULL };
    HWND tb;
    int y = 12;
    set_combo(p, "Phosphor", 16, y, 110, 200, "view.phosphor", 0, phosphors, S_VIEW); y += 30;
    set_check(p, "Scanlines", 16, y, 300, "view.scanlines", 0, S_VIEW); y += 26;
    set_check(p, "Blinking cursor", 16, y, 300, "view.cursor_blink", 1, S_VIEW); y += 26;
    set_combo(p, "Aspect", 16, y, 110, 200, "view.aspect", 0, aspects, S_VIEW); y += 30;
    set_check(p, "Smooth scaling", 16, y, 300, "view.smooth", 1, S_VIEW); y += 40;
    set_check(p, "Enable audio (applied when Settings closes)", 16, y, 400, "enable_audio", 1, S_HOST); y += 28;
    mk_label(p, "Volume", 16, y, 110);
    tb = CreateWindowA(TRACKBAR_CLASSA, "", WS_CHILD | WS_VISIBLE | TBS_HORZ, h89w_px(126), h89w_px(y), h89w_px(220),
                       h89w_px(28), p, NULL, NULL, NULL);
    SendMessageA(tb, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageA(tb, TBM_SETPOS, TRUE, h89session_get_int(g_session, "sound.volume", 70));
    y += 34;
    set_check(p, "Bell", 16, y, 300, "sound.bell", 1, S_LIVE); y += 26;
    set_check(p, "Key click (unless SW402 or ESC x2 turns it off)", 16, y, 400, "sound.click", 1, S_LIVE); y += 26;
    set_check(p, "Visual bell (flash the screen)", 16, y, 400, "sound.visual_bell", 0, S_LIVE);
}

static void build_fujinet(HWND p)
{
    Binding *b;
    HWND btn;
    char url[160];
    int y = 12;
    set_check(p, "Enable FujiNet (applied when Settings closes)", 16, y, 400, "enable_fujinet", 1, S_HOST);
    y += 30;
    snprintf(url, sizeof url, "Web UI: %s", h89session_fujinet_webui_url(g_session));
    mk_label(p, url, 16, y, 400);
    btn = CreateWindowA("BUTTON", "Open", WS_CHILD | WS_VISIBLE | WS_TABSTOP, h89w_px(420), h89w_px(y), h89w_px(80),
                       h89w_px(26), p, (HMENU)ID_WEBUI,
                        NULL, NULL);
    h89w_set_font(btn, h89w_font());
    y += 34;
    b = number(p, "Card handshake delay (cycles)", 16, y, 220, &g_cfg.fujinet_ack_delay);
    b->group = G_FUJI;
}

void h89w_show_settings(HWND owner)
{
    static int registered;
    static const char *const names[NPAGES] = { "Machine", "Slots && Drives", "Switches", "CP/NET",
                                               "Display && Sound", "FujiNet" };
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtr(owner, GWLP_HINSTANCE);
    RECT rc, want;
    const char *pg = getenv("H89_PREFS_PAGE");
    static const char *const keys[NPAGES] = { "machine", "slots", "switches", "cpnet", "display", "fujinet" };
    int first = 0;

    if (g_win) {
        SetForegroundWindow(g_win);
        return;
    }
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = win_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "H89Settings";
        wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(IDI_APPICON));
        RegisterClassA(&wc);
        wc.lpfnWndProc = page_proc;
        wc.lpszClassName = "H89SettingsPage";
        wc.hIcon = NULL;
        RegisterClassA(&wc);
        registered = 1;
    }
    h89session_get_config(g_session, &g_cfg);
    g_orig = g_cfg;
    g_host_changed = 0;
    g_nrows = 0;

    SetRect(&want, 0, 0, h89w_px(720), h89w_px(640));
    AdjustWindowRectEx(&want, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
    g_win = CreateWindowA("H89Settings", "Settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                          CW_USEDEFAULT, want.right - want.left, want.bottom - want.top, owner, NULL, inst, NULL);
    g_status = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE, h89w_px(12), h89w_px(10), h89w_px(696), h89w_px(20),
                            g_win, NULL, inst, NULL);
    h89w_set_font(g_status, h89w_font());
    g_tab = CreateWindowA(WC_TABCONTROLA, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, h89w_px(8), h89w_px(36),
                          h89w_px(704), h89w_px(556), g_win, NULL,
                          inst, NULL);
    h89w_set_font(g_tab, h89w_font());
    for (int i = 0; i < NPAGES; i++) {
        TCITEMA item;
        memset(&item, 0, sizeof item);
        item.mask = TCIF_TEXT;
        item.pszText = (char *)names[i];
        TabCtrl_InsertItem(g_tab, i, &item);
    }
    GetClientRect(g_tab, &rc);
    TabCtrl_AdjustRect(g_tab, FALSE, &rc);
    for (int i = 0; i < NPAGES; i++)
        g_pages[i] = CreateWindowA("H89SettingsPage", "", WS_CHILD, h89w_px(8) + rc.left, h89w_px(36) + rc.top,
                                   rc.right - rc.left,
                                   rc.bottom - rc.top, g_win, NULL, inst, NULL);
    build_machine(g_pages[0]);
    build_slots(g_pages[1]);
    build_switches(g_pages[2]);
    build_cpnet(g_pages[3]);
    build_display(g_pages[4]);
    build_fujinet(g_pages[5]);
    g_ok = CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, h89w_px(524), h89w_px(602),
                         h89w_px(90), h89w_px(28),
                         g_win, (HMENU)ID_OK, inst, NULL);
    h89w_set_font(g_ok, h89w_font());
    {
        HWND c = CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, h89w_px(620), h89w_px(602), h89w_px(90),
                               h89w_px(28), g_win,
                               (HMENU)ID_CANCEL, inst, NULL);
        h89w_set_font(c, h89w_font());
    }
    for (int i = 0; pg && i < NPAGES; i++)
        if (!strcmp(pg, keys[i]))
            first = i;
    TabCtrl_SetCurSel(g_tab, first);
    ShowWindow(g_pages[first], SW_SHOW);
    changed();
    rebuild_sw501();
    EnableWindow(owner, FALSE);
    ShowWindow(g_win, SW_SHOW);
}
