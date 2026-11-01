/*
 * DisplayWidget -- see DisplayWidget.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "DisplayWidget.h"

#include "DisplayGeometry.h"

#include <QEvent>
#include <QFile>
#include <QMatrix4x4>
#include <QTimer>
#include <QVector4D>
#include <QWindow>

/* A wrong resource path gives an empty shader, a pipeline that fails to
 * build and a silent drop to the software display: say so out loud. */
static QShader loadShader(const char *path)
{
    QFile f(QString::fromLatin1(path));
    QShader s;
    if (f.open(QIODevice::ReadOnly))
        s = QShader::fromSerialized(f.readAll());
    if (!s.isValid())
        qWarning("DisplayWidget: shader %s is missing or empty; the display will fall back to software",
                 path);
    return s;
}

DisplayWidget::DisplayWidget(h89session *session, QWidget *parent)
    : QRhiWidget(parent), m_session(session), m_feed(new FrameFeed(session, this)),
      m_scratch(H89SESSION_FB_WIDTH, H89SESSION_FB_HEIGHT, QImage::Format_RGB32)
{
    m_scratch.fill(0);
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
    connect(m_feed, &FrameFeed::frameReady, this, &DisplayWidget::onFrame);
}

DisplayWidget::~DisplayWidget()
{
    m_feed->setActive(false);
}

void DisplayWidget::setTvAspect(bool tv) { m_tvAspect = tv; update(); }
void DisplayWidget::setSmooth(bool smooth) { m_smooth = smooth; update(); }

/* The bell: the picture inverted for 120 ms. The timer exists only for
 * that long; nothing runs the rest of the time. */
void DisplayWidget::flash()
{
    if (m_flash)
        return;
    m_flash = true;
    update();
    QTimer::singleShot(120, this, [this] { m_flash = false; update(); });
}

/* ---- idle ------------------------------------------------------------------
 * A hidden or minimised window has nobody to draw for: the feed unregisters
 * from the session, so the GUI thread gets no wakeups at all until it is
 * shown again. The machine itself keeps running. */

void DisplayWidget::updateActive()
{
    QWidget *w = window();
    m_feed->setActive(isVisible() && w && !w->isMinimized());
}

void DisplayWidget::showEvent(QShowEvent *e)
{
    QRhiWidget::showEvent(e);
    if (window())
        window()->installEventFilter(this);
    updateActive();
}

void DisplayWidget::hideEvent(QHideEvent *e)
{
    QRhiWidget::hideEvent(e);
    if (window())
        window()->removeEventFilter(this);
    updateActive();
}

bool DisplayWidget::eventFilter(QObject *watched, QEvent *e)
{
    if (e->type() == QEvent::WindowStateChange || e->type() == QEvent::Show ||
        e->type() == QEvent::Hide)
        updateActive();
    return QRhiWidget::eventFilter(watched, e);
}

bool DisplayWidget::event(QEvent *e)
{
    /* The compositor can mark a window occluded without minimising it */
    if (e->type() == QEvent::ScreenChangeInternal || e->type() == QEvent::WindowActivate)
        updateActive();
    return QRhiWidget::event(e);
}

/* ---- frames ---------------------------------------------------------------- */

void DisplayWidget::onFrame()
{
    /* 0x00RRGGBB straight into the image the upload reads from */
    if (!h89session_copy_frame(m_session, reinterpret_cast<uint32_t *>(m_scratch.bits()), &m_serial))
        return;
    m_dirty = true;
    update();
}

/* ---- GPU ------------------------------------------------------------------- */

void DisplayWidget::releaseResources()
{
    m_pipeline.reset();
    m_srb.reset();
    m_linear.reset();
    m_nearest.reset();
    m_tex.reset();
    m_ubuf.reset();
}

