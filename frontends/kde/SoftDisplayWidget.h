/*
 * The software display: the fallback for a machine with no usable GPU
 * device. A QWidget that scales with QPainter, fed by FrameFeed like the GPU
 * one.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <QImage>
#include <QWidget>
#include <cstdint>

#include "DisplayView.h"
#include "FrameFeed.h"
#include "h89session.h"

class SoftDisplayWidget : public QWidget, public DisplayView {
    Q_OBJECT
public:
    explicit SoftDisplayWidget(h89session *session, QWidget *parent = nullptr);
    ~SoftDisplayWidget() override;

    QWidget *widget() override { return this; }
    void setTvAspect(bool tv) override;
    void setSmooth(bool smooth) override;
    void flash() override;

protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    bool eventFilter(QObject *watched, QEvent *e) override;
    /* Tab is the H19's: it reaches the window's key handler instead of
     * moving the focus. */
    bool focusNextPrevChild(bool) override { return false; }

private:
    void onFrame();
    void updateActive();

    h89session *m_session;
    FrameFeed *m_feed;
    QImage m_image;            /* Format_RGB32, FB_WIDTH x FB_HEIGHT */
    uint64_t m_serial = 0;
    bool m_tvAspect = true;
    bool m_smooth = true;
    bool m_flash = false;
};
