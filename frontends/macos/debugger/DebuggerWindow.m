/*
 * DebuggerWindow.m -- the debugger (AppKit) over the session's engine,
 * core/include/h89debug.h: the GNOME, KDE and Win32 windows' toolbar and
 * tabs.
 *
 * Tabs: the Prompt (the engine's commands, Tab completion, a history), the
 * CPU and memory (every register editable in any radix, the flags, a dump),
 * the disassembly (double-click a line for a breakpoint, the wheel browses),
 * breakpoints, the H89's parts as the engine reports them, and FujiNet (the
 * FujiBus packets on the card, both ways, beside the card's state). F5
 * run/stop, F7 step, F8 over, Shift+F8 out, F12 close. Showing it attaches
 * the engine (which stops the machine); closing it detaches, and the
 * machine runs on. The FujiNet trace runs from the session's start, so its
 * tab shows what happened before the window opened.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#import "DebuggerWindow.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "h89debug.h"
#include "h89session.h"

#define DISASM_WINDOW 48
#define MEM_ROWS 32
#define NREGS 17
#define NFLAGS 6
#define MAX_BPS 128
#define NTABS 6
#define TAB_PROMPT 0
#define TAB_FUJINET 5
#define TRACE_MAX 512
#define NSAVES 3

static const struct { const char *name; int reg; int wide; } kRegs[NREGS] = {
    { "PC", H89_REG_PC, 1 }, { "SP", H89_REG_SP, 1 }, { "AF", H89_REG_AF, 1 },
    { "BC", H89_REG_BC, 1 }, { "DE", H89_REG_DE, 1 }, { "HL", H89_REG_HL, 1 },
    { "IX", H89_REG_IX, 1 }, { "IY", H89_REG_IY, 1 },
    { "AF'", H89_REG_AF2, 1 }, { "BC'", H89_REG_BC2, 1 }, { "DE'", H89_REG_DE2, 1 },
    { "HL'", H89_REG_HL2, 1 }, { "I", H89_REG_I, 0 }, { "R", H89_REG_R, 0 },
    { "IM", H89_REG_IM, -1 }, { "IFF1", H89_REG_IFF1, -1 }, { "IFF2", H89_REG_IFF2, -1 },
};

static const int kFlagIds[NFLAGS] = { H89_FLAG_S, H89_FLAG_Z, H89_FLAG_H, H89_FLAG_PV, H89_FLAG_N, H89_FLAG_C };
static NSString *const kFlagNames[NFLAGS] = { @"S", @"Z", @"H", @"P/V", @"N", @"C" };

static const int kBpTypes[6] = {
    H89DEBUG_BP_EXEC, H89DEBUG_BP_READ, H89DEBUG_BP_WRITE, H89DEBUG_BP_READ | H89DEBUG_BP_WRITE,
    H89DEBUG_BP_IN, H89DEBUG_BP_OUT
};

static const char *const kSaveKinds[NSAVES] = { "dis", "ram", "regs" };
static NSString *const kSaveTitles[NSAVES] = {
    @"Disassembly (the 64K as mapped)…", @"Memory (the 64K as the CPU sees it)…",
    @"Registers and the H89's State…"
};
static NSString *const kSaveFiles[NSAVES] = { @"disassembly.asm", @"memory.bin", @"h89-state.txt" };

/* The tabs, in order, by the names H89_DEBUGGER_TAB takes. */
static const char *const kTabNames[NTABS] = { "prompt", "cpu", "disassembly", "breakpoints", "h89", "fujinet" };

/* ---- helpers ------------------------------------------------------------------------ */

/* The engine's text, its control characters dropped (it is ASCII, but a
 * label from a symbol file need not be valid UTF-8). */
static NSString *textFrom(const char *s)
{
    size_t n = s ? strlen(s) : 0, o = 0;
    char *buf = malloc(n + 1);
    NSString *r;
    if (!buf)
        return @"";
    for (size_t i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 || c == '\n' || c == '\t')
            buf[o++] = (char)c;
    }
    buf[o] = '\0';
    r = [NSString stringWithUTF8String:buf];
    if (!r)
        r = [NSString stringWithCString:buf encoding:NSISOLatin1StringEncoding];
    free(buf);
    return r ?: @"";
}

