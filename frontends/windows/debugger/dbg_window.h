/*
 * The debugger window (F12), Win32.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <windows.h>

#include "h89session.h"

/* Shows the debugger (attaching it stops the machine), or hides it. */
void h89_debugger_show(HWND owner, h89session *session);
void h89_debugger_toggle(HWND owner, h89session *session);
/* The window's keys (F5, F7, F8, Shift+F8, F12, and the prompt's Enter,
 * Tab and history) before TranslateMessage; 1 when taken. */
int  h89_debugger_pretranslate(MSG *msg);
