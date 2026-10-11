/*
 * H89H19KeysWindow -- see the header. Typing with it the key window types
 * on the H19, as the main window does, and its buttons never take the
 * keyboard from it.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#import "H19KeysWindow.h"

#import "AppDelegate.h"
#import "KeyForward.h"

typedef struct {
    int key;
    const char *label;
} KeyDef;

static const KeyDef kFunction[] = {
    { H89K_F1, "f1" }, { H89K_F2, "f2" }, { H89K_F3, "f3" }, { H89K_F4, "f4" }, { H89K_F5, "f5" },
    { H89K_ERASE, "ERASE" }, { H89K_BLUE, "BLUE" }, { H89K_RED, "RED" }, { H89K_GRAY, "GRAY" },
};
static const KeyDef kKeys[] = {
    { 0x1b, "ESC" }, { 0x09, "TAB" }, { 0x08, "BACK SP" }, { 0x7f, "DELETE" },
    { H89K_LINEFEED, "LINE FEED" }, { 0x0d, "RETURN" },
};
static const KeyDef kTerminal[] = {
    { H89K_BREAK, "BREAK" }, { H89K_OFFLINE, "OFF LINE" }, { H89K_SCROLL, "SCROLL" },
    { H89K_RESET, "SHIFT+RESET" },
};
static const KeyDef kPad[12] = {
    { H89K_KP7, "7 IC" }, { H89K_KP8, "8 \xe2\x86\x91" }, { H89K_KP9, "9 DC" },
    { H89K_KP4, "4 \xe2\x86\x90" }, { H89K_KP5, "5 HOME" }, { H89K_KP6, "6 \xe2\x86\x92" },
    { H89K_KP1, "1 IL" }, { H89K_KP2, "2 \xe2\x86\x93" }, { H89K_KP3, "3 DL" },
    { H89K_KP0, "0" }, { H89K_KP_DOT, "." }, { H89K_KP_ENTER, "ENTER" },
};
static const char *const kLightNames[8] = {
    "Off line", "Hold screen", "Keypad shifted", "Alternate keypad",
    "Keyboard off", "Graphics", "Insert", "Reverse",
};

#define NUM(a) (sizeof(a) / sizeof((a)[0]))

/* A button that does not take the keyboard: clicking it leaves the window's
 * content view the first responder, so typing goes on to the H19. */
@interface H89KeyButton : NSButton
@end
@implementation H89KeyButton
- (BOOL)acceptsFirstResponder { return NO; }
@end

/* The content view types on the H19. */
@interface H89KeysContentView : NSView
@property (nonatomic) h89session *session;
@end
@implementation H89KeysContentView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return YES; }
- (void)keyDown:(NSEvent *)e
{
    const uint32_t ks = H89KeysymFromEvent(e);
    H89AppDelegate *app = (H89AppDelegate *)[NSApp delegate];
    if (ks == H89_KEYSYM_F1 + 8) { [[self window] orderOut:nil]; return; }
    if (ks == H89_KEYSYM_F1 + 10) { [app toggleFullScreen:nil]; return; }
    if (ks == H89_KEYSYM_F1 + 11) { [app toggleDebugger:nil]; return; }
    if (!H89ForwardKeyDown(self.session, e)) [super keyDown:e];
}
@end

@implementation H89H19KeysWindow {
    h89session *_session;
    NSButton *_shift;
    NSTextField *_lights[8];
    NSTimer *_timer;
}

- (instancetype)initWithSession:(h89session *)session
{
    NSPanel *w = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 660, 380)
                                            styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                       NSWindowStyleMaskUtilityWindow)
                                              backing:NSBackingStoreBuffered
                                                defer:YES];
    self = [super initWithWindow:w];
    if (!self) return nil;
    _session = session;
    w.title = @"H19 Keys";
    w.releasedWhenClosed = NO;
    w.hidesOnDeactivate = NO;
    w.becomesKeyOnlyIfNeeded = NO;
    w.delegate = self;
    [self build];
    [w center];
    return self;
}

- (NSTextField *)label:(NSString *)text at:(NSPoint)p width:(CGFloat)width in:(NSView *)v
{
    NSTextField *l = [NSTextField labelWithString:text];
    l.frame = NSMakeRect(p.x, p.y, width, 18);
    [v addSubview:l];
    return l;
}