static NSString *trimmed(NSString *s)
{
    return [s stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
}

static NSColor *accentColor(void)
{
    return [NSColor colorWithSRGBRed:((H89SESSION_ACCENT_RGB >> 16) & 0xff) / 255.0
                               green:((H89SESSION_ACCENT_RGB >> 8) & 0xff) / 255.0
                                blue:(H89SESSION_ACCENT_RGB & 0xff) / 255.0
                               alpha:1.0];
}

static NSFont *monoFont(void)
{
    return [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
}

/* The open panel's filter: a type per extension (a dynamic one for an
 * extension nothing has registered, which still matches by extension). */
static NSArray<UTType *> *typesForExtensions(NSArray<NSString *> *exts)
{
    NSMutableArray<UTType *> *types = [NSMutableArray array];
    for (NSString *ext in exts) {
        UTType *t = [UTType typeWithFilenameExtension:ext];
        if (t)
            [types addObject:t];
    }
    return types;
}

/* A read-only monospaced text view in a scroll view. */
static NSTextView *monoView(NSScrollView **scrollOut, BOOL wrap)
{
    NSScrollView *scroll = [[NSScrollView alloc] init];
    NSTextView *view = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = !wrap;
    view.editable = NO;
    view.richText = NO;
    view.font = monoFont();
    view.textColor = NSColor.textColor;
    view.backgroundColor = NSColor.textBackgroundColor;
    if (wrap) {
        view.autoresizingMask = NSViewWidthSizable;
    } else {
        view.horizontallyResizable = YES;
        view.textContainer.widthTracksTextView = NO;
        view.textContainer.containerSize = NSMakeSize(FLT_MAX, FLT_MAX);
        view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    }
    scroll.documentView = view;
    [scroll setContentHuggingPriority:NSLayoutPriorityDefaultLow
                       forOrientation:NSLayoutConstraintOrientationVertical];
    *scrollOut = scroll;
    return view;
}

/* Replace a text view's text, keeping the reader's place. */
static void setTextKeepingPlace(NSTextView *view, NSString *text)
{
    NSScrollView *scroll = view.enclosingScrollView;
    const NSPoint at = scroll ? scroll.contentView.bounds.origin : NSZeroPoint;
    view.string = text;
    if (scroll)
        [scroll.contentView scrollToPoint:at];
}

/* Whether a view's reader is at its end (a log then follows new lines). */
static BOOL atEnd(NSTextView *view)
{
    NSScrollView *scroll = view.enclosingScrollView;
    if (!scroll)
        return YES;
    return NSMaxY(scroll.contentView.documentVisibleRect) >= NSHeight(view.frame) - 4;
}

/* The same where the reader left it, or, following, at its end. */
static void setTextAt(NSTextView *view, NSString *text, BOOL follow)
{
    if (![view.string isEqualToString:text])
        setTextKeepingPlace(view, text);
    if (follow)
        [view scrollRangeToVisible:NSMakeRange(view.string.length, 0)];
}

static NSStackView *hstack(NSArray<NSView *> *views)
{
    NSStackView *s = [NSStackView stackViewWithViews:views];
    s.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    s.spacing = 6;
    [s setHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];
    [s setContentHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];
    return s;
}

/* A column whose low-hugging member (a scroll view) takes the height left. */
static NSStackView *vstack(NSArray<NSView *> *views)
{
    NSStackView *s = [NSStackView stackViewWithViews:views];
    s.orientation = NSUserInterfaceLayoutOrientationVertical;
    s.alignment = NSLayoutAttributeLeading;
    s.distribution = NSStackViewDistributionFill;
    s.spacing = 6;
    return s;
}

/* A member of a column as wide as the column. */
static void fillWidth(NSStackView *stack, NSView *view, CGFloat inset)
{
    [view.widthAnchor constraintEqualToAnchor:stack.widthAnchor constant:-inset].active = YES;
}

/* ---- a text view that reports double-clicked lines and wheel rows ------------------------ */

@interface H89RowTextView : NSTextView
@property (nonatomic, copy) void (^onDoubleClickLine)(int line);
@property (nonatomic, copy) void (^onScrollRows)(int rows);
@end

@implementation H89RowTextView {
    CGFloat _accum;
}

- (void)mouseDown:(NSEvent *)event
{
    if (event.clickCount == 2 && self.onDoubleClickLine) {
        const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
        NSString *text = self.string;
        NSUInteger idx = [self characterIndexForInsertionAtPoint:p];
        int line = 0;
        if (idx > text.length)
            idx = text.length;
        for (NSUInteger i = 0; i < idx; i++)
            if ([text characterAtIndex:i] == '\n')
                line++;
        self.onDoubleClickLine(line);
        return;
    }
    if (!self.onDoubleClickLine)
        [super mouseDown:event];
}

- (void)scrollWheel:(NSEvent *)event
{
    if (!self.onScrollRows) {
        [super scrollWheel:event];
        return;
    }
    /* a trackpad reports points, a wheel lines; up is positive */
    _accum += event.hasPreciseScrollingDeltas ? -event.scrollingDeltaY / 16.0 : -event.scrollingDeltaY * 3.0;
    const int rows = (int)_accum;
    _accum -= rows;
    if (rows != 0)
        self.onScrollRows(rows);
}
@end

/* ---- the window ------------------------------------------------------------------------ */

@interface H89DebuggerWindow () <NSTextFieldDelegate, NSTabViewDelegate>
@end

@implementation H89DebuggerWindow {
    h89session *_session;
    h89debug *_dbg;
    NSTimer *_poll;
    id _keyMonitor;
    unsigned _seenGen;
    BOOL _wasStopped;
    int _runningTicks;
    BOOL _placed;

    NSTabView *_tabs;
    NSButton *_runBtn;
    NSPopUpButton *_radix;
    NSTextField *_status;

    NSTextView *_promptOut;
    NSTextField *_promptIn;
    NSMutableArray<NSString *> *_history;
    NSInteger _historyPos;

    NSTextField *_reg[NREGS];
    NSButton *_flag[NFLAGS];
    NSTextField *_cpuInfo;
    NSTextField *_memGoto, *_memAddr, *_memVal;
    H89RowTextView *_mem;
    int _memBase;

    NSButton *_followPc;
    NSTextField *_jump;
    H89RowTextView *_disasm;
    int _disasmTop;
    uint16_t _lineAddr[DISASM_WINDOW];
    int _lineCount;

    NSPopUpButton *_bpType;
    NSTextField *_bpStart, *_bpEnd, *_bpCond, *_bpMsg;
    NSStackView *_bpList;

    NSTextView *_h89;

    NSTextView *_fujiText, *_fujiState;
    NSTextField *_fujiNote;
    uint64_t _fujiLast;
    int _fujiCount;
}

- (instancetype)initWithSession:(h89session *)session
{
    NSWindow *win = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 1000, 760)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                      defer:YES];
    self = [super initWithWindow:win];
    if (!self)
        return nil;
    _session = session;
    _dbg = h89session_debugger(session);
    _history = [NSMutableArray array];
    _memBase = 0x2000;
    _fujiCount = -1;

    win.title = @"Debugger";
    win.releasedWhenClosed = NO;
    win.minSize = NSMakeSize(760, 520);
    win.delegate = self;
    [self buildWindow];

    __weak H89DebuggerWindow *weakSelf = self;
    _poll = [NSTimer timerWithTimeInterval:0.1 repeats:YES block:^(NSTimer *t) {
        (void)t;
        [weakSelf poll];
    }];
    [[NSRunLoop mainRunLoop] addTimer:_poll forMode:NSRunLoopCommonModes];

    /* F5/F7/F8/Shift+F8/F12 wherever the focus is in this window, the
     * prompt's field included */
    _keyMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                        handler:^NSEvent *(NSEvent *event) {
        H89DebuggerWindow *s = weakSelf;
        if (!s || event.window != s.window || !s.window.isVisible)
            return event;
        return [s handleKey:event] ? nil : event;
    }];
    return self;
}

- (void)dealloc
{
    [_poll invalidate];
    if (_keyMonitor)
        [NSEvent removeMonitor:_keyMonitor];
}

/* ---- showing and hiding ---------------------------------------------------------------- */

- (void)attach
{
    if (h89session_is_running(_session))
        h89debug_attach(_dbg);
    /* one more refresh at the next poll, once the stop has landed */
    _seenGen = h89debug_generation(_dbg) - 1;
    [self refreshAll];
}

- (void)showDebugger
{
    if (!_placed) {
        [self.window center];
        _placed = YES;
    }
    [self.window makeKeyAndOrderFront:nil];
    [self attach];
    if ([_tabs indexOfTabViewItem:_tabs.selectedTabViewItem] == TAB_PROMPT)
        [self.window makeFirstResponder:_promptIn];
}

- (void)toggleDebugger
{
    if (self.window.isVisible) {
        /* windowWillClose: detaches */
        [self.window close];
        return;
    }
    [self showDebugger];
}

- (void)windowWillClose:(NSNotification *)note
{
    if (note.object != self.window)
        return;
    h89debug_detach(_dbg);
}

