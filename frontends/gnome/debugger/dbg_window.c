/*
 * dbg_window.c -- the debugger window. The engine (core/debugger) and this
 * window arrive together with milestone M5; until then F12 says so.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dbg_window.h"

void h89_debugger_show(GtkWindow *parent, h89session *session)
{
    AdwDialog *dlg;
    (void)session;
    dlg = adw_alert_dialog_new("Debugger", "The debugger is not in this build yet.");
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dlg), "ok", "OK");
    adw_dialog_present(dlg, GTK_WIDGET(parent));
}

void h89_debugger_toggle(GtkWindow *parent, h89session *session)
{
    h89_debugger_show(parent, session);
}
