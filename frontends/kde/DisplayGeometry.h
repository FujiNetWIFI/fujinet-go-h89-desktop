/*
 * Where the picture goes inside the display, shared by the GPU and the
 * software display: the H19's 4:3 tube or the buffer's own 640:500, fitted
 * and centred.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <QRectF>
#include <QSizeF>

#include "h89session.h"

inline QRectF displayRect(QSizeF avail, bool tvAspect)
{
    const double want = tvAspect ? 4.0 / 3.0
                                 : (double)H89SESSION_FB_WIDTH / (double)H89SESSION_FB_HEIGHT;
    const double w = avail.width(), h = avail.height();
    double sw, sh;
    if (w / h > want) { sh = h; sw = sh * want; }
    else              { sw = w; sh = sw / want; }
    return QRectF((w - sw) / 2.0, (h - sh) / 2.0, sw, sh);
}