- (BOOL)handleKey:(NSEvent *)event
{
    NSString *chars = event.charactersIgnoringModifiers;
    const NSEventModifierFlags mods = event.modifierFlags &
        (NSEventModifierFlagShift | NSEventModifierFlagControl | NSEventModifierFlagOption |
         NSEventModifierFlagCommand);
    if (chars.length != 1 || (mods & (NSEventModifierFlagControl | NSEventModifierFlagOption |
                                      NSEventModifierFlagCommand)))
        return NO;
    switch ([chars characterAtIndex:0]) {
    case NSF5FunctionKey:
        [self toggleRun:nil];
        return YES;
    case NSF7FunctionKey:
        [self step:nil];
        return YES;
    case NSF8FunctionKey:
        if (mods & NSEventModifierFlagShift)
            [self stepOut:nil];
        else
            [self stepOver:nil];
        return YES;
    case NSF12FunctionKey:
        [self.window close];
        return YES;
    default:
        return NO;
    }
}

- (void)poll
{
    unsigned gen;
    BOOL stopped;
    if (!self.window.isVisible)
        return;
    /* opened while the machine was off: take it when it starts */
    if (!h89debug_is_attached(_dbg) && h89session_is_running(_session)) {
        [self attach];
        return;
    }
    gen = h89debug_generation(_dbg);
    stopped = h89debug_is_stopped(_dbg) != 0;
    if (gen != _seenGen || stopped != _wasStopped) {
        _seenGen = gen;
        _wasStopped = stopped;
        [self refreshAll];
    } else if (!stopped && ++_runningTicks >= 5) {
        /* running: the live values twice a second */
        _runningTicks = 0;
        [self refreshStatus];
        [self refreshCpu];
        [self refreshMem];
        [self refreshH89];
        if ([_tabs indexOfTabViewItem:_tabs.selectedTabViewItem] == TAB_FUJINET)
            [self refreshFujinet:NO];
    }
}

/* ---- construction ------------------------------------------------------------------------- */

- (NSButton *)button:(NSString *)title tip:(NSString *)tip action:(SEL)sel
{
    NSButton *b = [NSButton buttonWithTitle:title target:self action:sel];
    b.toolTip = tip;
    [b setRefusesFirstResponder:YES];
    return b;
}

- (NSTextField *)label:(NSString *)text
{
    return [NSTextField labelWithString:text];
}

- (NSTextField *)heading:(NSString *)text
{
    NSTextField *l = [NSTextField labelWithString:text];
    l.font = [NSFont boldSystemFontOfSize:[NSFont systemFontSize]];
    return l;
}

- (NSTextField *)hint:(NSString *)text
{
    NSTextField *l = [NSTextField labelWithString:text];
    l.textColor = NSColor.secondaryLabelColor;
    l.lineBreakMode = NSLineBreakByTruncatingTail;
    [l setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                forOrientation:NSLayoutConstraintOrientationHorizontal];
    return l;
}

- (NSTextField *)field:(NSString *)placeholder width:(CGFloat)width action:(SEL)sel
{
    NSTextField *f = [[NSTextField alloc] init];
    f.placeholderString = placeholder;
    f.font = monoFont();
    f.target = self;
    f.action = sel;
    [f.cell setSendsActionOnEndEditing:NO];
    if (width > 0)
        [f.widthAnchor constraintEqualToConstant:width].active = YES;
    return f;
}

- (NSView *)buildToolbar
{
    NSStackView *bar = [[NSStackView alloc] init];
    bar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    bar.spacing = 6;

    _runBtn = [self button:@"Stop (F5)" tip:@"Run or stop" action:@selector(toggleRun:)];
    [bar addView:_runBtn inGravity:NSStackViewGravityLeading];
    [bar addView:[self button:@"Step (F7)" tip:@"One instruction" action:@selector(step:)]
       inGravity:NSStackViewGravityLeading];
    [bar addView:[self button:@"Over (F8)" tip:@"Over a CALL, RST, DJNZ or block op" action:@selector(stepOver:)]
       inGravity:NSStackViewGravityLeading];
    [bar addView:[self button:@"Out (⇧F8)" tip:@"To the return" action:@selector(stepOut:)]
       inGravity:NSStackViewGravityLeading];
    [bar addView:[self button:@"Tick" tip:@"One 2 ms clock tick" action:@selector(tick:)]
       inGravity:NSStackViewGravityLeading];

    _radix = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [_radix addItemsWithTitles:@[ @"Hex", @"Octal", @"Split octal" ]];
    [_radix selectItemAtIndex:h89debug_radix(_dbg)];
    _radix.toolTip = @"How numbers are shown: hex, octal, or Heath's split octal (041.000)";
    _radix.target = self;
    _radix.action = @selector(radixChanged:);
    [_radix setRefusesFirstResponder:YES];
    [bar addView:_radix inGravity:NSStackViewGravityLeading];

    [bar addView:[self button:@"Symbols…" tip:@"Load a symbol file" action:@selector(loadSymbols:)]
       inGravity:NSStackViewGravityTrailing];
    {
        NSPopUpButton *save = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:YES];
        [save addItemWithTitle:@"Save"];
        save.lastItem.tag = -1;
        for (int i = 0; i < NSAVES; i++) {
            [save addItemWithTitle:kSaveTitles[i]];
            save.lastItem.tag = i;
        }
        save.target = self;
        save.action = @selector(saveFile:);
        [save setRefusesFirstResponder:YES];
        [bar addView:save inGravity:NSStackViewGravityTrailing];
    }
    [bar setHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];
    [bar setContentHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];
    return bar;
}

- (NSView *)buildPrompt
{
    NSScrollView *outScroll;
    NSStackView *v;
    _promptOut = monoView(&outScroll, YES);
    _promptOut.string = @"FujiNet Go H89 debugger. Type help for the commands.\n";
    _promptIn = [self field:@"help, step, break 040.000, mem hl, radix split, fdc …  (Tab completes, ↑↓ history)"
                      width:0
                     action:@selector(runPrompt:)];
    _promptIn.delegate = self;
    v = vstack(@[ outScroll, _promptIn ]);
    fillWidth(v, outScroll, 0);
    fillWidth(v, _promptIn, 0);
    return v;
}

