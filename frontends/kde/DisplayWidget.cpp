/*
 * DisplayWidget -- see DisplayWidget.h.
 *
 * Frames are pulled by serial: copy_frame does nothing when the H19 has not
 * drawn anything new. The H89 paces itself; the timer only decides how
 * soon a change is seen.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "DisplayWidget.h"

#include <QPainter>

DisplayWidget::DisplayWidget(h89session *session, QWidget *parent)
    : QWidget(parent), m_session(session),
      m_image(H89SESSION_FB_WIDTH, H89SESSION_FB_HEIGHT, QImage::Format_RGB32)
{
    m_image.fill(Qt::black);
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    connect(&m_timer, &QTimer::timeout, this, &DisplayWidget::tick);
    m_timer.start(8);
}

void DisplayWidget::tick()
{
    /* 0x00RRGGBB is QImage::Format_RGB32 as it is */
    if (h89session_copy_frame(m_session, reinterpret_cast<uint32_t *>(m_image.bits()), &m_serial))
        update();
}

void DisplayWidget::setTvAspect(bool tv) { m_tvAspect = tv; update(); }
void DisplayWidget::setSmooth(bool smooth) { m_smooth = smooth; update(); }

void DisplayWidget::flash()
{
    if (m_flash)
        return;
    m_flash = true;
    update();
    QTimer::singleShot(120, this, [this] { m_flash = false; update(); });
}

void DisplayWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    const double want = m_tvAspect ? 4.0 / 3.0
                                   : (double)H89SESSION_FB_WIDTH / (double)H89SESSION_FB_HEIGHT;
    double w = width(), h = height(), sw, sh;
    if (w / h > want) { sh = h; sw = sh * want; }
    else              { sw = w; sh = sw / want; }

    p.setRenderHint(QPainter::SmoothPixmapTransform, m_smooth);
    const QRectF target((w - sw) / 2.0, (h - sh) / 2.0, sw, sh);
    if (m_flash) {
        QImage inv = m_image;
        inv.invertPixels();
        p.drawImage(target, inv);
    } else {
        p.drawImage(target, m_image);
    }
}
