/*
 * keymap -- the H19 keyboard from a desktop keyboard (h89session.h's
 * h89session_key_from_event and the native-code tables). Pure functions,
 * in the core so core/tests/keymap_test.c can check them without SDL.
 *
 * The defaults are MAME's H19 key assignments where the family's own keys
 * allow: F1-F5 are f1-f5, F6 ERASE, F7 BLUE, F8 RED, F10 GRAY (F9, F11 and
 * F12 belong to the app: the H19 Keys panel, full screen, the debugger);
 * Pause is BREAK, Scroll Lock SCROLL, Insert IC. The keypad with Num Lock
 * on is the H19's keypad (digits, or ESC ? x in alternate keypad mode);
 * with Num Lock off its keys are the H19 keypad's shifted functions --
 * IC/up/DC, left/HOME/right, IL/down/DL -- as MAME labels them.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "h89session.h"

int h89session_key_from_event(uint32_t keysym, uint32_t unicode, int ctrl, int shift)
{
    (void)shift;

    /* CTRL with a letter or @[\]^_ (and space): the control codes */
    if (ctrl)
    {
        uint32_t c = unicode ? unicode : (keysym < 0x80 ? keysym : 0);
        if (c >= 'a' && c <= 'z')
            return (int)(c - 'a' + 1);
        if ((c >= '@' && c <= '_') || c == ' ')
            return (int)(c & 0x1f);
        if (c == '2')
            return 0;            /* CTRL-@ on a PC layout */
        if (c == '6')
            return 0x1e;         /* CTRL-^ */
        if (c == '-')
            return 0x1f;         /* CTRL-_ */
    }

    switch (keysym)
    {
    case H89_KEYSYM_BACKSPACE: return 0x08;
    case H89_KEYSYM_TAB: return 0x09;
    case H89_KEYSYM_RETURN: return 0x0d;
    case H89_KEYSYM_LINEFEED: return H89K_LINEFEED;
    case H89_KEYSYM_ESCAPE: return 0x1b;
    case H89_KEYSYM_DELETE: return 0x7f;

    case H89_KEYSYM_UP: return H89K_UP;
    case H89_KEYSYM_DOWN: return H89K_DOWN;
    case H89_KEYSYM_LEFT: return H89K_LEFT;
    case H89_KEYSYM_RIGHT: return H89K_RIGHT;
    case H89_KEYSYM_HOME: return H89K_HOME;
    case H89_KEYSYM_INSERT: return H89K_IC;

    case H89_KEYSYM_PAUSE:
    case H89_KEYSYM_BREAK: return H89K_BREAK;
    case H89_KEYSYM_SCROLL_LOCK: return H89K_SCROLL;

    /* the keypad, Num Lock off: the shifted functions */
    case H89_KEYSYM_KP_HOME: return H89K_IC;
    case H89_KEYSYM_KP_UP: return H89K_UP;
    case H89_KEYSYM_KP_PAGE_UP: return H89K_DC;
    case H89_KEYSYM_KP_LEFT: return H89K_LEFT;
    case H89_KEYSYM_KP_BEGIN: return H89K_HOME;
    case H89_KEYSYM_KP_RIGHT: return H89K_RIGHT;
    case H89_KEYSYM_KP_END: return H89K_IL;
    case H89_KEYSYM_KP_DOWN: return H89K_DOWN;
    case H89_KEYSYM_KP_PAGE_DOWN: return H89K_DL;
    case H89_KEYSYM_KP_INSERT: return H89K_KP0;
    case H89_KEYSYM_KP_DELETE: return H89K_KP_DOT;
    case H89_KEYSYM_KP_ENTER: return H89K_KP_ENTER;
    case H89_KEYSYM_KP_DECIMAL: return H89K_KP_DOT;
    default: break;
    }

    if (keysym >= H89_KEYSYM_KP_0 && keysym <= H89_KEYSYM_KP_9)
        return H89K_KP0 + (int)(keysym - H89_KEYSYM_KP_0);

    if (keysym >= H89_KEYSYM_F1 && keysym <= H89_KEYSYM_F12)
    {
        static const int fkeys[12] = {
            H89K_F1, H89K_F2, H89K_F3, H89K_F4, H89K_F5, H89K_ERASE,
            H89K_BLUE, H89K_RED, -1 /* F9 */, H89K_GRAY, -1, -1,
        };
        return fkeys[keysym - H89_KEYSYM_F1];
    }

    if (unicode >= 0x20 && unicode < 0x7f)
        return (int)unicode;
    if (!unicode && keysym >= 0x20 && keysym < 0x7f)
        return (int)keysym;
    return -1;
}

