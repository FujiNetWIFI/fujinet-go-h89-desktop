/*
 * What the Windows frontend's windows share: the session, the main window
 * and a few of its services.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <windows.h>

#include "h89session.h"

#define APP_TITLE "FujiNet Go H89"

extern h89session *g_session;
extern HWND g_hwnd;
extern HBRUSH g_accent_brush;

/* The view settings (aspect, smoothing) re-read and applied. */
void h89w_apply_view(void);
/* Power cycle onto the settings, or restart the whole session (FujiNet or
 * audio switched). */
void h89w_restart(int host_options_changed);
/* Starts the machine; without its ROMs, offers to import them. */
void h89w_start(void);
/* Imports ROM files the user picks (a multi-select dialog over `owner`);
 * returns how many images were imported. */
int  h89w_import_roms(HWND owner);
/* A key message from any of the app's windows, for the H19 (the keys window
 * and the main window share it). Returns 1 when it was used. */
int  h89w_key_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/* The system's message font, and a monospaced one, both at the screen's DPI. */
HFONT h89w_font(void);
HFONT h89w_mono_font(void);
void  h89w_set_font(HWND h, HFONT f);
/* Layout units (pixels at 96 DPI) to the screen's pixels: the manifest
 * declares the app DPI aware, so Windows does not scale its windows. */
int   h89w_px(int v);

/* prefs.c */
void h89w_show_settings(HWND owner);
/* keys_window.c */
void h89w_keys_toggle(HWND owner);
int  h89w_keys_pretranslate(MSG *msg);
