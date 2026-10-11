/*
 * The H19 Keys panel (F9): every key of the H19's keyboard a PC keyboard
 * does not have, or has somewhere else, as buttons -- f1-f5, ERASE, the
 * blue/red/gray keys, BREAK, OFF LINE, SCROLL, the keypad with its shifted
 * functions, SHIFT+RESET -- and lights for the terminal's modes. It takes
 * the family's Controllers slot: the H89 has no game ports.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <adwaita.h>

#include "h89session.h"

G_BEGIN_DECLS

/* Shows the panel, or hides it when it is showing. */
void h89_keys_panel_toggle(GtkWindow *parent, h89session *session);

G_END_DECLS