uint32_t h89session_keysym_from_win_vk(unsigned vk, int extended)
{
    switch (vk)
    {
    case 0x08: return H89_KEYSYM_BACKSPACE;
    case 0x09: return H89_KEYSYM_TAB;
    case 0x0d: return extended ? H89_KEYSYM_KP_ENTER : H89_KEYSYM_RETURN;
    case 0x03: /* VK_CANCEL: CTRL+Break */
    case 0x13: return H89_KEYSYM_PAUSE;
    case 0x1b: return H89_KEYSYM_ESCAPE;
    case 0x91: return H89_KEYSYM_SCROLL_LOCK;
    /* the navigation keys: the dedicated ones are "extended", the keypad's
     * (Num Lock off) are not */
    case 0x21: return extended ? H89_KEYSYM_PAGE_UP : H89_KEYSYM_KP_PAGE_UP;
    case 0x22: return extended ? H89_KEYSYM_PAGE_DOWN : H89_KEYSYM_KP_PAGE_DOWN;
    case 0x23: return extended ? H89_KEYSYM_END : H89_KEYSYM_KP_END;
    case 0x24: return extended ? H89_KEYSYM_HOME : H89_KEYSYM_KP_HOME;
    case 0x25: return extended ? H89_KEYSYM_LEFT : H89_KEYSYM_KP_LEFT;
    case 0x26: return extended ? H89_KEYSYM_UP : H89_KEYSYM_KP_UP;
    case 0x27: return extended ? H89_KEYSYM_RIGHT : H89_KEYSYM_KP_RIGHT;
    case 0x28: return extended ? H89_KEYSYM_DOWN : H89_KEYSYM_KP_DOWN;
    case 0x2d: return extended ? H89_KEYSYM_INSERT : H89_KEYSYM_KP_INSERT;
    case 0x2e: return extended ? H89_KEYSYM_DELETE : H89_KEYSYM_KP_DELETE;
    case 0x0c: return H89_KEYSYM_KP_BEGIN;   /* VK_CLEAR: keypad 5, Num Lock off */
    case 0x6e: return H89_KEYSYM_KP_DECIMAL;
    default: break;
    }
    if (vk >= 0x60 && vk <= 0x69)
        return H89_KEYSYM_KP_0 + (vk - 0x60);
    if (vk >= 0x70 && vk <= 0x7b)
        return H89_KEYSYM_F1 + (vk - 0x70);
    return 0;
}

