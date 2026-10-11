/*
 * AppKit key events to the H19: the hardware key code through the session's
 * table for the special keys, the character for everything else -- the same
 * route the other frontends take (h89session_key_from_event).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#import <Cocoa/Cocoa.h>
#include <stdint.h>

#include "h89session.h"

/* The keysym for a key event (an X11 keysym), or 0 for one the H19 has no
 * use for. */
uint32_t H89KeysymFromEvent(NSEvent *event);

/* Types a key-down on the H19. Returns YES when the H19 took it. F9, F11
 * and F12 are not the H19's: the caller handles them first. */
BOOL H89ForwardKeyDown(h89session *session, NSEvent *event);

/* This app's accent colour (H89SESSION_ACCENT_RGB). */
NSColor *H89AccentColor(void);
