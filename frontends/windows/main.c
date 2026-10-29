/*
 * FujiNet Go H89 -- the Windows (Win32 + GDI) frontend.
 *
 * No toolkit: a plain window, a menu bar, a status bar and StretchDIBits.
 * That is enough for the H19's 640 x 500 picture, and it keeps the
 * artifact a folder you copy rather than a runtime hunt.
 *
 * Keys go to the H19 as characters: WM_CHAR carries what the layout types
 * (CTRL with a letter is its control code), and WM_KEYDOWN the keys that
 * type nothing -- the arrows, the keypad, the function keys, which the
 * session's table turns into H19 keys; the WM_CHAR a handled key would
 * also produce is taken off the queue. The app's own shortcuts are
 * CTRL+SHIFT ones (and F9, F11, F12), since CTRL alone is the H19's.
 *
 * DwmFlush() on a present thread is this platform's frame clock.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debugger/dbg_window.h"
#include "resource.h"
#include "win_common.h"

#define WIN_CLASS "FujiNetGoH89"
#define DOT_PART_W 22

h89session *g_session;
HWND g_hwnd;
HBRUSH g_accent_brush;

static HWND g_statusbar;
static HMENU g_drives_menu;
static uint32_t *g_fb;
static uint64_t g_serial;
static BITMAPINFO g_bmi;
static CRITICAL_SECTION g_fb_lock;
static volatile LONG g_running = 1;
static HANDLE g_present_thread;
static int g_aspect = 1, g_smooth = 1, g_fullscreen = 0, g_flash = 0;
static WINDOWPLACEMENT g_placement;
static int g_link_up, g_disk_busy;
static int g_choose_ids[16];
static int g_choose_n;
static char g_choose_path[MAX_PATH];

/* ---- fonts ------------------------------------------------------------------- */

int h89w_px(int v)
{
    static int dpi;
    if (!dpi) {
        HDC dc = GetDC(NULL);
        dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
        if (dc)
            ReleaseDC(NULL, dc);
        if (dpi < 96)
            dpi = 96;
    }
    return MulDiv(v, dpi, 96);
}

HFONT h89w_font(void)
{
    static HFONT f;
    if (!f) {
        NONCLIENTMETRICSA ncm;
        memset(&ncm, 0, sizeof ncm);
        ncm.cbSize = sizeof ncm;
        if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0))
            f = CreateFontIndirectA(&ncm.lfMessageFont);
        if (!f)
            f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    return f;
}

HFONT h89w_mono_font(void)
{
    static HFONT f;
    if (!f)
        f = CreateFontA(-h89w_px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        FIXED_PITCH | FF_MODERN, "Consolas");
    return f;
}

void h89w_set_font(HWND h, HFONT f)
{
    SendMessageA(h, WM_SETFONT, (WPARAM)f, TRUE);
}

/* ---- the present thread: this platform's frame clock -------------------------- */

static DWORD WINAPI present_thread(LPVOID arg)
{
    (void)arg;
    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        LARGE_INTEGER t, f;
        if (FAILED(DwmFlush()))
            Sleep(8);
        QueryPerformanceCounter(&t);
        QueryPerformanceFrequency(&f);
        h89session_notify_vsync(g_session, (int64_t)(t.QuadPart * 1000000000LL / f.QuadPart));
        EnterCriticalSection(&g_fb_lock);
        if (h89session_copy_frame(g_session, g_fb, &g_serial)) {
            LeaveCriticalSection(&g_fb_lock);
            InvalidateRect(g_hwnd, NULL, FALSE);
        } else {
            LeaveCriticalSection(&g_fb_lock);
        }
    }
    return 0;
}

/* ---- painting -------------------------------------------------------------------- */

static int statusbar_height(void)
{
    RECT r;
    if (!g_statusbar || !IsWindowVisible(g_statusbar))
        return 0;
    GetWindowRect(g_statusbar, &r);
    return r.bottom - r.top;
}

static void paint(HDC dc)
{
    RECT rc;
    double want, w, h, sw, sh;

    GetClientRect(g_hwnd, &rc);
    rc.bottom -= statusbar_height();
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    if (w <= 0 || h <= 0)
        return;
    /* the H19's tube was 4:3; square pixels show the buffer as it is */
    want = g_aspect ? 4.0 / 3.0 : (double)H89SESSION_FB_WIDTH / (double)H89SESSION_FB_HEIGHT;
    if (w / h > want) {
        sh = h;
        sw = sh * want;
    } else {
        sw = w;
        sh = sw / want;
    }
    EnterCriticalSection(&g_fb_lock);
    SetStretchBltMode(dc, g_smooth ? HALFTONE : COLORONCOLOR);
    StretchDIBits(dc, (int)((w - sw) / 2), (int)((h - sh) / 2), (int)sw, (int)sh, 0, 0,
                  H89SESSION_FB_WIDTH, H89SESSION_FB_HEIGHT, g_fb, &g_bmi, DIB_RGB_COLORS,
                  g_flash ? NOTSRCCOPY : SRCCOPY);
    LeaveCriticalSection(&g_fb_lock);
}

