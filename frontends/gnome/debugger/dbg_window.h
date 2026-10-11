/*
 * The debugger window (F12).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <adwaita.h>

#include "h89session.h"

G_BEGIN_DECLS

/* Shows the debugger (attaching it stops the machine), or hides it. */
void h89_debugger_toggle(GtkWindow *parent, h89session *session);
void h89_debugger_show(GtkWindow *parent, h89session *session);

G_END_DECLS
