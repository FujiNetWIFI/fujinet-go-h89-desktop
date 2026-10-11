/*
 * H89DisplayView -- the framebuffer, on a CVDisplayLink.
 *
 * The H89 paces itself (core/machine/host.cpp) and publishes a frame when
 * the screen changes; the display link is only when to look for one. Its
 * callback runs on its OWN high-priority thread, not the main one, so it
 * does the cheap thing (pull the frame) and asks AppKit to redraw on the
 * main thread. Drawing from the callback thread would be a use of AppKit
 * off the main thread, which is undefined.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#import "DisplayView.h"

#import <CoreVideo/CoreVideo.h>

#include <stdlib.h>
#include <string.h>

/* The H19's 640x500 on the tube's 4:3 */
#define TV_RATIO (4.0 / 3.0)

/* How long the visual bell inverts the picture */
#define FLASH_SECONDS 0.12

@implementation H89DisplayView {
    h89session *_session;
    CVDisplayLinkRef _link;
    uint32_t *_fb;              /* written by the link's thread */
    uint32_t *_shown;           /* what drawRect: draws (main thread) */
    uint64_t _serial;
    BOOL _fresh;                /* _fb holds a frame drawRect: has not taken (under _lock) */
    NSLock *_lock;
    CGColorSpaceRef _cs;
    BOOL _tv;
    BOOL _smooth;
    BOOL _flash;
    NSUInteger _flashSerial;
}

static CVReturn displayCallback(CVDisplayLinkRef link, const CVTimeStamp *now,
                                const CVTimeStamp *out, CVOptionFlags flagsIn,
                                CVOptionFlags *flagsOut, void *ctx)
{
    (void)link; (void)now; (void)out; (void)flagsIn; (void)flagsOut;
    H89DisplayView *self = (__bridge H89DisplayView *)ctx;
    [self tick];
    return kCVReturnSuccess;
}

- (instancetype)initWithSession:(h89session *)session
{
    self = [super initWithFrame:NSMakeRect(0, 0, 960, 720)];
    if (!self) return nil;
    _session = session;
    _tv = YES;
    _smooth = YES;
    _lock = [[NSLock alloc] init];

    const size_t n = (size_t)H89SESSION_FB_WIDTH * H89SESSION_FB_HEIGHT;
    _fb = calloc(n, sizeof *_fb);
    _shown = calloc(n, sizeof *_shown);
    _cs = CGColorSpaceCreateDeviceRGB();

    CVDisplayLinkCreateWithActiveCGDisplays(&_link);
    CVDisplayLinkSetOutputCallback(_link, displayCallback, (__bridge void *)self);
    CVDisplayLinkStart(_link);
    return self;
}

- (void)stop
{
    if (_link) {
        CVDisplayLinkStop(_link);
        CVDisplayLinkRelease(_link);
        _link = NULL;
    }
}

- (void)dealloc
{
    [self stop];
    if (_cs) CGColorSpaceRelease(_cs);
    free(_fb);
    free(_shown);
}

- (void)tick
{
    [_lock lock];
    const int got = h89session_copy_frame(_session, _fb, &_serial);
    if (got) _fresh = YES;
    [_lock unlock];
    if (!got) return;
    /* AppKit is main-thread only; the display link's callback is not. */
    dispatch_async(dispatch_get_main_queue(), ^{ [self setNeedsDisplay:YES]; });
}

- (void)setTvAspect:(BOOL)tv { _tv = tv; [self setNeedsDisplay:YES]; }
- (void)setSmooth:(BOOL)smooth { _smooth = smooth; [self setNeedsDisplay:YES]; }

- (void)flash
{
    _flash = YES;
    [self setNeedsDisplay:YES];
    const NSUInteger serial = ++_flashSerial;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(FLASH_SECONDS * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        if (serial != self->_flashSerial) return;
        self->_flash = NO;
        [self setNeedsDisplay:YES];
    });
}

- (BOOL)isOpaque { return YES; }

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    CGContextRef dc = [[NSGraphicsContext currentContext] CGContext];
    const NSRect b = [self bounds];

    CGContextSetRGBFillColor(dc, 0, 0, 0, 1);
    CGContextFillRect(dc, b);

    [_lock lock];
    if (_fresh) {
        memcpy(_shown, _fb, (size_t)H89SESSION_FB_WIDTH * H89SESSION_FB_HEIGHT * sizeof *_fb);
        _fresh = NO;
    }
    [_lock unlock];
    /* The session's pixels are 0x00RRGGBB in host order: a little-endian
     * 32-bit image with the alpha byte skipped reads them as they are. */
    CGDataProviderRef dp = CGDataProviderCreateWithData(NULL, _shown,
        (size_t)H89SESSION_FB_WIDTH * H89SESSION_FB_HEIGHT * 4, NULL);
    if (!dp) return;
    CGImageRef img = CGImageCreate(H89SESSION_FB_WIDTH, H89SESSION_FB_HEIGHT, 8, 32,
                                   H89SESSION_FB_WIDTH * 4, _cs,
                                   (CGBitmapInfo)kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little,
                                   dp, NULL, false, kCGRenderingIntentDefault);
    CGDataProviderRelease(dp);
    if (!img) return;

    const double want = _tv ? TV_RATIO : (double)H89SESSION_FB_WIDTH / (double)H89SESSION_FB_HEIGHT;
    const double w = b.size.width, h = b.size.height;
    double sw, sh;
    if (w / h > want) { sh = h; sw = sh * want; }
    else              { sw = w; sh = sw / want; }
    const CGRect r = CGRectMake((w - sw) / 2, (h - sh) / 2, sw, sh);

    CGContextSetInterpolationQuality(dc, _smooth ? kCGInterpolationHigh : kCGInterpolationNone);
    CGContextDrawImage(dc, r, img);
    CGImageRelease(img);

    if (_flash) {
        CGContextSetBlendMode(dc, kCGBlendModeDifference);
        CGContextSetRGBFillColor(dc, 1, 1, 1, 1);
        CGContextFillRect(dc, r);
        CGContextSetBlendMode(dc, kCGBlendModeNormal);
    }
}
@end