void DisplayWidget::initialize(QRhiCommandBuffer *)
{
    if (m_rhi != rhi()) {
        releaseResources();
        m_rhi = rhi();
    }
    if (m_pipeline)
        return;

    m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_ubuf->create();
    m_nearest.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
                                      QRhiSampler::None, QRhiSampler::ClampToEdge,
                                      QRhiSampler::ClampToEdge));
    m_nearest->create();
    m_linear.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                     QRhiSampler::None, QRhiSampler::ClampToEdge,
                                     QRhiSampler::ClampToEdge));
    m_linear->create();
    if (!m_tex) {
        m_tex.reset(m_rhi->newTexture(QRhiTexture::BGRA8,
                                      QSize(H89SESSION_FB_WIDTH, H89SESSION_FB_HEIGHT)));
        m_tex->create();
        m_dirty = true;   /* a fresh texture is empty; a still picture would stay black */
    }

    m_smoothApplied = m_smooth;
    m_srb.reset(m_rhi->newShaderResourceBindings());
    m_srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_ubuf.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_tex.get(), m_smooth ? m_linear.get() : m_nearest.get()),
    });
    m_srb->create();

    m_pipeline.reset(m_rhi->newGraphicsPipeline());
    m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_pipeline->setShaderStages({
        { QRhiShaderStage::Vertex, loadShader(":/shaders/display.vert.qsb") },
        { QRhiShaderStage::Fragment, loadShader(":/shaders/display.frag.qsb") },
    });
    m_pipeline->setVertexInputLayout({});
    m_pipeline->setShaderResourceBindings(m_srb.get());
    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    if (!m_pipeline->create()) {
        m_pipeline.reset();
        emit renderFailed();
    }
}

void DisplayWidget::render(QRhiCommandBuffer *cb)
{
    if (!m_pipeline)
        return;
    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();

    if (m_smooth != m_smoothApplied || !m_srb) {
        m_smoothApplied = m_smooth;
        m_srb.reset(m_rhi->newShaderResourceBindings());
        m_srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                m_ubuf.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                      m_tex.get(), m_smooth ? m_linear.get() : m_nearest.get()),
        });
        m_srb->create();
    }

    if (m_dirty) {
        QRhiTextureSubresourceUploadDescription sub;
        const qsizetype bytes = (qsizetype)H89SESSION_FB_WIDTH * H89SESSION_FB_HEIGHT * 4;
        sub.setData(QByteArray::fromRawData(reinterpret_cast<const char *>(m_scratch.constBits()), bytes));
        sub.setDataStride(H89SESSION_FB_WIDTH * 4);
        sub.setSourceSize(QSize(H89SESSION_FB_WIDTH, H89SESSION_FB_HEIGHT));
        u->uploadTexture(m_tex.get(), QRhiTextureUploadDescription({ QRhiTextureUploadEntry(0, 0, sub) }));
        m_dirty = false;
    }

    const QSize px = renderTarget()->pixelSize();
    const QRectF r = displayRect(QSizeF(px), m_tvAspect);

    /* pixel rect -> clip space (y up), then through the backend's correction
     * so Vulkan's flipped y comes out the right way up */
    auto clip = [&](double x, double y) {
        return m_rhi->clipSpaceCorrMatrix() *
               QVector4D(float(x / px.width() * 2.0 - 1.0), float(1.0 - y / px.height() * 2.0), 0.f, 1.f);
    };
    const QVector4D tl = clip(r.left(), r.top());
    const QVector4D br = clip(r.right(), r.bottom());
    const float params[8] = { tl.x(), tl.y(), br.x(), br.y(), m_flash ? 1.f : 0.f, 0.f, 0.f, 0.f };
    u->updateDynamicBuffer(m_ubuf.get(), 0, sizeof params, params);

    cb->beginPass(renderTarget(), Qt::black, { 1.0f, 0 }, u);
    cb->setGraphicsPipeline(m_pipeline.get());
    cb->setViewport(QRhiViewport(0, 0, px.width(), px.height()));
    cb->setShaderResources(m_srb.get());
    cb->draw(4);
    cb->endPass();
}