void h89w_apply_view(void)
{
    HMENU m = GetMenu(g_hwnd);
    g_aspect = h89session_get_int(g_session, "view.aspect", 0) == 0;
    g_smooth = h89session_get_int(g_session, "view.smooth", 1) != 0;
    if (m) {
        CheckMenuItem(m, IDM_TV_ASPECT, MF_BYCOMMAND | (g_aspect ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(m, IDM_SMOOTH, MF_BYCOMMAND | (g_smooth ? MF_CHECKED : MF_UNCHECKED));
    }
    InvalidateRect(g_hwnd, NULL, TRUE);
}

/* ---- status ---------------------------------------------------------------------- */

static void set_status_text(const char *text)
{
    wchar_t w[512];
    if (!g_statusbar)
        return;
    MultiByteToWideChar(CP_UTF8, 0, text, -1, w, 512);
    SendMessageW(g_statusbar, SB_SETTEXTW, 2, (LPARAM)w);
}

static void update_status(void)
{
    char line[400];
    if (!g_statusbar)
        return;
    if (!h89session_is_running(g_session)) {
        char what[512];
        snprintf(line, sizeof line, "%s",
                 h89session_missing_roms(g_session, what, sizeof what) > 0
                     ? "Off \xe2\x80\x94 ROMs needed (Machine > Import System ROMs)" : "Off");
        g_link_up = 0;
    } else {
        int link = h89session_card_link(g_session);
        g_link_up = link == 2;
        snprintf(line, sizeof line, "%s%s",
                 link < 0 ? "No FujiNet card" : h89session_card_link_text(link),
                 h89session_warp(g_session) ? "  \xc2\xb7  WARP" : "");
    }
    SendMessageA(g_statusbar, SB_SETTEXTA, SBT_OWNERDRAW | 0, (LPARAM)g_disk_busy);
    SendMessageA(g_statusbar, SB_SETTEXTA, SBT_OWNERDRAW | 1, (LPARAM)g_link_up);
    set_status_text(line);
}

static void draw_status_dot(const DRAWITEMSTRUCT *di)
{
    const int on = di->itemData != 0;
    const int size = h89w_px(10);
    RECT r = di->rcItem;
    const int x = r.left + ((r.right - r.left) - size) / 2;
    const int y = r.top + ((r.bottom - r.top) - size) / 2;
    HGDIOBJ oldb = SelectObject(di->hDC, on ? (HGDIOBJ)g_accent_brush : GetStockObject(GRAY_BRUSH));
    HGDIOBJ oldp = SelectObject(di->hDC, GetStockObject(NULL_PEN));
    Ellipse(di->hDC, x, y, x + size + 1, y + size + 1);
    SelectObject(di->hDC, oldp);
    SelectObject(di->hDC, oldb);
}

/* the drive light and the visual bell, four times a second */
static void fast_tick(void)
{
    int busy = 0, n = h89session_drive_count(g_session);
    for (int i = 0; i < n && !busy; i++) {
        h89_drive_info d;
        if (h89session_drive_info_at(g_session, i, &d) == 0 && d.active)
            busy = 1;
    }
    if (busy != g_disk_busy) {
        g_disk_busy = busy;
        SendMessageA(g_statusbar, SB_SETTEXTA, SBT_OWNERDRAW | 0, (LPARAM)busy);
    }
    if (h89session_take_visual_bells(g_session) > 0 &&
        h89session_get_int(g_session, "sound.visual_bell", 0)) {
        g_flash = 1;
        InvalidateRect(g_hwnd, NULL, FALSE);
        SetTimer(g_hwnd, IDT_FLASH, 120, NULL);
    }
}

/* ---- starting, ROMs ------------------------------------------------------------------ */

static const char *base_name(const char *p)
{
    const char *s = strrchr(p, '\\');
    const char *t = strrchr(p, '/');
    if (t && (!s || t > s))
        s = t;
    return s ? s + 1 : p;
}

int h89w_import_roms(HWND owner)
{
    static char buf[16384];
    OPENFILENAMEA ofn;
    char msg[1024];
    int total = 0;
    char *log;
    size_t loglen = 0;

    memset(&ofn, 0, sizeof ofn);
    buf[0] = '\0';
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "MAME ROM sets and ROM images (*.zip;*.bin;*.rom)\0*.zip;*.bin;*.rom\0All files\0*.*\0\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof buf;
    ofn.lpstrTitle = "Import System ROMs";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameA(&ofn))
        return 0;
    log = calloc(1, 8192);
    /* one file: the full path; several: the folder, then the names */
    if (buf[strlen(buf) + 1] == '\0') {
        total = h89session_import_roms(g_session, buf, msg, sizeof msg);
        snprintf(log, 8192, "%s", msg);
    } else {
        const char *dir = buf;
        for (const char *name = buf + strlen(buf) + 1; *name; name += strlen(name) + 1) {
            char path[MAX_PATH * 2];
            int n;
            snprintf(path, sizeof path, "%s\\%s", dir, name);
            n = h89session_import_roms(g_session, path, msg, sizeof msg);
            if (n > 0)
                total += n;
            loglen += (size_t)snprintf(log + loglen, 8192 - loglen, "%s%s", loglen ? "\r\n\r\n" : "", msg);
            if (loglen >= 8000)
                break;
        }
    }
    MessageBoxA(owner, log, total > 0 ? "ROMs Imported" : "Nothing Imported",
                (total > 0 ? MB_ICONINFORMATION : MB_ICONWARNING) | MB_OK);
    free(log);
    return total;
}

static void rom_setup(void)
{
    char what[512], text[1400];
    /* asked for at start; a ROM file on the command line may have been
     * imported since */
    if (h89session_missing_roms(g_session, what, sizeof what) == 0)
        return;
    snprintf(text, sizeof text,
             "FujiNet Go H89 does not come with the Heath ROMs. Import them from MAME's h89.zip "
             "and heath_tlb.zip, or from ROM image files: they are recognised by their contents "
             "and kept in\r\n%s\r\n\r\nStill needed: %s.\r\n\r\nImport them now?",
             h89session_roms_path(g_session), what);
    if (MessageBoxA(g_hwnd, text, "Import the H89's ROMs", MB_ICONINFORMATION | MB_YESNO) != IDYES)
        return;
    if (h89w_import_roms(g_hwnd) > 0 && h89session_missing_roms(g_session, what, sizeof what) == 0)
        h89w_start();
    else if (h89session_missing_roms(g_session, what, sizeof what) > 0)
        PostMessageA(g_hwnd, WM_APP + 1, 0, 0);   /* ask again */
}

void h89w_start(void)
{
    h89session_start_opts opts;
    char what[512];
    if (h89session_is_running(g_session))
        return;
    h89session_default_opts(g_session, &opts);
    if (h89session_start(g_session, &opts) == 0) {
        update_status();
        return;
    }
    if (h89session_missing_roms(g_session, what, sizeof what) > 0)
        PostMessageA(g_hwnd, WM_APP + 1, 0, 0);
    else
        MessageBoxA(g_hwnd, h89session_last_error(g_session), "Cannot Start the H89", MB_ICONWARNING | MB_OK);
    update_status();
}

void h89w_restart(int host)
{
    if (!h89session_is_running(g_session)) {
        h89w_start();
        return;
    }
    if (host) {
        h89session_stop(g_session);
        h89w_start();
        return;
    }
    if (h89session_power_cycle(g_session) != 0)
        MessageBoxA(g_hwnd, h89session_last_error(g_session), "Cannot Power Cycle", MB_ICONWARNING | MB_OK);
    update_status();
}

/* ---- disks --------------------------------------------------------------------------- */

static const char *const kH17Filter =
    "H17 disk images (*.h8d;*.h17;*.h17disk;*.h17raw;*.rawdisk;*.tmpdisk)\0*.h8d;*.h17;*.h17disk;*.h17raw;*.rawdisk;*.tmpdisk\0All files\0*.*\0\0";
static const char *const kSoftFilter =
    "Soft-sectored disk images (*.imd;*.td0;*.mmsdisk;*.logdisk;*.rawfloppy)\0*.imd;*.td0;*.mmsdisk;*.logdisk;*.rawfloppy\0All files\0*.*\0\0";
static const char *const kAnyFilter =
    "Disk images\0*.h8d;*.h17;*.h17disk;*.h17raw;*.rawdisk;*.tmpdisk;*.imd;*.td0;*.mmsdisk;*.logdisk;*.rawfloppy\0All files\0*.*\0\0";

static int pick_file(const char *title, const char *filter, char *path, DWORD pathsz)
{
    OPENFILENAMEA ofn;
    const char *dir = h89session_get_str(g_session, "drives.last_dir", "");
    memset(&ofn, 0, sizeof ofn);
    path[0] = '\0';
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = pathsz;
    ofn.lpstrTitle = title;
    ofn.lpstrInitialDir = dir && *dir ? dir : h89session_disks_path(g_session);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameA(&ofn) ? 1 : 0;
}

static void remember_folder(const char *path)
{
    char dir[MAX_PATH];
    const char *b = base_name(path);
    snprintf(dir, sizeof dir, "%.*s", (int)(b - path), path);
    if (dir[0])
        h89session_set_str(g_session, "drives.last_dir", dir);
}

static void insert_into(int id, const char *path)
{
    char err[512];
    if (h89session_drive_insert(g_session, id, path, 0, err, sizeof err) != 0) {
        MessageBoxA(g_hwnd, err, "Cannot Insert the Disk", MB_ICONWARNING | MB_OK);
        return;
    }
    remember_folder(path);
}

static void load_media(const char *path)
{
    char msg[1024];
    int ids[16], nids = 0, rc;
    int kind = h89session_media_kind(path);

    if (kind == H89_MEDIA_ROMS) {
        h89session_import_roms(g_session, path, msg, sizeof msg);
        MessageBoxA(g_hwnd, msg, "Import ROMs", MB_ICONINFORMATION | MB_OK);
        if (!h89session_is_running(g_session))
            h89w_start();
        return;
    }
    if (!h89session_is_running(g_session) && kind != H89_MEDIA_UNKNOWN) {
        MessageBoxA(g_hwnd, "The H89 is off: import its ROMs first.", APP_TITLE, MB_ICONINFORMATION | MB_OK);
        return;
    }
    rc = h89session_import_media(g_session, path, msg, sizeof msg, ids, 16, &nids);
    if (rc < 0) {
        MessageBoxA(g_hwnd, msg, "Cannot Use the File", MB_ICONWARNING | MB_OK);
        return;
    }
    if (rc == 1) {
        /* every drive that takes it is full: ask which, by a menu */
        HMENU pop = CreatePopupMenu();
        POINT pt;
        AppendMenuA(pop, MF_STRING | MF_GRAYED, 0, "Which drive? (it is ejected first)");
        AppendMenuA(pop, MF_SEPARATOR, 0, NULL);
        g_choose_n = 0;
        for (int i = 0; i < nids && i < 16; i++) {
            h89_drive_info d;
            if (h89session_drive_info(g_session, ids[i], &d) == 0) {
                AppendMenuA(pop, MF_STRING, IDM_CHOOSE_BASE + ids[i], d.label);
                g_choose_ids[g_choose_n++] = ids[i];
            }
        }
        snprintf(g_choose_path, sizeof g_choose_path, "%s", path);
        GetCursorPos(&pt);
        SetForegroundWindow(g_hwnd);
        TrackPopupMenu(pop, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
        DestroyMenu(pop);
        return;
    }
    if (kind != H89_MEDIA_UNKNOWN)
        remember_folder(path);
    set_status_text(msg);
}

/* The Drives menu, rebuilt as it opens: the running machine's drives. */
static void fill_drives_menu(void)
{
    int n = h89session_drive_count(g_session), shown = 0;
    while (GetMenuItemCount(g_drives_menu) > 0)
        DeleteMenu(g_drives_menu, 0, MF_BYPOSITION);
    for (int i = 0; i < n; i++) {
        h89_drive_info d;
        char label[400];
        HMENU sub;
        if (h89session_drive_info_at(g_session, i, &d) != 0 || !d.removable)
            continue;
        sub = CreatePopupMenu();
        AppendMenuA(sub, MF_STRING, IDM_DRIVE_BASE + d.id * 4 + DRIVE_INSERT, "&Insert...");
        if (d.loaded) {
            AppendMenuA(sub, MF_STRING, IDM_DRIVE_BASE + d.id * 4 + DRIVE_EJECT, "&Eject");
            AppendMenuA(sub, MF_STRING | (d.write_protected ? MF_CHECKED : 0) |
                             (d.read_only_format ? MF_GRAYED : 0),
                        IDM_DRIVE_BASE + d.id * 4 + DRIVE_WP, "&Write Protect");
        }
        snprintf(label, sizeof label, "%s: %.300s", d.label, d.loaded ? base_name(d.path) : "empty");
        AppendMenuA(g_drives_menu, MF_POPUP, (UINT_PTR)sub, label);
        shown++;
    }
    if (!shown)
        AppendMenuA(g_drives_menu, MF_STRING, IDM_SETTINGS, "No drives (fit a controller in Settings)");
    AppendMenuA(g_drives_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_drives_menu, MF_STRING, IDM_INSERT_DISK, "Insert &Disk...\tCtrl+Shift+O");
    AppendMenuA(g_drives_menu, MF_STRING, IDM_NEW_DISK, "&New Blank Disk...");
    AppendMenuA(g_drives_menu, MF_STRING, IDM_DISKS_FOLDER, "Open Disks &Folder");
}

static void drive_command(int cmd)
{
    int id = (cmd - IDM_DRIVE_BASE) / 4, action = (cmd - IDM_DRIVE_BASE) % 4;
    h89_drive_info d;
    char path[MAX_PATH], err[512], title[160];
    if (h89session_drive_info(g_session, id, &d) != 0)
        return;
    switch (action) {
    case DRIVE_INSERT:
        snprintf(title, sizeof title, "Insert a Disk in %s", d.label);
        if (pick_file(title, H89_DRIVE_CTRL(id) == H89_CTRL_H17 ? kH17Filter : kSoftFilter, path, sizeof path))
            insert_into(id, path);
        break;
    case DRIVE_EJECT:
        if (h89session_drive_eject(g_session, id, err, sizeof err) != 0)
            MessageBoxA(g_hwnd, err, "Cannot Eject the Disk", MB_ICONWARNING | MB_OK);
        break;
    case DRIVE_WP:
        h89session_drive_set_write_protect(g_session, id, !d.write_protected);
        break;
    default:
        break;
    }
}

/* New Blank Disk: the kind, sides and tracks, then where to save it. */
static int g_blank_done;
static HWND g_blank_kind, g_blank_sides, g_blank_tracks;

static LRESULT CALLBACK blank_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            if (LOWORD(wp) == IDOK) {
                int k = (int)SendMessageA(g_blank_kind, CB_GETCURSEL, 0, 0);
                h89session_set_int(g_session, "blank.kind", k);
                h89session_set_int(g_session, "blank.sides", SendMessageA(g_blank_sides, CB_GETCURSEL, 0, 0) ? 2 : 1);
                h89session_set_int(g_session, "blank.tracks", SendMessageA(g_blank_tracks, CB_GETCURSEL, 0, 0) ? 80 : 40);
            }
            g_blank_done = LOWORD(wp) == IDOK ? 1 : -1;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == 100 && HIWORD(wp) == CBN_SELCHANGE) {
            int imd = SendMessageA(g_blank_kind, CB_GETCURSEL, 0, 0) == H89_BLANK_IMD;
            EnableWindow(g_blank_sides, !imd);
            EnableWindow(g_blank_tracks, !imd);
        }
        break;
    case WM_CLOSE:
        g_blank_done = -1;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void new_blank_disk(void)
{
    static const char *const kinds[] = {
        "H17: sector image (.h8d) - format it with HDOS INIT", "H17: H17Disk (.h17)",
        "H17: raw tracks (.h17raw)", "Soft-sectored: ImageDisk (.imd) - format it in the H89",
    };
    static const char *const exts[] = { "h8d", "h17", "h17raw", "imd" };
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtr(g_hwnd, GWLP_HINSTANCE);
    static int registered;
    HWND w, c;
    MSG m;
    int k, s, t;
    char path[MAX_PATH], err[512];
    OPENFILENAMEA ofn;

    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = blank_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "H89BlankDisk";
        wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(IDI_APPICON));
        RegisterClassA(&wc);
        registered = 1;
    }
    w = CreateWindowExA(WS_EX_DLGMODALFRAME, "H89BlankDisk", "New Blank Disk",
                        WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, h89w_px(440), h89w_px(200),
                        g_hwnd, NULL, inst, NULL);
    g_blank_kind = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, h89w_px(16), h89w_px(16),
                                 h89w_px(400), h89w_px(200), w,
                                 (HMENU)100, inst, NULL);
    for (int i = 0; i < 4; i++)
        SendMessageA(g_blank_kind, CB_ADDSTRING, 0, (LPARAM)kinds[i]);
    SendMessageA(g_blank_kind, CB_SETCURSEL, (WPARAM)h89session_get_int(g_session, "blank.kind", 0), 0);
    c = CreateWindowA("STATIC", "Sides", WS_CHILD | WS_VISIBLE, h89w_px(16), h89w_px(54), h89w_px(60),
                      h89w_px(20), w, NULL, inst, NULL);
    h89w_set_font(c, h89w_font());
    g_blank_sides = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, h89w_px(80), h89w_px(50),
                                  h89w_px(80), h89w_px(200), w,
                                  NULL, inst, NULL);
    SendMessageA(g_blank_sides, CB_ADDSTRING, 0, (LPARAM)"1");
    SendMessageA(g_blank_sides, CB_ADDSTRING, 0, (LPARAM)"2");
    SendMessageA(g_blank_sides, CB_SETCURSEL, h89session_get_int(g_session, "blank.sides", 1) == 2, 0);
    c = CreateWindowA("STATIC", "Tracks", WS_CHILD | WS_VISIBLE, h89w_px(190), h89w_px(54), h89w_px(60),
                      h89w_px(20), w, NULL, inst, NULL);
    h89w_set_font(c, h89w_font());
    g_blank_tracks = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, h89w_px(250), h89w_px(50),
                                   h89w_px(80), h89w_px(200), w,
                                   NULL, inst, NULL);
    SendMessageA(g_blank_tracks, CB_ADDSTRING, 0, (LPARAM)"40");
    SendMessageA(g_blank_tracks, CB_ADDSTRING, 0, (LPARAM)"80");
    SendMessageA(g_blank_tracks, CB_SETCURSEL, h89session_get_int(g_session, "blank.tracks", 40) == 80, 0);
    c = CreateWindowA("BUTTON", "Create...", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, h89w_px(230), h89w_px(110),
                      h89w_px(90), h89w_px(28), w,
                      (HMENU)IDOK, inst, NULL);
    h89w_set_font(c, h89w_font());
    c = CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE, h89w_px(326), h89w_px(110), h89w_px(90),
                      h89w_px(28), w, (HMENU)IDCANCEL, inst, NULL);
    h89w_set_font(c, h89w_font());
    h89w_set_font(g_blank_kind, h89w_font());
    h89w_set_font(g_blank_sides, h89w_font());
    h89w_set_font(g_blank_tracks, h89w_font());
    blank_proc(w, WM_COMMAND, MAKEWPARAM(100, CBN_SELCHANGE), 0);
    ShowWindow(w, SW_SHOW);
    EnableWindow(g_hwnd, FALSE);
    g_blank_done = 0;
    while (!g_blank_done && GetMessage(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(w, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    EnableWindow(g_hwnd, TRUE);
    SetForegroundWindow(g_hwnd);
    if (g_blank_done != 1)
        return;
    k = h89session_get_int(g_session, "blank.kind", 0);
    s = h89session_get_int(g_session, "blank.sides", 1);
    t = h89session_get_int(g_session, "blank.tracks", 40);
    if (k < 0 || k > 3)
        k = 0;
    snprintf(path, sizeof path, "blank.%s", exts[k]);
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = "Disk images\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof path;
    ofn.lpstrTitle = "Save the New Disk";
    ofn.lpstrInitialDir = h89session_disks_path(g_session);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    CreateDirectoryA(h89session_disks_path(g_session), NULL);
    if (!GetSaveFileNameA(&ofn))
        return;
    if (h89session_new_blank_disk(g_session, path, k, s, t, err, sizeof err) != 0) {
        MessageBoxA(g_hwnd, err, "Cannot Create the Disk", MB_ICONWARNING | MB_OK);
        return;
    }
    load_media(path);
}

/* ---- text in and out ------------------------------------------------------------------ */

static void copy_screen(void)
{
    static char buf[8192];
    wchar_t *w;
    HGLOBAL mem;
    int n;
    if (h89session_screen_text(g_session, buf, sizeof buf, 1) == 0 || !OpenClipboard(g_hwnd))
        return;
    n = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
    mem = GlobalAlloc(GMEM_MOVEABLE, (size_t)n * 2 + 2);
    if (mem) {
        w = GlobalLock(mem);
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, w, n);
        GlobalUnlock(mem);
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, mem);
    }
    CloseClipboard();
    set_status_text("The screen's text is on the clipboard");
}

