/*
 * SoftDisplayWidget -- see SoftDisplayWidget.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "SoftDisplayWidget.h"

#include "DisplayGeometry.h"

#include <QEvent>
#include <QPainter>
#include <QTimer>

SoftDisplayWidget::SoftDisplayWidget(h89session *session, QWidget *parent)
    : QWidget(parent), m_session(session), m_feed(new FrameFeed(session, this)),
      m_image(H89SESSION_FB_WIDTH, H89SESSION_FB_HEIGHT, QImage::Format_RGB32)
{
    m_image.fill(Qt::black);
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    connect(m_feed, &FrameFeed::frameReady, this, &SoftDisplayWidget::onFrame);
}

SoftDisplayWidget::~SoftDisplayWidget()
{
    m_feed->setActive(false);
}

void SoftDisplayWidget::setTvAspect(bool tv) { m_tvAspect = tv; update(); }
void SoftDisplayWidget::setSmooth(bool smooth) { m_smooth = smooth; update(); }

void SoftDisplayWidget::flash()
{
    if (m_flash)
        return;
    m_flash = true;
    update();
    QTimer::singleShot(120, this, [this] { m_flash = false; update(); });
}

void SoftDisplayWidget::updateActive()
{
    m_feed->setActive(isVisible() && window() && !window()->isMinimized());
}

void SoftDisplayWidget::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (window())
        window()->installEventFilter(this);
    updateActive();
}

void SoftDisplayWidget::hideEvent(QHideEvent *e)
{
    QWidget::hideEvent(e);
    if (window())
        window()->removeEventFilter(this);
    updateActive();
}

bool SoftDisplayWidget::eventFilter(QObject *watched, QEvent *e)
{
    if (e->type() == QEvent::WindowStateChange || e->type() == QEvent::Show ||
        e->type() == QEvent::Hide)
        updateActive();
    return QWidget::eventFilter(watched, e);
}

void SoftDisplayWidget::onFrame()
{
    /* The pixels are 0x00RRGGBB; Format_RGB32 needs 0xFFRRGGBB, or KWin
     * composites the zero alpha as see-through, so force alpha on. */
    quint32 *px = reinterpret_cast<quint32 *>(m_image.bits());
    if (!h89session_copy_frame(m_session, reinterpret_cast<uint32_t *>(px), &m_serial))
        return;
    for (qsizetype i = 0, n = (qsizetype)H89SESSION_FB_WIDTH * H89SESSION_FB_HEIGHT; i < n; i++)
        px[i] |= 0xFF000000u;
    update();
}

void SoftDisplayWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    p.setRenderHint(QPainter::SmoothPixmapTransform, m_smooth);
    const QRectF target = displayRect(QSizeF(size()), m_tvAspect);
    if (m_flash) {
        QImage inv = m_image;
        inv.invertPixels();
        p.drawImage(target, inv);
    } else {
        p.drawImage(target, m_image);
    }
}
