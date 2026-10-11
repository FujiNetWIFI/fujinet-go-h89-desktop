/*
 * The application delegate: the window, the menu bar, and the session.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#import <Cocoa/Cocoa.h>

#include "h89session.h"

@interface H89AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate, NSMenuDelegate>
/* mediaPath: a file named on the command line (a disk, ROMs or anything for
 * the SD folder), or NULL. */
- (instancetype)initWithSession:(h89session *)session mediaPath:(const char *)mediaPath;
/* The H19 Keys window (F9), the debugger (F12) and fullscreen (F11): the
 * keys any of the app's windows passes on. */
- (void)toggleKeys:(id)sender;
- (void)toggleDebugger:(id)sender;
- (void)toggleFullScreen:(id)sender;
@end
