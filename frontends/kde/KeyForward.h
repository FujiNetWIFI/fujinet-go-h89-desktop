/*
 * Qt key events to H19 keys.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <QKeyEvent>

#include "h89session.h"

/* The H19 key a Qt key event types (an ASCII code or an H89K_* code), or -1
 * for one it has no use for. The character Qt says the key types is what
 * the H19 gets, so the layout is the user's; CTRL with a letter is its
 * control code. */
int h89KeyFromQt(const QKeyEvent *e);

/* Sends `e` to the H19 when it is one of its keys; true when it was. The
 * app's own keys (F9, F11, F12, CTRL+SHIFT shortcuts) are not. */
bool h89ForwardKey(h89session *session, const QKeyEvent *e);