uint32_t h89session_keysym_from_macos_keycode(unsigned kc)
{
    switch (kc)
    {
    case 0x24: return H89_KEYSYM_RETURN;
    case 0x30: return H89_KEYSYM_TAB;
    case 0x33: return H89_KEYSYM_BACKSPACE;
    case 0x35: return H89_KEYSYM_ESCAPE;
    case 0x75: return H89_KEYSYM_DELETE;
    case 0x73: return H89_KEYSYM_HOME;
    case 0x77: return H89_KEYSYM_END;
    case 0x74: return H89_KEYSYM_PAGE_UP;
    case 0x79: return H89_KEYSYM_PAGE_DOWN;
    case 0x7b: return H89_KEYSYM_LEFT;
    case 0x7c: return H89_KEYSYM_RIGHT;
    case 0x7d: return H89_KEYSYM_DOWN;
    case 0x7e: return H89_KEYSYM_UP;
    case 0x72: return H89_KEYSYM_INSERT;       /* Help/Insert */
    case 0x71: return H89_KEYSYM_PAUSE;        /* F15 on Apple keyboards */
    case 0x6b: return H89_KEYSYM_SCROLL_LOCK;  /* F14 */
    case 0x7a: return H89_KEYSYM_F1;
    case 0x78: return H89_KEYSYM_F1 + 1;
    case 0x63: return H89_KEYSYM_F1 + 2;
    case 0x76: return H89_KEYSYM_F1 + 3;
    case 0x60: return H89_KEYSYM_F1 + 4;
    case 0x61: return H89_KEYSYM_F1 + 5;
    case 0x62: return H89_KEYSYM_F1 + 6;
    case 0x64: return H89_KEYSYM_F1 + 7;
    case 0x65: return H89_KEYSYM_F1 + 8;
    case 0x6d: return H89_KEYSYM_F1 + 9;
    case 0x67: return H89_KEYSYM_F1 + 10;
    case 0x6f: return H89_KEYSYM_F1 + 11;
    case 0x52: return H89_KEYSYM_KP_0;
    case 0x53: return H89_KEYSYM_KP_0 + 1;
    case 0x54: return H89_KEYSYM_KP_0 + 2;
    case 0x55: return H89_KEYSYM_KP_0 + 3;
    case 0x56: return H89_KEYSYM_KP_0 + 4;
    case 0x57: return H89_KEYSYM_KP_0 + 5;
    case 0x58: return H89_KEYSYM_KP_0 + 6;
    case 0x59: return H89_KEYSYM_KP_0 + 7;
    case 0x5b: return H89_KEYSYM_KP_0 + 8;
    case 0x5c: return H89_KEYSYM_KP_0 + 9;
    case 0x41: return H89_KEYSYM_KP_DECIMAL;
    case 0x4c: return H89_KEYSYM_KP_ENTER;
    default: return 0;
    }
}

uint32_t h89session_keysym_from_qt_key(unsigned k, int keypad)
{
    if (keypad)
    {
        if (k >= '0' && k <= '9')
            return H89_KEYSYM_KP_0 + (k - '0');
        switch (k)
        {
        case '.': return H89_KEYSYM_KP_DECIMAL;
        case 0x01000005u: return H89_KEYSYM_KP_ENTER;      /* Key_Enter */
        case 0x01000010u: return H89_KEYSYM_KP_HOME;
        case 0x01000011u: return H89_KEYSYM_KP_END;
        case 0x01000012u: return H89_KEYSYM_KP_LEFT;
        case 0x01000013u: return H89_KEYSYM_KP_UP;
        case 0x01000014u: return H89_KEYSYM_KP_RIGHT;
        case 0x01000015u: return H89_KEYSYM_KP_DOWN;
        case 0x01000016u: return H89_KEYSYM_KP_PAGE_UP;
        case 0x01000017u: return H89_KEYSYM_KP_PAGE_DOWN;
        case 0x01000006u: return H89_KEYSYM_KP_INSERT;
        case 0x01000007u: return H89_KEYSYM_KP_DELETE;
        case 0x0100000bu: return H89_KEYSYM_KP_BEGIN;      /* Key_Clear */
        default: break;
        }
    }
    switch (k)
    {
    case 0x01000000u: return H89_KEYSYM_ESCAPE;
    case 0x01000001u: return H89_KEYSYM_TAB;
    case 0x01000003u: return H89_KEYSYM_BACKSPACE;
    case 0x01000004u: return H89_KEYSYM_RETURN;
    case 0x01000005u: return H89_KEYSYM_KP_ENTER;
    case 0x01000006u: return H89_KEYSYM_INSERT;
    case 0x01000007u: return H89_KEYSYM_DELETE;
    case 0x01000008u: return H89_KEYSYM_PAUSE;
    case 0x01000010u: return H89_KEYSYM_HOME;
    case 0x01000011u: return H89_KEYSYM_END;
    case 0x01000012u: return H89_KEYSYM_LEFT;
    case 0x01000013u: return H89_KEYSYM_UP;
    case 0x01000014u: return H89_KEYSYM_RIGHT;
    case 0x01000015u: return H89_KEYSYM_DOWN;
    case 0x01000016u: return H89_KEYSYM_PAGE_UP;
    case 0x01000017u: return H89_KEYSYM_PAGE_DOWN;
    case 0x01000026u: return H89_KEYSYM_SCROLL_LOCK;
    default: break;
    }
    if (k >= 0x01000030u && k <= 0x0100003bu)
        return H89_KEYSYM_F1 + (k - 0x01000030u);
    return 0;
}