- (NSView *)buildCpu
{
    NSMutableArray<NSView *> *rows = [NSMutableArray array];
    NSMutableArray<NSView *> *row = nil;
    NSScrollView *memScroll;
    NSStackView *v;

    for (int i = 0; i < NREGS; i++) {
        NSTextField *l = [self label:[NSString stringWithUTF8String:kRegs[i].name]];
        l.alignment = NSTextAlignmentRight;
        [l.widthAnchor constraintEqualToConstant:34].active = YES;
        if (i % 6 == 0)
            row = [NSMutableArray array];
        _reg[i] = [self field:@"" width:84 action:@selector(applyRegister:)];
        _reg[i].tag = i;
        _reg[i].toolTip = @"Return sets it: a number in any radix, a label or an expression";
        [row addObject:l];
        [row addObject:_reg[i]];
        if (i % 6 == 5 || i == NREGS - 1)
            [rows addObject:hstack(row)];
    }
    {
        NSMutableArray<NSView *> *flags = [NSMutableArray arrayWithObject:[self label:@"Flags"]];
        for (int i = 0; i < NFLAGS; i++) {
            _flag[i] = [NSButton checkboxWithTitle:kFlagNames[i] target:self action:@selector(flagClicked:)];
            _flag[i].tag = i;
            [_flag[i] setRefusesFirstResponder:YES];
            [flags addObject:_flag[i]];
        }
        [rows addObject:hstack(flags)];
    }
    _cpuInfo = [self label:@""];
    _cpuInfo.font = monoFont();
    _cpuInfo.textColor = NSColor.secondaryLabelColor;
    [rows addObject:_cpuInfo];

    _memGoto = [self field:@"address, label or expression" width:200 action:@selector(memGoto:)];
    _memAddr = [self field:@"address" width:100 action:@selector(memWrite:)];
    _memVal = [self field:@"value" width:70 action:@selector(memWrite:)];
    [rows addObject:hstack(@[ [self label:@"Memory at"], _memGoto, [self label:@"Write"], _memAddr,
                               [self label:@"="], _memVal,
                               [self hint:@"as the CPU would write it (ROM ignores it)"] ])];

    {
        NSTextView *plain = monoView(&memScroll, NO);
        /* the dump is a fixed window onto memory: the wheel moves it */
        _mem = [[H89RowTextView alloc] initWithFrame:plain.frame];
        _mem.editable = NO;
        _mem.richText = NO;
        _mem.font = monoFont();
        _mem.textColor = NSColor.textColor;
        _mem.backgroundColor = NSColor.textBackgroundColor;
        _mem.horizontallyResizable = YES;
        _mem.textContainer.widthTracksTextView = NO;
        _mem.textContainer.containerSize = NSMakeSize(FLT_MAX, FLT_MAX);
        _mem.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        memScroll.documentView = _mem;
        __weak H89DebuggerWindow *weakSelf = self;
        _mem.onScrollRows = ^(int n) {
            H89DebuggerWindow *s = weakSelf;
            if (!s)
                return;
            const int per = h89debug_radix(s->_dbg) == H89_RADIX_HEX ? 16 : 8;
            s->_memBase = (s->_memBase + n * per) & 0xffff;
            [s refreshMem];
        };
    }
    [rows addObject:memScroll];

    v = vstack(rows);
    fillWidth(v, memScroll, 0);
    return v;
}

- (NSView *)buildDisasm
{
    NSScrollView *scroll = [[NSScrollView alloc] init];
    NSStackView *row, *v;

    _followPc = [NSButton checkboxWithTitle:@"Follow PC" target:self action:@selector(followChanged:)];
    _followPc.state = NSControlStateValueOn;
    [_followPc setRefusesFirstResponder:YES];
    _jump = [self field:@"go to an address or a label" width:220 action:@selector(jumpTo:)];
    row = hstack(@[ _followPc, _jump,
                    [self hint:@"Double-click a line to toggle its breakpoint; scroll to browse"] ]);

    scroll.hasVerticalScroller = NO;
    scroll.hasHorizontalScroller = YES;
    [scroll setContentHuggingPriority:NSLayoutPriorityDefaultLow
                       forOrientation:NSLayoutConstraintOrientationVertical];
    _disasm = [[H89RowTextView alloc] initWithFrame:NSMakeRect(0, 0, 600, 600)];
    _disasm.editable = NO;
    _disasm.richText = NO;
    _disasm.selectable = NO;
    _disasm.font = monoFont();
    _disasm.textColor = NSColor.textColor;
    _disasm.backgroundColor = NSColor.textBackgroundColor;
    _disasm.horizontallyResizable = YES;
    _disasm.textContainer.widthTracksTextView = NO;
    _disasm.textContainer.containerSize = NSMakeSize(FLT_MAX, FLT_MAX);
    _disasm.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    {
        __weak H89DebuggerWindow *weakSelf = self;
        _disasm.onDoubleClickLine = ^(int line) {
            H89DebuggerWindow *s = weakSelf;
            if (!s || line < 0 || line >= s->_lineCount)
                return;
            h89debug_breakpoint_toggle(s->_dbg, s->_lineAddr[line]);
            [s refreshDisasm];
            [s refreshBps];
        };
        _disasm.onScrollRows = ^(int n) {
            H89DebuggerWindow *s = weakSelf;
            if (!s || n == 0)
                return;
            s->_followPc.state = NSControlStateValueOff;
            s->_disasmTop = h89debug_row_address(s->_dbg, (uint16_t)s->_disasmTop, n) & 0xffff;
            [s refreshDisasm];
        };
    }
    scroll.documentView = _disasm;

    v = vstack(@[ row, scroll ]);
    fillWidth(v, row, 0);
    fillWidth(v, scroll, 0);
    return v;
}

- (NSView *)buildBreakpoints
{
    NSScrollView *bpScroll = [[NSScrollView alloc] init];
    NSClipView *clip;
    NSStackView *row, *v;

    _bpType = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [_bpType addItemsWithTitles:@[ @"Execute", @"Read", @"Write", @"Read/Write", @"Port In", @"Port Out" ]];
    _bpStart = [self field:@"start" width:120 action:@selector(addBreakpoint:)];
    _bpEnd = [self field:@"end (optional)" width:120 action:@selector(addBreakpoint:)];
    _bpCond = [self field:@"condition, e.g. a == 0 or value == 3" width:0 action:@selector(addBreakpoint:)];
    [_bpCond setContentHuggingPriority:NSLayoutPriorityDefaultLow
                        forOrientation:NSLayoutConstraintOrientationHorizontal];
    row = hstack(@[ _bpType, _bpStart, _bpEnd, _bpCond,
                    [self button:@"Add" tip:@"Add the breakpoint" action:@selector(addBreakpoint:)],
                    [self button:@"Clear All" tip:@"Remove every breakpoint" action:@selector(clearBreakpoints:)] ]);
    _bpMsg = [self label:@""];
    _bpMsg.textColor = NSColor.systemRedColor;

    /* the rows scroll: a session can collect more breakpoints than the tab
     * is tall; the list is pinned to the top and to the clip view's width */
    _bpList = [NSStackView stackViewWithViews:@[]];
    _bpList.orientation = NSUserInterfaceLayoutOrientationVertical;
    _bpList.alignment = NSLayoutAttributeLeading;
    _bpList.spacing = 4;
    _bpList.translatesAutoresizingMaskIntoConstraints = NO;
    bpScroll.hasVerticalScroller = YES;
    bpScroll.drawsBackground = NO;
    bpScroll.documentView = _bpList;
    clip = bpScroll.contentView;
    [_bpList.topAnchor constraintEqualToAnchor:clip.topAnchor].active = YES;
    [_bpList.leadingAnchor constraintEqualToAnchor:clip.leadingAnchor].active = YES;
    [_bpList.widthAnchor constraintEqualToAnchor:clip.widthAnchor].active = YES;
    [bpScroll setContentHuggingPriority:NSLayoutPriorityDefaultLow
                         forOrientation:NSLayoutConstraintOrientationVertical];

    v = vstack(@[ row, _bpMsg,
                  [self hint:@"Untick to disable. Conditions are C expressions over registers, flags, "
                             @"[addr], {addr}, value and addr"],
                  bpScroll ]);
    fillWidth(v, row, 0);
    fillWidth(v, bpScroll, 0);
    return v;
}

