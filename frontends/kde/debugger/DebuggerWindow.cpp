/*
 * DebuggerWindow -- the engine (core/debugger) and this window arrive
 * together with milestone M5; until then F12 says so.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "DebuggerWindow.h"

#include <QMessageBox>

void DebuggerWindow::showFor(QWidget *parent, h89session *session)
{
    (void)session;
    QMessageBox::information(parent, QStringLiteral("Debugger"),
                             QStringLiteral("The debugger is not in this build yet."));
}

void DebuggerWindow::toggleFor(QWidget *parent, h89session *session)
{
    showFor(parent, session);
}
