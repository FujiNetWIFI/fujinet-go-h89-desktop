/*
 * The H19's screen: a GtkWidget that pulls frames from the session on the
 * compositor's frame clock.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <adwaita.h>

#include "h89session.h"

#define H89_TYPE_DISPLAY (h89_display_get_type())
G_DECLARE_FINAL_TYPE(H89Display, h89_display, H89, DISPLAY, GtkWidget)

GtkWidget *h89_display_new(h89session *session);
/* The CRT's 4:3, or the scan's own shape (640 x 250 lines, doubled). */
void h89_display_set_tv_aspect(H89Display *self, gboolean tv);
void h89_display_set_smooth(H89Display *self, gboolean smooth);
