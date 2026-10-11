/*
 * The debugger window (F12), AppKit.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#import <Cocoa/Cocoa.h>

#include "h89session.h"

@interface H89DebuggerWindow : NSWindowController <NSWindowDelegate>
- (instancetype)initWithSession:(h89session *)session;
/* Shows it; attaching the engine stops the machine. */
- (void)showDebugger;
/* Shows it, or hides it (and detaches, so the machine runs on). */
- (void)toggleDebugger;
@end
