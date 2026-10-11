/*
 * The H19's screen.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#import <Cocoa/Cocoa.h>

#include "h89session.h"

@interface H89DisplayView : NSView
- (instancetype)initWithSession:(h89session *)session;
/* YES: the tube's 4:3 (setting view.aspect 0, the default); NO: square
 * pixels. */
- (void)setTvAspect:(BOOL)tv;
- (void)setSmooth:(BOOL)smooth;
/* The visual bell: the picture inverted for a moment. */
- (void)flash;
- (void)stop;
@end
