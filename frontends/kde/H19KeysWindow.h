/*
 * The H19 Keys window (F9): every key of the H19's keyboard a PC keyboard
 * does not have, as buttons, and lights for the terminal's modes. It takes
 * the family's Controllers slot: the H89 has no game ports.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <QLabel>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include "h89session.h"

class H19KeysWindow : public QWidget {
    Q_OBJECT
public:
    explicit H19KeysWindow(h89session *session, QWidget *parent = nullptr);

protected:
    /* typing with this window active still types on the H19 */
    void keyPressEvent(QKeyEvent *e) override;

private:
    void press(int key);
    void refreshLights();

    h89session *m_session;
    QToolButton *m_shift = nullptr;
    QLabel *m_lights[8] = {};
    QTimer m_timer;
};
