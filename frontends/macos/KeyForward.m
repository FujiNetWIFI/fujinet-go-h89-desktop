/*
 * KeyForward -- see the header.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#import "KeyForward.h"

#import <Carbon/Carbon.h>   /* kVK_* virtual key codes */

uint32_t H89KeysymFromEvent(NSEvent *event)
{
    const uint32_t ks = h89session_keysym_from_macos_keycode([event keyCode]);
    if (ks) return ks;
    if ([event keyCode] == kVK_Space) return ' ';

    /* Anything else: the character the key makes with Shift (and without
     * Control or Option), which is a Latin-1 keysym as it stands -- as a
     * GDK keyval is for the same key. */
    NSString *chars = [event charactersIgnoringModifiers];
    if ([chars length] == 1) {
        const unichar c = [chars characterAtIndex:0];
        if (c >= 0x20 && c <= 0x7e) return (uint32_t)c;
    }
    return 0;
}

BOOL H89ForwardKeyDown(h89session *session, NSEvent *event)
{
    const NSEventModifierFlags f = [event modifierFlags];
    /* Command shortcuts are the menus'. */
    if (f & NSEventModifierFlagCommand) return NO;
    const uint32_t ks = H89KeysymFromEvent(event);
    if (!ks) return NO;
    uint32_t unicode = 0;
    NSString *chars = [event charactersIgnoringModifiers];
    if ([chars length] == 1) {
        const unichar c = [chars characterAtIndex:0];
        if (c < 0xf700) unicode = c;     /* not one of AppKit's function-key codes */
    }
    const int ctrl = (f & NSEventModifierFlagControl) != 0;
    const int shift = (f & NSEventModifierFlagShift) != 0;
    const int key = h89session_key_from_event(ks, unicode, ctrl, shift);
    if (key < 0) return NO;
    h89session_key(session, key, shift);
    return YES;
}

NSColor *H89AccentColor(void)
{
    return [NSColor colorWithSRGBRed:((H89SESSION_ACCENT_RGB >> 16) & 0xff) / 255.0
                               green:((H89SESSION_ACCENT_RGB >> 8) & 0xff) / 255.0
                                blue:(H89SESSION_ACCENT_RGB & 0xff) / 255.0
                               alpha:1.0];
}
