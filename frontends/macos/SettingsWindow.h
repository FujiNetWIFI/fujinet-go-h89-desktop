/*
 * Settings (Cmd+,), AppKit: the machine's configuration and the app's
 * preferences in six tabs.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#import <Cocoa/Cocoa.h>

#include "h89session.h"

/* Posted (main thread) when a view setting changes live: phosphor,
 * scanlines, cursor blink, aspect, smoothing. */
extern NSString *const H89ViewSettingsChangedNotification;

typedef NS_ENUM(NSInteger, H89SettingsOutcome) {
    H89SettingsNothing = 0,     /* cancelled, or nothing the machine needs */
    H89SettingsPowerCycle,      /* a new configuration: power cycle onto it */
    H89SettingsRestart,         /* FujiNet or audio switched: restart the session */
};

@interface H89SettingsWindow : NSWindowController <NSWindowDelegate, NSTabViewDelegate>
/* done runs on the main thread when the window closes. */
- (instancetype)initWithSession:(h89session *)session
                           done:(void (^)(H89SettingsOutcome outcome))done;
/* page: "machine", "slots", "switches", "cpnet", "display", "fujinet", or
 * nil for the first. */
- (void)showOnPage:(NSString *)page;
@end
