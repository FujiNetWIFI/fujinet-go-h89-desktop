/*
 * DisplayGlue.h -- what DisplayView.m needs to know about THIS machine: the
 * session's functions, the frame size and the shape of the picture.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "h89session.h"

#define FN_VIEW      H89DisplayView
#define FN_SESSION   h89session
#define FN_FB_W      H89SESSION_FB_WIDTH
#define FN_FB_H      H89SESSION_FB_HEIGHT
#define FN_INITIAL_W 960
#define FN_INITIAL_H 720

/* The visual bell (DisplayExtras.inc). */
#define FN_EXTRA_IVARS \
    NSUInteger _flashSerial;
#define FN_EXTRA_METHODS "DisplayExtras.inc"

/* The H19's 640x500 on the tube's 4:3 */
#define TV_RATIO (4.0 / 3.0)

/* The machine paces itself (it has no vsync lock): nothing here is used for
 * pacing, the tick count only has to be sane. */
static inline int fn_machine_hz(FN_SESSION *s) { (void)s; return 60; }

static inline void fn_notify_vsync(FN_SESSION *s, int64_t ns)
{
    h89session_notify_vsync(s, ns);
}

/* Every frame is the whole 640 x 500; nothing to measure. */
static inline int fn_pull_frame(FN_SESSION *s, uint32_t *dst, int *lines, uint64_t *serial)
{
    *lines = FN_FB_H;
    return h89session_copy_frame(s, dst, serial);
}

static inline double fn_picture_aspect(FN_SESSION *s, int lines, BOOL tv)
{
    (void)s;
    return tv ? TV_RATIO : (double)FN_FB_W / (double)lines;
}
