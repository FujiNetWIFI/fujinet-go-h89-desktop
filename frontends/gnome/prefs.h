/*
 * Preferences dialog for the GNOME frontend.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <adwaita.h>

#include "h89session.h"
#include "window.h"

G_BEGIN_DECLS

/* Shows the Preferences. The view and sound settings apply live. The
 * machine -- model, memory, monitor, switches, what is in the slots and the
 * drives -- is edited as a whole and checked (h89_config_validate) when the
 * dialog closes: a configuration that can be built is stored, and the
 * H89 power-cycles onto it if you say so; one that cannot says why and is
 * not stored. FujiNet and audio on/off restart the session. */
void h89_prefs_show(H89Window *parent, h89session *session);

G_END_DECLS