static void paste(void)
{
    HANDLE h;
    if (!OpenClipboard(g_hwnd))
        return;
    h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t *w = GlobalLock(h);
        if (w) {
            int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
            char *u = malloc((size_t)n + 1);
            if (u) {
                WideCharToMultiByte(CP_UTF8, 0, w, -1, u, n, NULL, NULL);
                h89session_paste(g_session, u);
                free(u);
            }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
}

/* ---- the keyboard -------------------------------------------------------------------------- */

int h89w_key_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    const int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (msg == WM_KEYDOWN) {
        uint32_t ks;
        int key;
        if (wp == VK_F9) { h89w_keys_toggle(g_hwnd); return 1; }
        if (wp == VK_F11) { PostMessageA(g_hwnd, WM_COMMAND, IDM_FULLSCREEN, 0); return 1; }
        if (wp == VK_F12) { h89_debugger_toggle(g_hwnd, g_session); return 1; }
        if (ctrl && shift) {
            UINT cmd = wp == 'O' ? IDM_INSERT_DISK : wp == 'R' ? IDM_RESET : wp == 'C' ? IDM_COPY_SCREEN
                     : wp == 'V' ? IDM_PASTE : wp == 'Q' ? IDM_EXIT : 0;
            if (cmd) {
                MSG m;
                PostMessageA(g_hwnd, WM_COMMAND, cmd, 0);
                PeekMessageA(&m, hwnd, WM_CHAR, WM_DEADCHAR, PM_REMOVE);
                return 1;
            }
            return 0;
        }
        if (ctrl && wp == VK_OEM_COMMA) {
            MSG m;
            PostMessageA(g_hwnd, WM_COMMAND, IDM_SETTINGS, 0);
            PeekMessageA(&m, hwnd, WM_CHAR, WM_DEADCHAR, PM_REMOVE);
            return 1;
        }
        ks = h89session_keysym_from_win_vk((unsigned)wp, (int)((lp >> 24) & 1));
        if (!ks)
            return 0;
        key = h89session_key_from_event(ks, 0, ctrl, shift);
        if (key < 0)
            return 0;
        h89session_key(g_session, key, shift);
        {
            /* the character TranslateMessage queued for it, if any */
            MSG m;
            PeekMessageA(&m, hwnd, WM_CHAR, WM_DEADCHAR, PM_REMOVE);
        }
        return 1;
    }
    if (msg == WM_CHAR) {
        if (wp < 0x80) {
            h89session_key(g_session, (int)wp, shift);
            return 1;
        }
        return 0;
    }
    return 0;
}

