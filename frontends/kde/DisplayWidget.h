/*
 * The H19's screen: a QWidget that pulls frames from the session.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <QImage>
#include <QTimer>
#include <QWidget>
#include <cstdint>
#include <vector>

#include "h89session.h"

class DisplayWidget : public QWidget {
    Q_OBJECT
public:
    explicit DisplayWidget(h89session *session, QWidget *parent = nullptr);
    /* "view.aspect": 0 = the tube's 4:3, 1 = square pixels. */
    void setTvAspect(bool tv);
    void setSmooth(bool smooth);
    /* The visual bell: the picture inverted for a moment. */
    void flash();

protected:
    void paintEvent(QPaintEvent *) override;
    /* Tab is the H19's: it reaches the window's key handler instead of
     * moving the focus. */
    bool focusNextPrevChild(bool) override { return false; }

private:
    void tick();

    h89session *m_session;
    QImage m_image;
    uint64_t m_serial = 0;
    QTimer m_timer;
    bool m_tvAspect = true;
    bool m_smooth = true;
    bool m_flash = false;
};
