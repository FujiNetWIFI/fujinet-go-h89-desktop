/*
 * DisplayExtras.inc -- the visual bell, included into the display view's
 * @implementation (see DisplayGlue.h).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* How long the visual bell inverts the picture */
#define FLASH_SECONDS 0.12

- (void)extraInit {}

/* The visual bell: the picture inverted for a moment. */
- (void)flash
{
    [self setInverted:YES];
    const NSUInteger serial = ++_flashSerial;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(FLASH_SECONDS * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        if (serial != self->_flashSerial) return;
        [self setInverted:NO];
    });
}