- (NSButton *)key:(const KeyDef *)k frame:(NSRect)r in:(NSView *)v
{
    H89KeyButton *b = [[H89KeyButton alloc] initWithFrame:r];
    b.title = [NSString stringWithUTF8String:k->label];
    b.bezelStyle = NSBezelStyleRounded;
    b.tag = k->key;
    b.target = self;
    b.action = @selector(pressed:);
    [v addSubview:b];
    return b;
}

- (void)build
{
    H89KeysContentView *v = [[H89KeysContentView alloc] initWithFrame:NSMakeRect(0, 0, 660, 380)];
    v.session = _session;
    CGFloat y = 12;

    [self label:@"Function keys" at:NSMakePoint(12, y) width:200 in:v];
    y += 20;
    for (size_t i = 0; i < NUM(kFunction); i++)
        [self key:&kFunction[i] frame:NSMakeRect(12 + (CGFloat)i * 70, y, 68, 30) in:v];
    y += 40;
    [self label:@"Keys" at:NSMakePoint(12, y) width:200 in:v];
    y += 20;
    for (size_t i = 0; i < NUM(kKeys); i++)
        [self key:&kKeys[i] frame:NSMakeRect(12 + (CGFloat)i * 102, y, 100, 30) in:v];
    y += 40;
    [self label:@"Terminal" at:NSMakePoint(12, y) width:200 in:v];
    y += 20;
    _shift = [[H89KeyButton alloc] initWithFrame:NSMakeRect(12, y, 84, 30)];
    _shift.title = @"SHIFT";
    _shift.bezelStyle = NSBezelStyleRounded;
    [_shift setButtonType:NSButtonTypePushOnPushOff];
    [v addSubview:_shift];
    for (size_t i = 0; i < NUM(kTerminal); i++)
        [self key:&kTerminal[i] frame:NSMakeRect(100 + (CGFloat)i * 116, y, 114, 30) in:v];
    y += 40;
    [self label:@"Keypad" at:NSMakePoint(12, y) width:200 in:v];
    [self label:@"Modes" at:NSMakePoint(310, y) width:200 in:v];
    y += 20;
    for (int i = 0; i < 12; i++)
        [self key:&kPad[i] frame:NSMakeRect(12 + (i % 3) * 92, y + (i / 3) * 36, 90, 34) in:v];
    for (int i = 0; i < 8; i++)
        _lights[i] = [self label:@"" at:NSMakePoint(310, y + i * 18) width:240 in:v];
    y += 4 * 36 + 12;

    NSWindow *w = [self window];
    [w setContentSize:NSMakeSize(660, y)];
    v.frame = NSMakeRect(0, 0, 660, y);
    w.contentView = v;
    [w makeFirstResponder:v];
    [self refreshLights];
}

- (void)pressed:(NSButton *)sender
{
    const int key = (int)sender.tag;
    const BOOL shift = _shift.state == NSControlStateValueOn;
    h89session_key(_session, key, shift ? 1 : 0);
    if (shift && key != H89K_RESET) _shift.state = NSControlStateValueOff;
    [[self window] makeFirstResponder:[[self window] contentView]];
}

- (void)refreshLights
{
    const unsigned m = h89session_h19_modes(_session);
    for (int i = 0; i < 8; i++)
        _lights[i].stringValue = [NSString stringWithFormat:@"%@  %s",
                                  ((m >> i) & 1) ? @"●" : @"○", kLightNames[i]];
}

- (void)toggle
{
    NSWindow *w = [self window];
    if ([w isVisible]) {
        [w orderOut:nil];
        return;
    }
    [self showWindow:nil];
    [w makeKeyAndOrderFront:nil];
    [w makeFirstResponder:[w contentView]];
    [self refreshLights];
    if (!_timer)
        _timer = [NSTimer scheduledTimerWithTimeInterval:0.25 repeats:YES
                                                   block:^(NSTimer *t) { (void)t; [self refreshLights]; }];
}

- (void)windowWillClose:(NSNotification *)note
{
    (void)note;
    [_timer invalidate];
    _timer = nil;
}
@end
