/*
 * h89keys -- the H19 keyboard's keys, as the session and the core name them.
 *
 * A key is either an ASCII code (0x00-0x7f: letters, digits, punctuation,
 * and the control codes CTRL produces) or one of the H89K_* codes below for
 * the keys that send escape sequences or act on the terminal itself. The H19
 * encodes them according to its modes when they reach it (keypad shifted,
 * alternate keypad, off-line), so the codes name keys, not byte sequences.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89KEYS_H
#define H89KEYS_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    H89K_NONE = 0,

    H89K_F1 = 0x100,          /* ESC S */
    H89K_F2,                  /* ESC T */
    H89K_F3,                  /* ESC U */
    H89K_F4,                  /* ESC V */
    H89K_F5,                  /* ESC W */
    H89K_ERASE,               /* ESC J */
    H89K_BLUE,                /* ESC P */
    H89K_RED,                 /* ESC Q */
    H89K_GRAY,                /* ESC R */

    H89K_UP,                  /* ESC A (the keypad's shifted 8) */
    H89K_DOWN,                /* ESC B (2) */
    H89K_RIGHT,               /* ESC C (6) */
    H89K_LEFT,                /* ESC D (4) */
    H89K_HOME,                /* ESC H (5) */
    H89K_IC,                  /* ESC @ (7) insert character */
    H89K_DC,                  /* ESC N (9) delete character */
    H89K_IL,                  /* ESC L (1) insert line */
    H89K_DL,                  /* ESC M (3) delete line */

    /* The keypad keys themselves: digits unshifted, the functions above
     * shifted (or in keypad-shifted mode), ESC ? x in alternate keypad
     * mode. */
    H89K_KP0, H89K_KP1, H89K_KP2, H89K_KP3, H89K_KP4,
    H89K_KP5, H89K_KP6, H89K_KP7, H89K_KP8, H89K_KP9,
    H89K_KP_DOT,
    H89K_KP_ENTER,

    H89K_BREAK,               /* a break on the line: the 8250 sees BI */
    H89K_OFFLINE,             /* OFF LINE: toggles local mode */
    H89K_SCROLL,              /* SCROLL: releases hold-screen mode */
    H89K_LINEFEED,            /* LF */
    H89K_RESET,               /* SHIFT+RESET: the session resets the machine */

    H89K_LAST
};

#ifdef __cplusplus
}
#endif

#endif /* H89KEYS_H */
