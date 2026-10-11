/*
 * The debugger window (F12).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <QWidget>

#include "h89session.h"

namespace DebuggerWindow {
/* Shows the debugger (attaching it stops the machine), or hides it. */
void showFor(QWidget *parent, h89session *session);
void toggleFor(QWidget *parent, h89session *session);
}