- (NSView *)buildH89
{
    NSScrollView *scroll;
    _h89 = monoView(&scroll, NO);
    return scroll;
}

/* The packets left, a note and Clear over them; the card's state right. */
- (NSView *)buildFujinet
{
    NSScrollView *fujiScroll, *stateScroll;
    NSStackView *row, *left, *right, *split;

    _fujiNote = [self hint:@""];
    [_fujiNote setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    row = hstack(@[ _fujiNote, [self button:@"Clear" tip:@"Forget the packets so far" action:@selector(fujiClear:)] ]);
    row.distribution = NSStackViewDistributionFill;
    _fujiText = monoView(&fujiScroll, NO);
    _fujiState = monoView(&stateScroll, YES);
    left = vstack(@[ [self heading:@"FujiBus packets (SLIP-decoded)"], row, fujiScroll ]);
    fillWidth(left, row, 0);
    fillWidth(left, fujiScroll, 0);
    right = vstack(@[ [self heading:@"FujiNet card (82C55)"], stateScroll ]);
    fillWidth(right, stateScroll, 0);

    split = hstack(@[ left, right ]);
    split.distribution = NSStackViewDistributionFill;
    split.alignment = NSLayoutAttributeTop;
    split.spacing = 12;
    [left.widthAnchor constraintGreaterThanOrEqualToConstant:480].active = YES;
    [right.widthAnchor constraintEqualToConstant:320].active = YES;
    [left.heightAnchor constraintEqualToAnchor:split.heightAnchor].active = YES;
    [right.heightAnchor constraintEqualToAnchor:split.heightAnchor].active = YES;
    return split;
}

- (void)buildWindow
{
    NSArray *pages = @[ @[ @"Prompt", [self buildPrompt] ], @[ @"CPU & Memory", [self buildCpu] ],
                        @[ @"Disassembly", [self buildDisasm] ], @[ @"Breakpoints", [self buildBreakpoints] ],
                        @[ @"H89", [self buildH89] ], @[ @"FujiNet", [self buildFujinet] ] ];
    NSView *toolbar = [self buildToolbar];
    NSStackView *root;
    const char *tab;

    _status = [self label:@""];
    _status.textColor = NSColor.secondaryLabelColor;
    _status.lineBreakMode = NSLineBreakByTruncatingTail;
    [_status setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                      forOrientation:NSLayoutConstraintOrientationHorizontal];

    _tabs = [[NSTabView alloc] init];
    for (NSArray *page in pages) {
        NSTabViewItem *item = [[NSTabViewItem alloc] initWithIdentifier:page[0]];
        NSView *content = page[1];
        NSView *holder = [[NSView alloc] init];
        item.label = page[0];
        [holder addSubview:content];
        content.translatesAutoresizingMaskIntoConstraints = NO;
        [content.leadingAnchor constraintEqualToAnchor:holder.leadingAnchor constant:6].active = YES;
        [content.trailingAnchor constraintEqualToAnchor:holder.trailingAnchor constant:-6].active = YES;
        [content.topAnchor constraintEqualToAnchor:holder.topAnchor constant:6].active = YES;
        [content.bottomAnchor constraintEqualToAnchor:holder.bottomAnchor constant:-6].active = YES;
        item.view = holder;
        [_tabs addTabViewItem:item];
    }
    [_tabs setContentHuggingPriority:NSLayoutPriorityDefaultLow
                      forOrientation:NSLayoutConstraintOrientationVertical];
    tab = getenv("H89_DEBUGGER_TAB");
    if (tab && *tab) {
        int t = -1;
        for (int i = 0; i < NTABS; i++)
            if (!strcasecmp(tab, kTabNames[i]))
                t = i;
        if (t < 0 && tab[0] >= '0' && tab[0] <= '9')
            t = atoi(tab);
        if (t >= 0 && t < (int)_tabs.numberOfTabViewItems)
            [_tabs selectTabViewItemAtIndex:t];
    }
    _tabs.delegate = self;

    root = vstack(@[ toolbar, _status, _tabs ]);
    root.edgeInsets = NSEdgeInsetsMake(8, 8, 8, 8);
    fillWidth(root, toolbar, 16);
    fillWidth(root, _status, 16);
    fillWidth(root, _tabs, 16);
    self.window.contentView = root;
}

/* The Prompt's field takes the keys when its tab shows. */
- (void)tabView:(NSTabView *)tabView didSelectTabViewItem:(NSTabViewItem *)item
{
    if ([tabView indexOfTabViewItem:item] == TAB_PROMPT)
        [self.window makeFirstResponder:_promptIn];
    if ([tabView indexOfTabViewItem:item] == TAB_FUJINET)
        [self refreshFujinet:NO];
}

/* ---- actions ---------------------------------------------------------------------------------- */

- (BOOL)eval:(NSString *)text value:(long *)out
{
    char err[96];
    NSString *t = trimmed(text ?: @"");
    if (t.length == 0)
        return NO;
    return h89debug_eval(_dbg, t.UTF8String, out, err, (int)sizeof err) == 0;
}

- (void)say:(NSString *)msg
{
    [self appendPrompt:msg];
    _status.stringValue = trimmed([msg stringByTrimmingCharactersInSet:[NSCharacterSet newlineCharacterSet]]);
}

- (void)toggleRun:(id)sender
{
    (void)sender;
    if (h89debug_is_stopped(_dbg))
        h89debug_resume(_dbg);
    else
        h89debug_stop(_dbg);
    [self refreshAll];
}

- (void)step:(id)sender
{
    (void)sender;
    h89debug_step(_dbg);
    [self refreshAll];
}

- (void)stepOver:(id)sender
{
    (void)sender;
    h89debug_step_over(_dbg);
    [self refreshAll];
}

- (void)stepOut:(id)sender
{
    (void)sender;
    h89debug_step_out(_dbg);
    [self refreshAll];
}

- (void)tick:(id)sender
{
    (void)sender;
    h89debug_ticks(_dbg, 1);
    [self refreshAll];
}

- (void)radixChanged:(id)sender
{
    const int r = (int)_radix.indexOfSelectedItem;
    (void)sender;
    if (r < 0)
        return;
    h89debug_set_radix(_dbg, r);
    h89session_set_int(_session, "debugger.radix", h89debug_radix(_dbg));
    [self refreshAll];
}

- (void)appendPrompt:(NSString *)text
{
    NSString *t = [text hasSuffix:@"\n"] ? text : [text stringByAppendingString:@"\n"];
    NSDictionary *attrs = @{ NSFontAttributeName: monoFont(), NSForegroundColorAttributeName: NSColor.textColor };
    [_promptOut.textStorage appendAttributedString:[[NSAttributedString alloc] initWithString:t attributes:attrs]];
    [_promptOut scrollRangeToVisible:NSMakeRange(_promptOut.string.length, 0)];
}

- (void)runPrompt:(id)sender
{
    static char out[65536];
    NSString *cmd = trimmed(_promptIn.stringValue);
    (void)sender;
    if (cmd.length == 0)
        return;
    if (![_history.lastObject isEqualToString:cmd]) {
        [_history addObject:cmd];
        if (_history.count > 64)
            [_history removeObjectAtIndex:0];
    }
    _historyPos = (NSInteger)_history.count;
    [self appendPrompt:[NSString stringWithFormat:@"> %@", cmd]];
    out[0] = '\0';
    h89debug_command(_dbg, cmd.UTF8String, out, (int)sizeof out);
    if (out[0])
        [self appendPrompt:textFrom(out)];
    _promptIn.stringValue = @"";
    [self refreshAll];
}

/* In the prompt, Tab completes instead of moving the focus, and Up/Down
 * walk the history. */
- (BOOL)control:(NSControl *)control textView:(NSTextView *)textView doCommandBySelector:(SEL)sel
{
    (void)textView;
    if (control != _promptIn)
        return NO;
    if (sel == @selector(moveUp:) || sel == @selector(moveDown:)) {
        const NSInteger n = (NSInteger)_history.count;
        if (n == 0)
            return YES;
        _historyPos += sel == @selector(moveUp:) ? -1 : 1;
        if (_historyPos < 0)
            _historyPos = 0;
        if (_historyPos > n)
            _historyPos = n;
        _promptIn.stringValue = _historyPos < n ? _history[(NSUInteger)_historyPos] : @"";
        [_promptIn.currentEditor setSelectedRange:NSMakeRange(_promptIn.stringValue.length, 0)];
        return YES;
    }
    if (sel != @selector(insertTab:))
        return NO;
    {
        char comps[4096];
        NSString *text = _promptIn.stringValue;
        const NSRange sp = [text rangeOfString:@" " options:NSBackwardsSearch];
        const int n = h89debug_completions(_dbg, text.UTF8String, comps, (int)sizeof comps);
        if (n == 1) {
            NSString *c = [textFrom(comps) componentsSeparatedByString:@"\n"][0];
            NSString *head = sp.location == NSNotFound ? @"" : [text substringToIndex:sp.location + 1];
            _promptIn.stringValue = [NSString stringWithFormat:@"%@%@ ", head, c];
            [_promptIn.currentEditor setSelectedRange:NSMakeRange(_promptIn.stringValue.length, 0)];
        } else if (n > 1) {
            [self appendPrompt:textFrom(comps)];
        }
    }
    return YES;
}

- (void)loadSymbols:(id)sender
{
    char msg[512];
    NSOpenPanel *p = [NSOpenPanel openPanel];
    (void)sender;
    p.title = @"Load Symbols (z88dk .map, SDCC .noi, WLA-DX .sym, or an address list)";
    p.allowedContentTypes = typesForExtensions(@[ @"map", @"noi", @"sym", @"txt" ]);
    if ([p runModal] != NSModalResponseOK)
        return;
    msg[0] = '\0';
    h89debug_load_symbols(_dbg, p.URL.fileSystemRepresentation, msg, (int)sizeof msg);
    [self say:textFrom(msg)];
    [self refreshAll];
}

- (void)saveFile:(NSPopUpButton *)sender
{
    char msg[512];
    const NSInteger kind = sender.selectedItem.tag;
    NSSavePanel *p;
    if (kind < 0 || kind >= NSAVES)
        return;
    p = [NSSavePanel savePanel];
    p.title = [NSString stringWithFormat:@"Save %@", kSaveTitles[kind]];
    p.nameFieldStringValue = kSaveFiles[kind];
    if ([p runModal] != NSModalResponseOK)
        return;
    msg[0] = '\0';
    h89debug_save(_dbg, kSaveKinds[kind], p.URL.fileSystemRepresentation, msg, (int)sizeof msg);
    [self say:textFrom(msg)];
}

- (void)applyRegister:(NSTextField *)sender
{
    long v;
    const NSInteger i = sender.tag;
    if (i >= 0 && i < NREGS && [self eval:sender.stringValue value:&v])
        h89debug_cpu_set(_dbg, kRegs[i].reg, (int)v);
    else if (i >= 0 && i < NREGS)
        _status.stringValue = [NSString stringWithFormat:@"Not a value for %s: %@", kRegs[i].name,
                                                         sender.stringValue];
    [self.window makeFirstResponder:nil];
    [self refreshAll];
}

- (void)flagClicked:(NSButton *)sender
{
    const NSInteger i = sender.tag;
    if (i >= 0 && i < NFLAGS && h89debug_is_stopped(_dbg))
        h89debug_cpu_set(_dbg, kFlagIds[i], sender.state == NSControlStateValueOn ? 1 : 0);
    [self refreshCpu];
}

- (void)memGoto:(id)sender
{
    long a;
    (void)sender;
    if ([self eval:_memGoto.stringValue value:&a])
        _memBase = (int)(a & 0xfff0);
    else
        _status.stringValue = [NSString stringWithFormat:@"Not an address: %@", _memGoto.stringValue];
    [self refreshMem];
}

- (void)memWrite:(id)sender
{
    long a, v;
    (void)sender;
    if (![self eval:_memAddr.stringValue value:&a] || ![self eval:_memVal.stringValue value:&v]) {
        _status.stringValue = @"An address and a value, please";
        return;
    }
    h89debug_write(_dbg, (uint16_t)(a & 0xffff), (uint8_t)(v & 0xff));
    [self refreshAll];
}

- (void)followChanged:(id)sender
{
    (void)sender;
    [self refreshDisasm];
}

- (void)jumpTo:(id)sender
{
    long a;
    (void)sender;
    if (![self eval:_jump.stringValue value:&a]) {
        _status.stringValue = [NSString stringWithFormat:@"Not an address: %@", _jump.stringValue];
        return;
    }
    _followPc.state = NSControlStateValueOff;
    _disasmTop = (int)(a & 0xffff);
    [self refreshDisasm];
}

- (void)addBreakpoint:(id)sender
{
    long a, b;
    NSInteger t = _bpType.indexOfSelectedItem;
    int type, bpId;
    BOOL port;
    (void)sender;
    if (t < 0 || t >= 6)
        t = 0;
    type = kBpTypes[t];
    port = (type & (H89DEBUG_BP_IN | H89DEBUG_BP_OUT)) != 0;
    if (![self eval:_bpStart.stringValue value:&a]) {
        _bpMsg.stringValue = @"The start is not an address, a label or an expression";
        return;
    }
    b = a;
    if (trimmed(_bpEnd.stringValue).length > 0 && ![self eval:_bpEnd.stringValue value:&b]) {
        _bpMsg.stringValue = @"The end is not an address";
        return;
    }
    if (b < a) {
        _bpMsg.stringValue = @"The end is before the start";
        return;
    }
    if (port && (a < 0 || b > 0xff)) {
        _bpMsg.stringValue = @"A port is 0 to 255 (377Q)";
        return;
    }
    bpId = h89debug_breakpoint_add(_dbg, type, (uint16_t)(a & 0xffff), (uint16_t)(b & 0xffff),
                                   trimmed(_bpCond.stringValue).UTF8String);
    if (bpId < 0) {
        _bpMsg.stringValue = @"The condition does not parse";
        return;
    }
    _bpMsg.stringValue = @"";
    _bpStart.stringValue = @"";
    _bpEnd.stringValue = @"";
    _bpCond.stringValue = @"";
    [self refreshAll];
}

- (void)bpEnabled:(NSButton *)sender
{
    h89debug_breakpoint_enable(_dbg, (int)sender.tag, sender.state == NSControlStateValueOn ? 1 : 0);
    [self refreshAll];
}

- (void)bpRemove:(NSButton *)sender
{
    h89debug_breakpoint_remove(_dbg, (int)sender.tag);
    [self refreshAll];
}

- (void)clearBreakpoints:(id)sender
{
    (void)sender;
    h89debug_breakpoint_clear(_dbg);
    _bpMsg.stringValue = @"";
    [self refreshAll];
}

- (void)fujiClear:(id)sender
{
    (void)sender;
    h89debug_fujinet_trace_clear(_dbg);
    [self refreshFujinet:YES];
}

/* ---- refreshers ------------------------------------------------------------------------------- */

- (void)refreshStatus
{
    char reason[160];
    int addr = -1;
    const BOOL stopped = h89debug_is_stopped(_dbg) != 0;
    reason[0] = '\0';
    h89debug_stop_reason(_dbg, reason, (int)sizeof reason, &addr);
    if (stopped)
        _status.stringValue = reason[0] ? [NSString stringWithFormat:@"Stopped: %@", textFrom(reason)] : @"Stopped";
    else
        _status.stringValue = h89debug_is_attached(_dbg) ? @"Running" : @"The machine is off";
    _runBtn.title = stopped ? @"Run (F5)" : @"Stop (F5)";
    _runBtn.bezelColor = stopped ? accentColor() : nil;
    if (_radix.indexOfSelectedItem != (NSInteger)h89debug_radix(_dbg))
        [_radix selectItemAtIndex:h89debug_radix(_dbg)];
}

- (void)refreshCpu
{
    h89debug_cpu c;
    char buf[64];
    memset(&c, 0, sizeof c);
    h89debug_cpu_get(_dbg, &c);
    {
        const int vals[NREGS] = { c.pc, c.sp, c.af, c.bc, c.de, c.hl, c.ix, c.iy, c.af2, c.bc2,
                                  c.de2, c.hl2, c.i, c.r, c.im, c.iff1, c.iff2 };
        for (int i = 0; i < NREGS; i++) {
            if (_reg[i].currentEditor)
                continue;   /* being typed in */
            if (kRegs[i].wide > 0)
                h89debug_fmt16(_dbg, (unsigned)vals[i], buf, (int)sizeof buf);
            else if (kRegs[i].wide == 0)
                h89debug_fmt8(_dbg, (unsigned)vals[i], buf, (int)sizeof buf);
            else
                snprintf(buf, sizeof buf, "%d", vals[i]);
            _reg[i].stringValue = textFrom(buf);
        }
    }
    {
        const int flags[NFLAGS] = { c.sf, c.zf, c.hf, c.pf, c.nf, c.cf };
        for (int i = 0; i < NFLAGS; i++)
            _flag[i].state = flags[i] ? NSControlStateValueOn : NSControlStateValueOff;
    }
    _cpuInfo.stringValue = [NSString stringWithFormat:@"%llu cycles   %llu ticks of the 2 ms clock%@",
                                                      (unsigned long long)c.cycles, (unsigned long long)c.ticks,
                                                      c.halted ? @"   HALT" : @""];
}

- (void)refreshMem
{
    static char text[MEM_ROWS * 128 + 64];
    const int per = h89debug_radix(_dbg) == H89_RADIX_HEX ? 16 : 8;
    size_t len = 0;
    text[0] = '\0';
    for (int r = 0; r < MEM_ROWS; r++) {
        uint8_t row[16];
        char at[16], v[16];
        const uint16_t a = (uint16_t)((_memBase + r * per) & 0xffff);
        memset(row, 0, sizeof row);
        h89debug_read(_dbg, a, row, per);
        len += (size_t)snprintf(text + len, sizeof text - len, "%-8s ", h89debug_fmt16(_dbg, a, at, (int)sizeof at));
        for (int i = 0; i < per && len < sizeof text; i++)
            len += (size_t)snprintf(text + len, sizeof text - len, "%s ", h89debug_fmt8(_dbg, row[i], v, (int)sizeof v));
        if (len < sizeof text)
            len += (size_t)snprintf(text + len, sizeof text - len, " ");
        for (int i = 0; i < per && len < sizeof text; i++)
            len += (size_t)snprintf(text + len, sizeof text - len, "%c",
                                    row[i] >= 0x20 && row[i] < 0x7f ? (char)row[i] : '.');
        if (len < sizeof text)
            len += (size_t)snprintf(text + len, sizeof text - len, "\n");
        if (len >= sizeof text)
            break;
    }
    _mem.string = textFrom(text);
}

- (void)refreshDisasm
{
    static h89debug_line lines[DISASM_WINDOW];
    NSMutableString *text = [NSMutableString string];
    NSRange pcRange = NSMakeRange(NSNotFound, 0);
    int pcLine = -1, n;

    /* following, the PC sits a third of the way down */
    if (_followPc.state == NSControlStateValueOn) {
        h89debug_cpu c;
        memset(&c, 0, sizeof c);
        h89debug_cpu_get(_dbg, &c);
        _disasmTop = h89debug_row_address(_dbg, (uint16_t)c.pc, -(DISASM_WINDOW / 3)) & 0xffff;
    }
    n = h89debug_disassemble(_dbg, (uint16_t)_disasmTop, lines, DISASM_WINDOW, &pcLine);
    if (n < 0)
        n = 0;
    _lineCount = n;
    for (int i = 0; i < n; i++) {
        char line[300];
        NSString *s;
        _lineAddr[i] = lines[i].address;
        snprintf(line, sizeof line, "%c%c %-8s %-16s %-14s %s%s%s\n", lines[i].has_breakpoint ? '*' : ' ',
                 lines[i].is_pc ? '>' : ' ', lines[i].addr_text, lines[i].bytes, lines[i].label, lines[i].disasm,
                 lines[i].comment[0] ? "  ; " : "", lines[i].comment);
        s = textFrom(line);
        if (i == pcLine && s.length > 0)
            pcRange = NSMakeRange(text.length, s.length - 1);
        [text appendString:s];
    }
    if (n == 0)
        [text appendString:@"(no disassembly)\n"];
    _disasm.string = text;
    if (pcRange.location != NSNotFound)
        [_disasm.textStorage addAttributes:@{ NSBackgroundColorAttributeName: accentColor(),
                                              NSForegroundColorAttributeName: NSColor.blackColor }
                                     range:pcRange];
}

static const char *bpTypeName(int type)
{
    if (type & H89DEBUG_BP_EXEC)
        return "Execute";
    if ((type & (H89DEBUG_BP_READ | H89DEBUG_BP_WRITE)) == (H89DEBUG_BP_READ | H89DEBUG_BP_WRITE))
        return "Read/Write";
    if (type & H89DEBUG_BP_READ)
        return "Read";
    if (type & H89DEBUG_BP_WRITE)
        return "Write";
    if (type & H89DEBUG_BP_IN)
        return "Port In";
    return "Port Out";
}

- (void)refreshBps
{
    static h89debug_breakpoint bps[MAX_BPS];
    const int n = h89debug_breakpoint_list(_dbg, bps, MAX_BPS);
    for (NSView *v in [_bpList.arrangedSubviews copy])
        [_bpList removeView:v];
    if (n <= 0) {
        [_bpList addArrangedSubview:[self hint:@"No breakpoints. Double-click a disassembly line, or add one above."]];
        return;
    }
    for (int i = 0; i < n; i++) {
        char text[320], range[40], a[16], b[16];
        const BOOL port = (bps[i].type & (H89DEBUG_BP_IN | H89DEBUG_BP_OUT)) != 0;
        NSButton *box, *remove;
        if (port) {
            h89debug_fmt8(_dbg, bps[i].start, a, (int)sizeof a);
            h89debug_fmt8(_dbg, bps[i].end, b, (int)sizeof b);
        } else {
            h89debug_fmt16(_dbg, bps[i].start, a, (int)sizeof a);
            h89debug_fmt16(_dbg, bps[i].end, b, (int)sizeof b);
        }
        if (bps[i].end != bps[i].start)
            snprintf(range, sizeof range, "%s-%s", a, b);
        else
            snprintf(range, sizeof range, "%s", a);
        snprintf(text, sizeof text, "#%-3d %-10s %-16s hits %-6u %s%s", bps[i].id, bpTypeName(bps[i].type), range,
                 bps[i].hits, bps[i].condition[0] ? "if " : "", bps[i].condition);
        box = [NSButton checkboxWithTitle:textFrom(text) target:self action:@selector(bpEnabled:)];
        box.state = bps[i].enabled ? NSControlStateValueOn : NSControlStateValueOff;
        box.tag = bps[i].id;
        box.font = monoFont();
        box.toolTip = @"Enabled";
        remove = [self button:@"Remove" tip:@"Remove this breakpoint" action:@selector(bpRemove:)];
        remove.tag = bps[i].id;
        [_bpList addArrangedSubview:hstack(@[ box, remove ])];
    }
}

- (void)refreshH89
{
    static char part[4096];
    NSMutableString *text = [NSMutableString string];
    for (int s = 0; s < H89DEBUG_SECTION_COUNT; s++) {
        part[0] = '\0';
        h89debug_section(_dbg, s, part, (int)sizeof part);
        [text appendFormat:@"%@%@\n%@", s ? @"\n" : @"", textFrom(h89debug_section_name(s)), textFrom(part)];
        if (![text hasSuffix:@"\n"])
            [text appendString:@"\n"];
    }
    setTextKeepingPlace((NSTextView *)_h89, text);
}

- (void)refreshFujinet:(BOOL)force
{
    static h89_fuji_packet p[TRACE_MAX];
    static char state[4096];
    int n = h89debug_fujinet_trace(_dbg, p, TRACE_MAX);
    const int link = h89session_card_link(_session);
    NSMutableString *t;
    BOOL follow;

    if (n < 0)
        n = 0;
    if (!h89session_fujinet_running(_session) && link != 2)
        _fujiNote.stringValue = @"FujiNet is off (Preferences ▸ FujiNet).";
    else
        _fujiNote.stringValue = link < 0 ? @"No FujiNet card" : textFrom(h89session_card_link_text(link));
    state[0] = '\0';
    h89debug_fujinet_state(_dbg, state, (int)sizeof state);
    setTextAt(_fujiState, textFrom(state), NO);

    if (!force && n == _fujiCount && (n == 0 || p[n - 1].serial == _fujiLast))
        return;
    _fujiCount = n;
    _fujiLast = n > 0 ? p[n - 1].serial : 0;
    follow = force || atEnd(_fujiText);
    t = [NSMutableString string];
    for (int i = 0; i < n; i++) {
        [t appendFormat:@"%@%@\n     ", i ? @"\n" : @"", textFrom(p[i].text)];
        for (int k = 0; k < p[i].nbytes && k < (int)sizeof p[i].data; k++)
            [t appendFormat:@"%02X ", (unsigned)p[i].data[k]];
        if (p[i].nbytes > (int)sizeof p[i].data)
            [t appendString:@"…"];
    }
    if (n == 0)
        [t appendString:@"(no FujiBus packets since the trace was cleared)"];
    setTextAt(_fujiText, t, follow);
}

- (void)refreshAll
{
    [self refreshStatus];
    [self refreshCpu];
    [self refreshMem];
    [self refreshDisasm];
    [self refreshBps];
    [self refreshH89];
    if ([_tabs indexOfTabViewItem:_tabs.selectedTabViewItem] == TAB_FUJINET)
        [self refreshFujinet:NO];
}
@end
