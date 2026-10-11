/*
 * The main application window.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <adwaita.h>

#include "h89session.h"

#define H89_TYPE_WINDOW (h89_window_get_type())
G_DECLARE_FINAL_TYPE(H89Window, h89_window, H89, WINDOW, AdwApplicationWindow)

GtkWidget *h89_window_new(AdwApplication *app, h89session *session);
/* main.c: the icon name to use (the installed id, or the in-tree art). */
const char *h89_icon_name(void);
void h89_window_toast(H89Window *self, const char *text);

/* Starts the machine (FujiNet first). When the ROMs it needs are not
 * imported yet, says which and offers to import them instead. */
void h89_window_start(H89Window *self);

/* A key typed with this window, or one of its panels, active: the window's
 * own keys (F9, F11, F12), else the H19's. TRUE when it was used. */
gboolean h89_window_key(H89Window *self, guint keyval, GdkModifierType state);

/* The display's shape: 0 = the CRT's 4:3, 1 = square pixels. */
void h89_window_apply_aspect(H89Window *self, int aspect);
void h89_window_apply_smooth(H89Window *self, int smooth);

/* A dropped or command-line file: ROMs are imported, a disk goes in a drive
 * that takes it (asking which when they are all full), anything else is
 * copied to FujiNet's SD folder. */
void h89_window_load_media(H89Window *self, const char *path);

/* Imports ROMs from `path` and says what happened over `over`; returns how
 * many images were imported. */
int h89_show_rom_import(GtkWidget *over, h89session *session, const char *path);

/* The accent colour (H89SESSION_ACCENT_RGB) as CSS classes every window can
 * use: ".h89-accent" paints a widget's background in it, ".h89-accent-text"
 * its label, ".h89-dot" with "-on"/"-off" a status light. Installed once by
 * the main window. */
void h89_install_accent_css(void);

/* Power-cycles onto the configuration in the settings, or, when FujiNet or
 * audio were switched, stops and starts the whole session. */
void h89_window_restart(H89Window *self, gboolean host_options_changed);