/* ---- the console log ------------------------------------------------------------------------ */

static HWND g_log_window, g_log_edit;

static void log_refresh(void)
{
    static char buf[128 * 1024];
    int n;
    if (!g_log_edit)
        return;
    n = h89session_fujinet_copy_log(g_session, buf, sizeof buf);
    SetWindowTextA(g_log_edit, n > 0 ? buf : "(no FujiNet output yet)");
    n = GetWindowTextLengthA(g_log_edit);
    SendMessageA(g_log_edit, EM_SETSEL, (WPARAM)n, (LPARAM)n);
    SendMessageA(g_log_edit, EM_SCROLLCARET, 0, 0);
}

static LRESULT CALLBACK log_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (g_log_edit)
            MoveWindow(g_log_edit, 0, 0, rc.right, rc.bottom, TRUE);
        return 0;
    }
    case WM_TIMER:
        log_refresh();
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, IDT_LOG_REFRESH);
        g_log_window = g_log_edit = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void show_fujinet_log(HINSTANCE inst)
{
    static int registered;
    RECT rc;
    if (h89session_fujinet_external(g_session)) {
        MessageBoxA(g_hwnd, "FujiNet is external: see its own console.", "FujiNet", MB_ICONINFORMATION | MB_OK);
        return;
    }
    if (g_log_window) {
        SetForegroundWindow(g_log_window);
        return;
    }
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = log_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "H89FujiNetLog";
        wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(IDI_APPICON));
        RegisterClassA(&wc);
        registered = 1;
    }
    g_log_window = CreateWindowA("H89FujiNetLog", "FujiNet Console Log", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                 CW_USEDEFAULT, h89w_px(860), h89w_px(600), NULL, NULL, inst, NULL);
    GetClientRect(g_log_window, &rc);
    g_log_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                                     ES_AUTOVSCROLL | ES_READONLY,
                                 0, 0, rc.right, rc.bottom, g_log_window, (HMENU)(INT_PTR)IDC_LOG_EDIT, inst, NULL);
    h89w_set_font(g_log_edit, h89w_mono_font());
    SetTimer(g_log_window, IDT_LOG_REFRESH, 1000, NULL);
    log_refresh();
    ShowWindow(g_log_window, SW_SHOW);
}

