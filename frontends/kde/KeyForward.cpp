/*
 * KeyForward -- see KeyForward.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KeyForward.h"

int h89KeyFromQt(const QKeyEvent *e)
{
    const Qt::KeyboardModifiers m = e->modifiers();
    const bool ctrl = m & Qt::ControlModifier;
    const bool shift = m & Qt::ShiftModifier;
    const int k = e->key();
    uint32_t ks = h89session_keysym_from_qt_key((unsigned)k, (m & Qt::KeypadModifier) != 0);
    uint32_t uc = 0;

    if (ctrl && k >= Qt::Key_A && k <= Qt::Key_Z) {
        uc = 'a' + (uint32_t)(k - Qt::Key_A);
    } else if (ctrl && k >= 0x20 && k < 0x7f) {
        uc = (uint32_t)k;
    } else {
        const QString t = e->text();
        if (!t.isEmpty() && t.at(0).unicode() >= 0x20 && t.at(0).unicode() != 0x7f)
            uc = t.at(0).unicode();
    }
    if (!ks && uc < 0x80)
        ks = uc;
    return h89session_key_from_event(ks, uc, ctrl, shift);
}

bool h89ForwardKey(h89session *session, const QKeyEvent *e)
{
    const Qt::KeyboardModifiers m = e->modifiers();
    const int k = e->key();
    if (k == Qt::Key_F9 || k == Qt::Key_F11 || k == Qt::Key_F12)
        return false;
    if ((m & (Qt::AltModifier | Qt::MetaModifier)) ||
        ((m & Qt::ControlModifier) && (m & Qt::ShiftModifier)))
        return false;
    const int key = h89KeyFromQt(e);
    if (key < 0)
        return false;
    h89session_key(session, key, (m & Qt::ShiftModifier) != 0);
    return true;
}
