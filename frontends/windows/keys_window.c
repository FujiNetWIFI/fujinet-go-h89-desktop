/*
 * keys_window.c -- the H19 Keys window (F9): every key of the H19's
 * keyboard a PC keyboard does not have, as buttons, a SHIFT latch, and
 * lights for the terminal's modes. Typing with it active types on the H19
 * (h89w_key_message), and its buttons never take the focus from it.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <windows.h>

#include <stdio.h>
#include <string.h>

#include "resource.h"
#include "win_common.h"

#define KEY_ID_BASE 3000
#define ID_SHIFT    2999
#define IDT_LIGHTS  1

static HWND g_keys;
static HWND g_shift;
static HWND g_lights[8];

typedef struct {
    int key;
    const char *label;
} KeyDef;

static const KeyDef kRows[4][9] = {
    { { H89K_F1, "f1" }, { H89K_F2, "f2" }, { H89K_F3, "f3" }, { H89K_F4, "f4" }, { H89K_F5, "f5" },
      { H89K_ERASE, "ERASE" }, { H89K_BLUE, "BLUE" }, { H89K_RED, "RED" }, { H89K_GRAY, "GRAY" } },
    { { 0x1b, "ESC" }, { 0x09, "TAB" }, { 0x08, "BACK SP" }, { 0x7f, "DELETE" },
      { H89K_LINEFEED, "LINE FEED" }, { 0x0d, "RETURN" }, { 0, NULL } },
    { { H89K_BREAK, "BREAK" }, { H89K_OFFLINE, "OFF LINE" }, { H89K_SCROLL, "SCROLL" },
      { H89K_RESET, "SHIFT+RESET" }, { 0, NULL } },
};
static const KeyDef kPad[12] = {
    { H89K_KP7, "7 IC" }, { H89K_KP8, "8 \xe2\x86\x91" }, { H89K_KP9, "9 DC" },
    { H89K_KP4, "4 \xe2\x86\x90" }, { H89K_KP5, "5 HOME" }, { H89K_KP6, "6 \xe2\x86\x92" },
    { H89K_KP1, "1 IL" }, { H89K_KP2, "2 \xe2\x86\x93" }, { H89K_KP3, "3 DL" },
    { H89K_KP0, "0" }, { H89K_KP_DOT, "." }, { H89K_KP_ENTER, "ENTER" },
};
static const char *const kLightNames[8] = {
    "Off line", "Hold screen", "Keypad shifted", "Alternate keypad",
    "Keyboard off", "Graphics", "Insert", "Reverse",
};

static void refresh_lights(void)
{
    unsigned m = h89session_h19_modes(g_session);
    for (int i = 0; i < 8; i++) {
        char text[64];
        snprintf(text, sizeof text, "%s  %s", (m >> i) & 1 ? "\xe2\x97\x8f" : "\xe2\x97\x8b", kLightNames[i]);
        {
            wchar_t w[64];
            MultiByteToWideChar(CP_UTF8, 0, text, -1, w, 64);
            SetWindowTextW(g_lights[i], w);
        }
    }
}

static LRESULT CALLBACK keys_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= KEY_ID_BASE) {
            int shift = SendMessageA(g_shift, BM_GETCHECK, 0, 0) == BST_CHECKED;
            int key = id - KEY_ID_BASE;
            h89session_key(g_session, key, shift);
            if (shift && key != H89K_RESET)
                SendMessageA(g_shift, BM_SETCHECK, BST_UNCHECKED, 0);
            SetFocus(hwnd);
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
    case WM_CHAR:
        if (msg == WM_KEYDOWN && wp == VK_F9) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (h89w_key_message(hwnd, msg, wp, lp))
            return 0;
        break;
    case WM_TIMER:
        refresh_lights();
        return 0;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, IDT_LIGHTS);
        g_keys = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static HWND button(HWND parent, HINSTANCE inst, const char *text, int id, int x, int y, int w, int h, DWORD extra)
{
    wchar_t wide[64];
    HWND b;
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, 64);
    b = CreateWindowW(L"BUTTON", wide, WS_CHILD | WS_VISIBLE | extra, h89w_px(x), h89w_px(y), h89w_px(w),
                      h89w_px(h), parent, (HMENU)(INT_PTR)id, inst, NULL);
    h89w_set_font(b, h89w_font());
    return b;
}

static HWND label(HWND parent, HINSTANCE inst, const char *text, int x, int y, int w)
{
    HWND l = CreateWindowA("STATIC", text, WS_CHILD | WS_VISIBLE, h89w_px(x), h89w_px(y), h89w_px(w), h89w_px(18),
                           parent, NULL, inst, NULL);
    h89w_set_font(l, h89w_font());
    return l;
}

void h89w_keys_toggle(HWND owner)
{
    static int registered;
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtr(owner, GWLP_HINSTANCE);
    RECT want;
    int y = 10;

    if (g_keys) {
        ShowWindow(g_keys, IsWindowVisible(g_keys) ? SW_HIDE : SW_SHOW);
        if (IsWindowVisible(g_keys))
            SetForegroundWindow(g_keys);
        return;
    }
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = keys_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "H89KeysWindow";
        wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(IDI_APPICON));
        RegisterClassA(&wc);
        registered = 1;
    }
    g_keys = CreateWindowExA(WS_EX_TOOLWINDOW, "H89KeysWindow", "H19 Keys",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 640, 400,
                             owner, NULL, inst, NULL);
    label(g_keys, inst, "Function keys", 12, y, 200);
    y += 20;
    for (int i = 0; i < 9; i++)
        button(g_keys, inst, kRows[0][i].label, KEY_ID_BASE + kRows[0][i].key, 12 + i * 68, y, 64, 28, 0);
    y += 38;
    label(g_keys, inst, "Keys", 12, y, 200);
    y += 20;
    for (int i = 0; kRows[1][i].label; i++)
        button(g_keys, inst, kRows[1][i].label, KEY_ID_BASE + kRows[1][i].key, 12 + i * 100, y, 96, 28, 0);
    y += 38;
    label(g_keys, inst, "Terminal", 12, y, 200);
    y += 20;
    g_shift = button(g_keys, inst, "SHIFT", ID_SHIFT, 12, y, 80, 28, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    for (int i = 0; kRows[2][i].label; i++)
        button(g_keys, inst, kRows[2][i].label, KEY_ID_BASE + kRows[2][i].key, 100 + i * 112, y, 108, 28, 0);
    y += 38;
    label(g_keys, inst, "Keypad", 12, y, 200);
    label(g_keys, inst, "Modes", 300, y, 200);
    y += 20;
    for (int i = 0; i < 12; i++)
        button(g_keys, inst, kPad[i].label, KEY_ID_BASE + kPad[i].key, 12 + (i % 3) * 88, y + (i / 3) * 36, 84, 32, 0);
    for (int i = 0; i < 8; i++)
        g_lights[i] = label(g_keys, inst, "", 300, y + i * 18, 220);
    y += 4 * 36 + 10;

    SetRect(&want, 0, 0, h89w_px(640), h89w_px(y));
    AdjustWindowRectEx(&want, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW);
    SetWindowPos(g_keys, NULL, 0, 0, want.right - want.left, want.bottom - want.top, SWP_NOMOVE | SWP_NOZORDER);
    refresh_lights();
    SetTimer(g_keys, IDT_LIGHTS, 250, NULL);
    ShowWindow(g_keys, SW_SHOW);
}

/* Keys typed with a button focused go to the window (and so the H19),
 * not to the button: SPACE does not press it. */
int h89w_keys_pretranslate(MSG *msg)
{
    if (!g_keys || !IsChild(g_keys, msg->hwnd))
        return 0;
    if (msg->message == WM_KEYDOWN || msg->message == WM_KEYUP || msg->message == WM_CHAR) {
        msg->hwnd = g_keys;
        TranslateMessage(msg);
        DispatchMessage(msg);
        return 1;
    }
    return 0;
}