/* ---- the window ---------------------------------------------------------------------------- */

static void build_menu(HWND hwnd)
{
    HMENU bar = CreateMenu();
    HMENU machine = CreatePopupMenu();
    HMENU edit = CreatePopupMenu();
    HMENU view = CreatePopupMenu();
    HMENU fuji = CreatePopupMenu();
    HMENU help = CreatePopupMenu();

    AppendMenuA(machine, MF_STRING, IDM_RESET, "&Reset (SHIFT+RESET)\tCtrl+Shift+R");
    AppendMenuA(machine, MF_STRING, IDM_POWER_CYCLE, "&Power Cycle");
    AppendMenuA(machine, MF_STRING | (h89session_get_int(g_session, "machine.warp", 0) ? MF_CHECKED : 0),
                IDM_WARP, "&Warp Speed");
    AppendMenuA(machine, MF_STRING, IDM_IMPORT_ROMS, "Import System &ROMs...");
    AppendMenuA(machine, MF_SEPARATOR, 0, NULL);
    AppendMenuA(machine, MF_STRING, IDM_SETTINGS, "Se&ttings...\tCtrl+,");
    AppendMenuA(machine, MF_SEPARATOR, 0, NULL);
    AppendMenuA(machine, MF_STRING, IDM_EXIT, "E&xit\tCtrl+Shift+Q");

    g_drives_menu = CreatePopupMenu();
    fill_drives_menu();

    AppendMenuA(edit, MF_STRING, IDM_COPY_SCREEN, "&Copy Screen Text\tCtrl+Shift+C");
    AppendMenuA(edit, MF_STRING, IDM_PASTE, "&Paste\tCtrl+Shift+V");

    AppendMenuA(view, MF_STRING, IDM_KEYS, "H19 &Keys\tF9");
    AppendMenuA(view, MF_STRING, IDM_DEBUGGER, "&Debugger\tF12");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING | (g_aspect ? MF_CHECKED : 0), IDM_TV_ASPECT, "&TV Aspect");
    AppendMenuA(view, MF_STRING | (g_smooth ? MF_CHECKED : 0), IDM_SMOOTH, "&Smooth Scaling");
    AppendMenuA(view, MF_STRING, IDM_FULLSCREEN, "&Fullscreen\tF11");

    AppendMenuA(fuji, MF_STRING, IDM_FUJINET_CONFIG, "&Configuration");
    AppendMenuA(fuji, MF_STRING, IDM_FUJINET_LOG, "Console &Log");
    AppendMenuA(fuji, MF_STRING, IDM_FUJINET_RESTART, "&Restart FujiNet");

    AppendMenuA(help, MF_STRING, IDM_ABOUT, "&About " APP_TITLE);

    AppendMenuA(bar, MF_POPUP, (UINT_PTR)machine, "&Machine");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)g_drives_menu, "&Drives");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)edit, "&Edit");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)view, "&View");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)fuji, "&FujiNet");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)help, "&Help");
    SetMenu(hwnd, bar);
}

