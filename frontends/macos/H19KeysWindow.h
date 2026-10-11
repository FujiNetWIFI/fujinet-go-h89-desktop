/*
 * The H19 Keys window (F9): every key of the H19's keyboard a Mac keyboard
 * does not have, a SHIFT latch, and lights for the terminal's modes.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#import <Cocoa/Cocoa.h>

#include "h89session.h"

@interface H89H19KeysWindow : NSWindowController <NSWindowDelegate>
- (instancetype)initWithSession:(h89session *)session;
- (void)toggle;
@end