static void toggle_fullscreen(HWND hwnd)
{
    DWORD style = GetWindowLong(hwnd, GWL_STYLE);
    if (!g_fullscreen) {
        MONITORINFO mi;
        memset(&mi, 0, sizeof mi);
        mi.cbSize = sizeof mi;
        g_placement.length = sizeof g_placement;
        GetWindowPlacement(hwnd, &g_placement);
        if (GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetMenu(hwnd, NULL);
            ShowWindow(g_statusbar, SW_HIDE);
            SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            g_fullscreen = 1;
        }
    } else {
        SetWindowLong(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        build_menu(hwnd);
        ShowWindow(g_statusbar, SW_SHOW);
        SetWindowPlacement(hwnd, &g_placement);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = 0;
    }
    InvalidateRect(hwnd, NULL, TRUE);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (g_statusbar)
            SendMessageA(g_statusbar, WM_SIZE, 0, 0);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_DRAWITEM:
        if (wp == IDC_STATUSBAR) {
            draw_status_dot((const DRAWITEMSTRUCT *)lp);
            return TRUE;
        }
        break;
    case WM_INITMENUPOPUP:
        if ((HMENU)wp == g_drives_menu)
            fill_drives_menu();
        break;
    case WM_KEYDOWN:
    case WM_CHAR:
        if (h89w_key_message(hwnd, msg, wp, lp))
            return 0;
        break;
    case WM_TIMER:
        if (wp == IDT_STATUS)
            update_status();
        else if (wp == IDT_FAST)
            fast_tick();
        else if (wp == IDT_FLASH) {
            KillTimer(hwnd, IDT_FLASH);
            g_flash = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_APP + 1:
        rom_setup();
        return 0;
    case WM_COMMAND: {
        const UINT cmd = LOWORD(wp);
        HINSTANCE inst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
        if (cmd >= IDM_DRIVE_BASE && cmd < IDM_DRIVE_BASE + 16 * H89_CTRL_COUNT * 4) {
            drive_command((int)cmd);
            return 0;
        }
        if (cmd >= IDM_CHOOSE_BASE && cmd < IDM_CHOOSE_BASE + 16 * H89_CTRL_COUNT) {
            insert_into((int)(cmd - IDM_CHOOSE_BASE), g_choose_path);
            return 0;
        }
        switch (cmd) {
        case IDM_RESET: h89session_reset(g_session); return 0;
        case IDM_POWER_CYCLE: h89w_restart(0); return 0;
        case IDM_WARP: {
            int on = !h89session_warp(g_session);
            h89session_set_warp(g_session, on);
            CheckMenuItem(GetMenu(hwnd), IDM_WARP, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
            update_status();
            return 0;
        }
        case IDM_IMPORT_ROMS:
            h89w_import_roms(hwnd);
            if (!h89session_is_running(g_session))
                h89w_start();
            return 0;
        case IDM_SETTINGS: h89w_show_settings(hwnd); return 0;
        case IDM_EXIT: PostMessage(hwnd, WM_CLOSE, 0, 0); return 0;
        case IDM_INSERT_DISK: {
            char path[MAX_PATH];
            if (pick_file("Insert a Disk", kAnyFilter, path, sizeof path))
                load_media(path);
            return 0;
        }
        case IDM_NEW_DISK: new_blank_disk(); return 0;
        case IDM_DISKS_FOLDER:
            CreateDirectoryA(h89session_disks_path(g_session), NULL);
            ShellExecuteA(hwnd, "open", h89session_disks_path(g_session), NULL, NULL, SW_SHOWNORMAL);
            return 0;
        case IDM_COPY_SCREEN: copy_screen(); return 0;
        case IDM_PASTE: paste(); return 0;
        case IDM_KEYS: h89w_keys_toggle(hwnd); return 0;
        case IDM_DEBUGGER: h89_debugger_toggle(hwnd, g_session); return 0;
        case IDM_FULLSCREEN: toggle_fullscreen(hwnd); return 0;
        case IDM_TV_ASPECT:
            h89session_set_int(g_session, "view.aspect", g_aspect ? 1 : 0);
            h89w_apply_view();
            return 0;
        case IDM_SMOOTH:
            h89session_set_int(g_session, "view.smooth", g_smooth ? 0 : 1);
            h89w_apply_view();
            return 0;
        case IDM_FUJINET_LOG: show_fujinet_log(inst); return 0;
        case IDM_FUJINET_CONFIG:
            if (h89session_fujinet_external(g_session)) {
                MessageBoxA(hwnd, "FujiNet is external: use its own web UI.", "FujiNet", MB_ICONINFORMATION | MB_OK);
                return 0;
            }
            if (!h89session_fujinet_running(g_session)) {
                MessageBoxA(hwnd, "FujiNet is not running.", "FujiNet", MB_ICONINFORMATION | MB_OK);
                return 0;
            }
            ShellExecuteA(hwnd, "open", h89session_fujinet_webui_url(g_session), NULL, NULL, SW_SHOWNORMAL);
            return 0;
        case IDM_FUJINET_RESTART:
            if (h89session_fujinet_external(g_session)) {
                MessageBoxA(hwnd, "FujiNet is external: restart it where it runs.", "FujiNet",
                            MB_ICONINFORMATION | MB_OK);
                return 0;
            }
            if (h89session_fujinet_restart(g_session) != 0)
                MessageBoxA(hwnd, h89session_last_error(g_session), "Cannot Restart FujiNet", MB_ICONWARNING | MB_OK);
            return 0;
        case IDM_ABOUT:
            MessageBoxA(hwnd,
                APP_TITLE " " H89_VERSION_STRING "\n\n"
                "A Heathkit H89 with a FujiNet card.\n"
                "The emulator is VirtualH89 (BSD-3-Clause, Mark Garlanger and\n"
                "Douglas Miller), adapted, with the FujiNet firmware\n"
                "(GPL-3.0-or-later) as the card's ESP32. The Heath ROMs are not\n"
                "included: they are imported from MAME's sets.\n\n"
                "Copyright (C) 2026 Thomas Cherryhomes -- GPL-3.0-or-later\n"
                "https://fujinet.online/",
                "About " APP_TITLE, MB_ICONINFORMATION | MB_OK);
            return 0;
        default:
            break;
        }
        break;
    }
    case WM_DROPFILES: {
        char path[MAX_PATH];
        HDROP drop = (HDROP)wp;
        if (DragQueryFileA(drop, 0, path, sizeof path))
            load_media(path);
        DragFinish(drop);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, IDT_STATUS);
        KillTimer(hwnd, IDT_FAST);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static int env_on(const char *name)
{
    const char *env = getenv(name);
    return env && *env && *env != '0';
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    WNDCLASSEX wc;
    MSG msg;
    RECT want;
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    int parts[3] = { h89w_px(DOT_PART_W), h89w_px(DOT_PART_W * 2), -1 };
    char media[MAX_PATH] = "";
    (void)prev;

    InitCommonControlsEx(&icc);
    InitializeCriticalSection(&g_fb_lock);

    g_session = h89session_new(NULL);
    if (!g_session) {
        MessageBoxA(NULL, "Could not create the session (unusable config or data directories?)", APP_TITLE,
                    MB_ICONERROR | MB_OK);
        return 1;
    }
    g_fb = calloc((size_t)H89SESSION_FB_WIDTH * H89SESSION_FB_HEIGHT, sizeof *g_fb);
    if (!g_fb)
        return 1;
    g_accent_brush = CreateSolidBrush(RGB((H89SESSION_ACCENT_RGB >> 16) & 0xff,
                                          (H89SESSION_ACCENT_RGB >> 8) & 0xff, H89SESSION_ACCENT_RGB & 0xff));
    memset(&g_bmi, 0, sizeof g_bmi);
    g_bmi.bmiHeader.biSize = sizeof g_bmi.bmiHeader;
    g_bmi.bmiHeader.biWidth = H89SESSION_FB_WIDTH;
    g_bmi.bmiHeader.biHeight = -H89SESSION_FB_HEIGHT;   /* top-down */
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;
    g_aspect = h89session_get_int(g_session, "view.aspect", 0) == 0;
    g_smooth = h89session_get_int(g_session, "view.smooth", 1) != 0;
    h89session_apply_view(g_session);

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = WIN_CLASS;
    wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_APPICON));
    wc.hIconSm = wc.hIcon;
    RegisterClassEx(&wc);

    /* the H19's 640 x 500 at 4:3, half again, as much of it as the screen
     * has room for */
    {
        RECT work;
        int w = h89w_px(1000), h = h89w_px(750);
        if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0) && h > (work.bottom - work.top) * 4 / 5) {
            h = (work.bottom - work.top) * 4 / 5;
            w = h * 4 / 3;
        }
        SetRect(&want, 0, 0, w, h);
    }
    AdjustWindowRectEx(&want, WS_OVERLAPPEDWINDOW, TRUE, WS_EX_ACCEPTFILES);
    g_hwnd = CreateWindowEx(WS_EX_ACCEPTFILES, WIN_CLASS, APP_TITLE, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, want.right - want.left, want.bottom - want.top + h89w_px(24), NULL,
                            NULL, inst, NULL);
    if (!g_hwnd)
        return 1;
    build_menu(g_hwnd);
    g_statusbar = CreateWindowExA(0, STATUSCLASSNAMEA, "", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                                  g_hwnd, (HMENU)(INT_PTR)IDC_STATUSBAR, inst, NULL);
    SendMessageA(g_statusbar, SB_SETPARTS, 3, (LPARAM)parts);
    ShowWindow(g_hwnd, show);

    /* FujiNet first, then the machine; without its ROMs, ask for them */
    h89w_start();
    if (cmdline && *cmdline) {
        /* a path on the command line, quotes and all: routed like a drop */
        const char *p = cmdline;
        size_t n;
        if (*p == '"')
            p++;
        snprintf(media, sizeof media, "%s", p);
        n = strlen(media);
        while (n && (media[n - 1] == '"' || media[n - 1] == ' '))
            media[--n] = '\0';
        if (media[0])
            load_media(media);
    }

    SetTimer(g_hwnd, IDT_STATUS, 1000, NULL);
    SetTimer(g_hwnd, IDT_FAST, 250, NULL);
    update_status();

    if (env_on("H89_OPEN_KEYS"))
        h89w_keys_toggle(g_hwnd);
    if (env_on("H89_OPEN_DEBUGGER"))
        h89_debugger_show(g_hwnd, g_session);
    if (env_on("H89_OPEN_SETTINGS"))
        h89w_show_settings(g_hwnd);

    g_present_thread = CreateThread(NULL, 0, present_thread, NULL, 0, NULL);

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        /* before TranslateMessage, so the sub-windows' keys reach them
         * whichever child control has the focus */
        if (h89_debugger_pretranslate(&msg))
            continue;
        if (h89w_keys_pretranslate(&msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    InterlockedExchange(&g_running, 0);
    if (g_present_thread) {
        WaitForSingleObject(g_present_thread, 2000);
        CloseHandle(g_present_thread);
    }
    h89session_stop(g_session);
    h89session_free(g_session);
    free(g_fb);
    DeleteObject(g_accent_brush);
    DeleteCriticalSection(&g_fb_lock);
    return (int)msg.wParam;
}
