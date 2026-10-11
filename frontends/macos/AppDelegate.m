/*
 * H89AppDelegate -- the window, the menu bar and the session's lifetime.
 *
 * The same app as the GNOME, KDE and Win32 frontends: the H19's screen with
 * a status line under it (the disk light, the FujiNet card's link), menus
 * for the machine, its drives, the clipboard, the view and FujiNet, and the
 * H19 Keys, Settings and debugger windows. Command is the Mac's shortcut
 * key, so Control stays the H19's (CP/M wants ^C, ^P, ^S and the rest).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#import "AppDelegate.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#import "DisplayView.h"
#import "H19KeysWindow.h"
#import "KeyForward.h"
#import "SettingsWindow.h"
#import "debugger/DebuggerWindow.h"

#include <stdlib.h>
#include <string.h>

#ifndef H89_VERSION_STRING
#define H89_VERSION_STRING "unknown"
#endif

#define APP_TITLE @"FujiNet Go H89"
#define STATUS_HEIGHT 24.0

/* Drive menu items: the tag is the drive's id and the action. */
enum { DRIVE_INSERT = 0, DRIVE_EJECT = 1, DRIVE_WP = 2 };
#define DRIVE_TAG(id, action) ((id) * 4 + (action))

static NSString *const kH17Exts[] = { @"h8d", @"h17", @"h17disk", @"h17raw", @"rawdisk", @"tmpdisk" };
static NSString *const kSoftExts[] = { @"imd", @"td0", @"mmsdisk", @"logdisk", @"rawfloppy" };

/* The open panels' filters: a type per extension (a dynamic one for an
 * extension nothing has registered, which still matches by extension). */
static NSArray<UTType *> *typesForExtensions(NSArray<NSString *> *exts)
{
    NSMutableArray<UTType *> *types = [NSMutableArray array];
    for (NSString *ext in exts) {
        UTType *t = [UTType typeWithFilenameExtension:ext];
        if (t) [types addObject:t];
    }
    return types;
}

static NSString *str(const char *s)
{
    return s ? [NSString stringWithUTF8String:s] : @"";
}

@class H89AppDelegate;

/* The content view sits between AppKit and the session: it forwards keys
 * and the drop, and leaves the display purely about pixels. */
@interface H89ContentView : NSView
@property (nonatomic) h89session *session;
@property (nonatomic, weak) H89AppDelegate *owner;
@end

@interface H89AppDelegate ()
- (void)loadMedia:(NSString *)path;
@end

@implementation H89ContentView

- (BOOL)acceptsFirstResponder { return YES; }

- (void)keyDown:(NSEvent *)e
{
    const uint32_t ks = H89KeysymFromEvent(e);
    if (!([e modifierFlags] & NSEventModifierFlagCommand)) {
        if (ks == H89_KEYSYM_F1 + 8) { [self.owner toggleKeys:nil]; return; }
        if (ks == H89_KEYSYM_F1 + 10) { [self.owner toggleFullScreen:nil]; return; }
        if (ks == H89_KEYSYM_F1 + 11) { [self.owner toggleDebugger:nil]; return; }
    }
    if (!H89ForwardKeyDown(self.session, e)) [super keyDown:e];
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    (void)sender;
    return NSDragOperationCopy;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    NSArray *urls = [[sender draggingPasteboard] readObjectsForClasses:@[[NSURL class]] options:nil];
    if ([urls count] == 0) return NO;
    [self.owner loadMedia:[(NSURL *)urls[0] path]];
    return YES;
}
@end

/* A round light for the status line. */
@interface H89Light : NSView
@property (nonatomic) BOOL on;
@end
@implementation H89Light
- (void)setOn:(BOOL)on
{
    if (on == _on) return;
    _on = on;
    [self setNeedsDisplay:YES];
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    const NSRect b = NSInsetRect([self bounds], 1, 1);
    [(_on ? H89AccentColor() : [NSColor tertiaryLabelColor]) setFill];
    [[NSBezierPath bezierPathWithOvalInRect:b] fill];
}
@end

@implementation H89AppDelegate {
    h89session *_session;
    const char *_mediaPath;
    NSString *_pendingMedia;     /* a file Finder handed over before launch finished */
    BOOL _launched;
    NSWindow *_window;
    H89DisplayView *_display;
    H89ContentView *_content;
    H89Light *_diskLight, *_linkLight;
    NSTextField *_statusText;
    NSString *_message;          /* a passing message in the status line */
    NSUInteger _messageSerial;
    NSTimer *_statusTimer, *_fastTimer;

    NSMenu *_drivesMenu;
    NSMenuItem *_aspectItem, *_smoothItem, *_warpItem;

    H89H19KeysWindow *_keys;
    H89DebuggerWindow *_debugger;
    H89SettingsWindow *_settings;

    NSWindow *_logWindow;
    NSTextView *_logView;
    NSTimer *_logTimer;

    BOOL _askingForRoms;
}

- (instancetype)initWithSession:(h89session *)session mediaPath:(const char *)mediaPath
{
    self = [super init];
    if (!self) return nil;
    _session = session;
    _mediaPath = mediaPath;
    return self;
}

- (NSString *)lastError
{
    const char *e = h89session_last_error(_session);
    return (e && e[0]) ? str(e) : @"The machine is not running.";
}

- (void)alert:(NSString *)title text:(NSString *)text
{
    NSAlert *a = [[NSAlert alloc] init];
    [a setMessageText:title];
    if (text) [a setInformativeText:text];
    [a runModal];
}

/* ---- launch ------------------------------------------------------------------- */

- (void)applicationDidFinishLaunching:(NSNotification *)note
{
    (void)note;

    /* The H19's 640x500 at 4:3, half again: a comfortable first window. */
    const NSRect frame = NSMakeRect(0, 0, 960, 720 + STATUS_HEIGHT);
    _window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [_window setTitle:APP_TITLE];
    [_window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
    [_window setContentMinSize:NSMakeSize(320, 250 + STATUS_HEIGHT)];
    [_window center];

    _content = [[H89ContentView alloc] initWithFrame:frame];
    _content.session = _session;
    _content.owner = self;
    [_content setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [_content registerForDraggedTypes:@[NSPasteboardTypeFileURL]];

    _display = [[H89DisplayView alloc] initWithSession:_session];
    [_display setFrame:NSMakeRect(0, STATUS_HEIGHT, frame.size.width, frame.size.height - STATUS_HEIGHT)];
    [_display setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [_content addSubview:_display];

    /* the status line: the disk light, the link light, the words */
    _diskLight = [[H89Light alloc] initWithFrame:NSMakeRect(8, 7, 10, 10)];
    _diskLight.toolTip = @"Disk activity";
    [_content addSubview:_diskLight];
    _linkLight = [[H89Light alloc] initWithFrame:NSMakeRect(24, 7, 10, 10)];
    _linkLight.toolTip = @"The FujiNet card's link";
    [_content addSubview:_linkLight];
    _statusText = [NSTextField labelWithString:@""];
    _statusText.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    _statusText.lineBreakMode = NSLineBreakByTruncatingTail;
    _statusText.frame = NSMakeRect(42, 4, frame.size.width - 50, 16);
    _statusText.autoresizingMask = NSViewWidthSizable;
    [_content addSubview:_statusText];

    [_window setContentView:_content];
    [self applyView];
    [self buildMenu];
    [_window makeKeyAndOrderFront:nil];
    [_window makeFirstResponder:_content];
    [_window setDelegate:self];

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(viewSettingsChanged:)
                                                 name:H89ViewSettingsChangedNotification object:nil];

    /* FujiNet first, then the machine; without its ROMs, ask for them. */
    [self start];
    _launched = YES;

    /* A file from the command line or from Finder is routed as a drop. */
    NSString *media = _pendingMedia;
    if (!media && _mediaPath)
        media = [[NSFileManager defaultManager] stringWithFileSystemRepresentation:_mediaPath
                                                                            length:strlen(_mediaPath)];
    _pendingMedia = nil;
    if (media) [self loadMedia:media];

    /* The family's launch hooks, for when the app misbehaves before a menu
     * is reachable. */
    if (getenv("H89_OPEN_KEYS")) [self toggleKeys:nil];
    if (getenv("H89_OPEN_DEBUGGER")) [[self debugger] showDebugger];
    if (getenv("H89_OPEN_SETTINGS")) [self showSettings:nil];

    _statusTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 repeats:YES
                                                     block:^(NSTimer *t) { (void)t; [self updateStatus]; }];
    _fastTimer = [NSTimer scheduledTimerWithTimeInterval:0.25 repeats:YES
                                                   block:^(NSTimer *t) { (void)t; [self fastTick]; }];
    [self updateStatus];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app
{
    (void)app;
    return YES;
}

- (void)applicationWillTerminate:(NSNotification *)note
{
    (void)note;
    [_statusTimer invalidate];
    [_fastTimer invalidate];
    [_logTimer invalidate];
    [_display stop];
    /* writes every disk back */
    h89session_stop(_session);
    h89session_settings_flush(_session);
    h89session_free(_session);
    _session = NULL;
}

- (BOOL)application:(NSApplication *)app openFile:(NSString *)filename
{
    (void)app;
    /* Opening a file from Finder launches the app and delivers the file
     * before applicationDidFinishLaunching:, when there is no machine yet
     * to put it in: hold it until there is. */
    if (!_launched) {
        _pendingMedia = filename;
        return YES;
    }
    [self loadMedia:filename];
    return YES;
}

/* ---- starting, ROMs ------------------------------------------------------------- */

- (void)start
{
    if (h89session_is_running(_session)) return;
    h89session_start_opts opts;
    h89session_default_opts(_session, &opts);
    if (h89session_start(_session, &opts) == 0) {
        [self updateStatus];
        return;
    }
    char what[512];
    if (h89session_missing_roms(_session, what, sizeof what) > 0) {
        /* after launch finishes: a modal alert inside it stalls the menu bar */
        dispatch_async(dispatch_get_main_queue(), ^{ [self romSetup]; });
    } else {
        [self alert:@"Cannot Start the H89" text:[self lastError]];
    }
    [self updateStatus];
}

/* Power cycle onto the settings, or restart the whole session (FujiNet or
 * audio switched). */
- (void)restart:(BOOL)host
{
    if (!h89session_is_running(_session)) {
        [self start];
        return;
    }
    if (host) {
        h89session_stop(_session);
        [self start];
        return;
    }
    if (h89session_power_cycle(_session) != 0)
        [self alert:@"Cannot Power Cycle" text:[self lastError]];
    [self updateStatus];
}

- (void)romSetup
{
    if (_askingForRoms) return;
    char what[512];
    if (h89session_missing_roms(_session, what, sizeof what) == 0) return;
    _askingForRoms = YES;
    NSAlert *a = [[NSAlert alloc] init];
    a.messageText = @"Import the H89's ROMs";
    a.informativeText = [NSString stringWithFormat:
        @"FujiNet Go H89 does not come with the Heath ROMs. Import them from MAME's h89.zip and "
         "heath_tlb.zip, or from ROM image files: they are recognised by their contents and kept in\n"
         "%@\n\nStill needed: %@.", str(h89session_roms_path(_session)), str(what)];
    [a addButtonWithTitle:@"Import…"];
    [a addButtonWithTitle:@"Not Now"];
    const NSModalResponse r = [a runModal];
    _askingForRoms = NO;
    if (r != NSAlertFirstButtonReturn) return;
    if ([self importRoms] > 0 && h89session_missing_roms(_session, what, sizeof what) == 0)
        [self start];
    else if (h89session_missing_roms(_session, what, sizeof what) > 0)
        dispatch_async(dispatch_get_main_queue(), ^{ [self romSetup]; });
}

/* Imports the ROM files the user picks; returns how many images. */
- (int)importRoms
{
    NSOpenPanel *p = [NSOpenPanel openPanel];
    p.title = @"Import System ROMs";
    p.allowsMultipleSelection = YES;
    p.allowedContentTypes = typesForExtensions(@[@"zip", @"bin", @"rom"]);
    if ([p runModal] != NSModalResponseOK) return 0;
    int total = 0;
    NSMutableArray<NSString *> *log = [NSMutableArray array];
    for (NSURL *url in [p URLs]) {
        char msg[1024];
        const int n = h89session_import_roms(_session, [[url path] fileSystemRepresentation], msg, sizeof msg);
        if (n > 0) total += n;
        [log addObject:str(msg)];
    }
    [self alert:(total > 0 ? @"ROMs Imported" : @"Nothing Imported") text:[log componentsJoinedByString:@"\n\n"]];
    return total;
}

- (void)importRoms:(id)sender
{
    (void)sender;
    if ([self importRoms] > 0 && !h89session_is_running(_session)) [self start];
    [self updateStatus];
}

/* ---- status ------------------------------------------------------------------------ */

- (void)say:(NSString *)message
{
    _message = message;
    const NSUInteger serial = ++_messageSerial;
    [self updateStatus];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (serial != self->_messageSerial) return;
        self->_message = nil;
        [self updateStatus];
    });
}

- (void)updateStatus
{
    if (!_session) return;
    NSString *line;
    if (!h89session_is_running(_session)) {
        char what[512];
        line = h89session_missing_roms(_session, what, sizeof what) > 0
                   ? @"Off — ROMs needed (Machine > Import System ROMs…)" : @"Off";
        _linkLight.on = NO;
    } else {
        const int link = h89session_card_link(_session);
        _linkLight.on = link == 2;
        line = [NSString stringWithFormat:@"%@%@", link < 0 ? @"No FujiNet card" : str(h89session_card_link_text(link)),
                h89session_warp(_session) ? @"  ·  WARP" : @""];
    }
    if (_message) line = _message;
    _statusText.stringValue = line;
}

/* the drive light and the visual bell, four times a second */
- (void)fastTick
{
    if (!_session) return;
    BOOL busy = NO;
    const int n = h89session_drive_count(_session);
    for (int i = 0; i < n && !busy; i++) {
        h89_drive_info d;
        if (h89session_drive_info_at(_session, i, &d) == 0 && d.active) busy = YES;
    }
    _diskLight.on = busy;
    if (h89session_take_visual_bells(_session) > 0 && h89session_get_int(_session, "sound.visual_bell", 0))
        [_display flash];
}

/* ---- the view ---------------------------------------------------------------------- */

- (void)applyView
{
    const BOOL tv = h89session_get_int(_session, "view.aspect", 0) == 0;
    const BOOL smooth = h89session_get_int(_session, "view.smooth", 1) != 0;
    [_display setTvAspect:tv];
    [_display setSmooth:smooth];
    [_aspectItem setState:tv ? NSControlStateValueOn : NSControlStateValueOff];
    [_smoothItem setState:smooth ? NSControlStateValueOn : NSControlStateValueOff];
}

- (void)viewSettingsChanged:(NSNotification *)note
{
    (void)note;
    [self applyView];
}

- (void)toggleAspect:(id)sender
{
    (void)sender;
    h89session_set_int(_session, "view.aspect", h89session_get_int(_session, "view.aspect", 0) == 0 ? 1 : 0);
    [self applyView];
}

- (void)toggleSmooth:(id)sender
{
    (void)sender;
    h89session_set_int(_session, "view.smooth", h89session_get_int(_session, "view.smooth", 1) ? 0 : 1);
    [self applyView];
}

- (void)toggleFullScreen:(id)sender
{
    (void)sender;
    [_window toggleFullScreen:nil];
}

- (void)toggleKeys:(id)sender
{
    (void)sender;
    if (!_keys) _keys = [[H89H19KeysWindow alloc] initWithSession:_session];
    [_keys toggle];
}

- (H89DebuggerWindow *)debugger
{
    if (!_debugger) _debugger = [[H89DebuggerWindow alloc] initWithSession:_session];
    return _debugger;
}

- (void)toggleDebugger:(id)sender
{
    (void)sender;
    [[self debugger] toggleDebugger];
}

/* ---- menu --------------------------------------------------------------------------- */

- (NSMenuItem *)item:(NSMenu *)menu title:(NSString *)title action:(SEL)sel key:(NSString *)key
{
    NSMenuItem *it = [menu addItemWithTitle:title action:sel keyEquivalent:key];
    [it setTarget:self];
    return it;
}

- (void)buildMenu
{
    NSMenu *bar = [[NSMenu alloc] init];

    NSMenuItem *appItem = [[NSMenuItem alloc] init];
    NSMenu *appMenu = [[NSMenu alloc] init];
    [self item:appMenu title:@"About " APP_TITLE action:@selector(showAbout:) key:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [self item:appMenu title:@"Settings…" action:@selector(showSettings:) key:@","];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Hide " APP_TITLE action:@selector(hide:) keyEquivalent:@"h"];
    NSMenuItem *others = [appMenu addItemWithTitle:@"Hide Others"
                                            action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    [others setKeyEquivalentModifierMask:NSEventModifierFlagOption | NSEventModifierFlagCommand];
    [appMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit " APP_TITLE action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [bar addItem:appItem];

    NSMenuItem *machineItem = [[NSMenuItem alloc] init];
    NSMenu *machine = [[NSMenu alloc] initWithTitle:@"Machine"];
    [self item:machine title:@"Reset (SHIFT+RESET)" action:@selector(resetMachine:) key:@"r"];
    NSMenuItem *pc = [self item:machine title:@"Power Cycle" action:@selector(powerCycle:) key:@"r"];
    [pc setKeyEquivalentModifierMask:NSEventModifierFlagShift | NSEventModifierFlagCommand];
    _warpItem = [self item:machine title:@"Warp Speed" action:@selector(toggleWarp:) key:@""];
    [machine addItem:[NSMenuItem separatorItem]];
    [self item:machine title:@"Import System ROMs…" action:@selector(importRoms:) key:@""];
    [machineItem setSubmenu:machine];
    [bar addItem:machineItem];

    NSMenuItem *drivesItem = [[NSMenuItem alloc] init];
    _drivesMenu = [[NSMenu alloc] initWithTitle:@"Drives"];
    _drivesMenu.delegate = self;
    [self menuNeedsUpdate:_drivesMenu];
    [drivesItem setSubmenu:_drivesMenu];
    [bar addItem:drivesItem];

    NSMenuItem *editItem = [[NSMenuItem alloc] init];
    NSMenu *edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    [self item:edit title:@"Copy Screen Text" action:@selector(copyScreenText:) key:@"c"];
    [self item:edit title:@"Paste" action:@selector(pasteToH19:) key:@"v"];
    [editItem setSubmenu:edit];
    [bar addItem:editItem];

    NSMenuItem *viewItem = [[NSMenuItem alloc] init];
    NSMenu *view = [[NSMenu alloc] initWithTitle:@"View"];
    /* F9, F11 and F12 are the main window's (see H89ContentView): a menu
     * equivalent would take them from the H19 Keys and debugger windows. */
    [self item:view title:@"H19 Keys (F9)" action:@selector(toggleKeys:) key:@"k"];
    [self item:view title:@"Debugger (F12)" action:@selector(toggleDebugger:) key:@"d"];
    [view addItem:[NSMenuItem separatorItem]];
    _aspectItem = [self item:view title:@"TV Aspect" action:@selector(toggleAspect:) key:@""];
    _smoothItem = [self item:view title:@"Smooth Scaling" action:@selector(toggleSmooth:) key:@""];
    NSMenuItem *fs = [self item:view title:@"Enter Full Screen" action:@selector(toggleFullScreen:) key:@"f"];
    [fs setKeyEquivalentModifierMask:NSEventModifierFlagControl | NSEventModifierFlagCommand];
    [viewItem setSubmenu:view];
    [bar addItem:viewItem];

    NSMenuItem *fujiItem = [[NSMenuItem alloc] init];
    NSMenu *fuji = [[NSMenu alloc] initWithTitle:@"FujiNet"];
    [self item:fuji title:@"Configuration" action:@selector(openWebUI:) key:@""];
    [self item:fuji title:@"Console Log" action:@selector(showFujiNetLog:) key:@""];
    [self item:fuji title:@"Restart FujiNet" action:@selector(restartFujiNet:) key:@""];
    [fujiItem setSubmenu:fuji];
    [bar addItem:fujiItem];

    /* Window and Help: AppKit fills these in (the window list, the Help
     * search field) once they are registered as such. */
    NSMenuItem *windowItem = [[NSMenuItem alloc] init];
    NSMenu *windows = [[NSMenu alloc] initWithTitle:@"Window"];
    [windows addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [windows addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
    [windows addItem:[NSMenuItem separatorItem]];
    [windows addItemWithTitle:@"Bring All to Front" action:@selector(arrangeInFront:) keyEquivalent:@""];
    [windowItem setSubmenu:windows];
    [bar addItem:windowItem];

    NSMenuItem *helpItem = [[NSMenuItem alloc] init];
    NSMenu *help = [[NSMenu alloc] initWithTitle:@"Help"];
    [self item:help title:@"About " APP_TITLE action:@selector(showAbout:) key:@""];
    [helpItem setSubmenu:help];
    [bar addItem:helpItem];

    [NSApp setMainMenu:bar];
    [NSApp setWindowsMenu:windows];
    [NSApp setHelpMenu:help];
    [self applyView];
}

/* The Drives menu, rebuilt as it opens: the running machine's drives. */
- (void)menuNeedsUpdate:(NSMenu *)menu
{
    if (menu != _drivesMenu) return;
    [menu removeAllItems];
    int shown = 0;
    const int n = h89session_drive_count(_session);
    for (int i = 0; i < n; i++) {
        h89_drive_info d;
        if (h89session_drive_info_at(_session, i, &d) != 0 || !d.removable) continue;
        NSMenu *sub = [[NSMenu alloc] initWithTitle:str(d.label)];
        NSMenuItem *it = [self item:sub title:@"Insert…" action:@selector(driveAction:) key:@""];
        it.tag = DRIVE_TAG(d.id, DRIVE_INSERT);
        if (d.loaded) {
            it = [self item:sub title:@"Eject" action:@selector(driveAction:) key:@""];
            it.tag = DRIVE_TAG(d.id, DRIVE_EJECT);
            it = [self item:sub title:@"Write Protect" action:@selector(driveAction:) key:@""];
            it.tag = DRIVE_TAG(d.id, DRIVE_WP);
            it.state = d.write_protected ? NSControlStateValueOn : NSControlStateValueOff;
            if (d.read_only_format) it.action = nil;     /* greyed: TD0 cannot be written */
        }
        NSString *title = [NSString stringWithFormat:@"%@: %@", str(d.label),
                           d.loaded ? [str(d.path) lastPathComponent] : @"empty"];
        NSMenuItem *top = [menu addItemWithTitle:title action:nil keyEquivalent:@""];
        [top setSubmenu:sub];
        shown++;
    }
    if (!shown) [self item:menu title:@"No drives (fit a controller in Settings)" action:@selector(showSettings:) key:@""];
    [menu addItem:[NSMenuItem separatorItem]];
    [self item:menu title:@"Insert Disk…" action:@selector(insertDisk:) key:@"o"];
    [self item:menu title:@"New Blank Disk…" action:@selector(newBlankDisk:) key:@"n"];
    [self item:menu title:@"Open Disks Folder" action:@selector(openDisksFolder:) key:@""];
}

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    const SEL a = [item action];
    const BOOL running = _session && h89session_is_running(_session);
    if (a == @selector(toggleWarp:)) {
        [item setState:(running && h89session_warp(_session)) ? NSControlStateValueOn : NSControlStateValueOff];
        return running;
    }
    if (a == @selector(resetMachine:) || a == @selector(copyScreenText:) || a == @selector(pasteToH19:) ||
        a == @selector(insertDisk:) || a == @selector(driveAction:))
        return running;
    if (a == @selector(openWebUI:) || a == @selector(showFujiNetLog:))
        return _session && h89session_fujinet_running(_session);
    return YES;
}

/* ---- the machine ----------------------------------------------------------------------- */

- (void)resetMachine:(id)sender
{
    (void)sender;
    h89session_reset(_session);
}

- (void)powerCycle:(id)sender
{
    (void)sender;
    [self restart:NO];
}

- (void)toggleWarp:(id)sender
{
    (void)sender;
    const int on = !h89session_warp(_session);
    h89session_set_warp(_session, on);
    h89session_set_int(_session, "machine.warp", on);
    [self updateStatus];
}

- (void)showSettings:(id)sender
{
    (void)sender;
    if (!_settings) {
        __weak H89AppDelegate *weakSelf = self;
        _settings = [[H89SettingsWindow alloc] initWithSession:_session done:^(H89SettingsOutcome outcome) {
            H89AppDelegate *me = weakSelf;
            if (!me) return;
            if (outcome == H89SettingsRestart) [me restart:YES];
            else if (outcome == H89SettingsPowerCycle) [me restart:NO];
            [me applyView];
            [me updateStatus];
        }];
    }
    [_settings showOnPage:nil];
}

- (void)showAbout:(id)sender
{
    (void)sender;
    NSMutableParagraphStyle *para = [[NSMutableParagraphStyle alloc] init];
    para.alignment = NSTextAlignmentCenter;
    NSAttributedString *credits = [[NSAttributedString alloc]
        initWithString:@"A Heathkit H89 with a FujiNet card.\n\n"
                        "The emulator is VirtualH89 (BSD-3-Clause, Mark Garlanger and Douglas Miller), "
                        "adapted, with the FujiNet firmware (GPL-3.0-or-later) as the card's ESP32. "
                        "The Heath ROMs are not included: they are imported from MAME's sets.\n\n"
                        "Licensed under the GNU General Public License, version 3 or later."
            attributes:@{ NSFontAttributeName: [NSFont systemFontOfSize:NSFont.smallSystemFontSize],
                          NSForegroundColorAttributeName: NSColor.labelColor,
                          NSParagraphStyleAttributeName: para }];
    [NSApp orderFrontStandardAboutPanelWithOptions:@{
        NSAboutPanelOptionApplicationName: APP_TITLE,
        NSAboutPanelOptionApplicationVersion: @H89_VERSION_STRING,
        NSAboutPanelOptionVersion: @"",
        NSAboutPanelOptionCredits: credits,
    }];
}

/* ---- disks ------------------------------------------------------------------------------- */

- (NSString *)pickDisk:(NSString *)title soft:(int)soft
{
    NSMutableArray<NSString *> *exts = [NSMutableArray array];
    if (soft != 1)
        for (size_t i = 0; i < sizeof kH17Exts / sizeof kH17Exts[0]; i++) [exts addObject:kH17Exts[i]];
    if (soft != 0)
        for (size_t i = 0; i < sizeof kSoftExts / sizeof kSoftExts[0]; i++) [exts addObject:kSoftExts[i]];
    NSOpenPanel *p = [NSOpenPanel openPanel];
    p.title = title;
    p.allowedContentTypes = typesForExtensions(exts);
    const char *dir = h89session_get_str(_session, "drives.last_dir", "");
    p.directoryURL = [NSURL fileURLWithPath:str(dir && dir[0] ? dir : h89session_disks_path(_session))
                                isDirectory:YES];
    if ([p runModal] != NSModalResponseOK) return nil;
    return [[p URL] path];
}

- (void)rememberFolder:(NSString *)path
{
    h89session_set_str(_session, "drives.last_dir", [[path stringByDeletingLastPathComponent] fileSystemRepresentation]);
}

- (void)insert:(NSString *)path into:(int)driveId
{
    char err[512];
    if (h89session_drive_insert(_session, driveId, [path fileSystemRepresentation], 0, err, sizeof err) != 0) {
        [self alert:@"Cannot Insert the Disk" text:str(err)];
        return;
    }
    [self rememberFolder:path];
}

- (void)driveAction:(NSMenuItem *)sender
{
    const int driveId = (int)sender.tag / 4, action = (int)sender.tag % 4;
    h89_drive_info d;
    char err[512];
    if (h89session_drive_info(_session, driveId, &d) != 0) return;
    switch (action) {
    case DRIVE_INSERT: {
        NSString *path = [self pickDisk:[NSString stringWithFormat:@"Insert a Disk in %@", str(d.label)]
                                   soft:(H89_DRIVE_CTRL(driveId) == H89_CTRL_H17 ? 0 : 1)];
        if (path) [self insert:path into:driveId];
        break;
    }
    case DRIVE_EJECT:
        if (h89session_drive_eject(_session, driveId, err, sizeof err) != 0)
            [self alert:@"Cannot Eject the Disk" text:str(err)];
        break;
    case DRIVE_WP:
        h89session_drive_set_write_protect(_session, driveId, !d.write_protected);
        break;
    default:
        break;
    }
}

- (void)insertDisk:(id)sender
{
    (void)sender;
    NSString *path = [self pickDisk:@"Insert Disk" soft:-1];
    if (path) [self loadMedia:path];
}

- (void)openDisksFolder:(id)sender
{
    (void)sender;
    NSString *dir = str(h89session_disks_path(_session));
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES
                                               attributes:nil error:NULL];
    [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:dir isDirectory:YES]];
}

/* A dropped file, a file from Finder or the command line, a new disk:
 * ROMs are imported, a disk goes in a drive (asking which when every one
 * that takes it is full), anything else to FujiNet's SD folder. */
- (void)loadMedia:(NSString *)path
{
    const char *fs = [path fileSystemRepresentation];
    const int kind = h89session_media_kind(fs);
    char msg[1024];
    if (kind == H89_MEDIA_ROMS) {
        h89session_import_roms(_session, fs, msg, sizeof msg);
        [self alert:@"Import ROMs" text:str(msg)];
        if (!h89session_is_running(_session)) [self start];
        return;
    }
    if (!h89session_is_running(_session) && kind != H89_MEDIA_UNKNOWN) {
        [self alert:APP_TITLE text:@"The H89 is off: import its ROMs first."];
        return;
    }
    int ids[16], nids = 0;
    const int rc = h89session_import_media(_session, fs, msg, sizeof msg, ids, 16, &nids);
    if (rc < 0) {
        [self alert:@"Cannot Use the File" text:str(msg)];
        return;
    }
    if (rc == 1) {
        /* every drive that takes it is full: ask which */
        NSAlert *a = [[NSAlert alloc] init];
        a.messageText = @"Which drive?";
        a.informativeText = [NSString stringWithFormat:@"Every drive that takes %@ holds a disk. "
                             "The one chosen is ejected first.", [path lastPathComponent]];
        NSPopUpButton *pop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 280, 26) pullsDown:NO];
        for (int i = 0; i < nids && i < 16; i++) {
            h89_drive_info d;
            if (h89session_drive_info(_session, ids[i], &d) != 0) continue;
            [pop addItemWithTitle:str(d.label)];
            [[pop lastItem] setTag:ids[i]];
        }
        a.accessoryView = pop;
        [a addButtonWithTitle:@"Insert"];
        [a addButtonWithTitle:@"Cancel"];
        if ([a runModal] != NSAlertFirstButtonReturn || ![pop selectedItem]) return;
        [self insert:path into:(int)[[pop selectedItem] tag]];
        return;
    }
    if (kind != H89_MEDIA_UNKNOWN) [self rememberFolder:path];
    [self say:str(msg)];
}

- (void)newBlankDisk:(id)sender
{
    (void)sender;
    static NSString *const kinds[H89_BLANK_KIND_COUNT] = {
        @"H17: sector image (.h8d) — format it with HDOS INIT",
        @"H17: H17Disk (.h17)",
        @"H17: raw tracks (.h17raw)",
        @"Soft-sectored: ImageDisk (.imd) — format it in the H89",
    };
    static NSString *const exts[H89_BLANK_KIND_COUNT] = { @"h8d", @"h17", @"h17raw", @"imd" };

    NSView *acc = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 62)];
    NSPopUpButton *kind = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 34, 400, 26) pullsDown:NO];
    for (int i = 0; i < H89_BLANK_KIND_COUNT; i++) [kind addItemWithTitle:kinds[i]];
    int k = h89session_get_int(_session, "blank.kind", 0);
    if (k < 0 || k >= H89_BLANK_KIND_COUNT) k = 0;
    [kind selectItemAtIndex:k];
    [acc addSubview:kind];
    NSTextField *sl = [NSTextField labelWithString:@"Sides"];
    sl.frame = NSMakeRect(0, 4, 44, 18);
    [acc addSubview:sl];
    NSPopUpButton *sides = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(46, 0, 70, 26) pullsDown:NO];
    [sides addItemsWithTitles:@[@"1", @"2"]];
    [sides selectItemAtIndex:(h89session_get_int(_session, "blank.sides", 1) == 2 ? 1 : 0)];
    [acc addSubview:sides];
    NSTextField *tl = [NSTextField labelWithString:@"Tracks"];
    tl.frame = NSMakeRect(140, 4, 50, 18);
    [acc addSubview:tl];
    NSPopUpButton *tracks = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(192, 0, 70, 26) pullsDown:NO];
    [tracks addItemsWithTitles:@[@"40", @"80"]];
    [tracks selectItemAtIndex:(h89session_get_int(_session, "blank.tracks", 40) == 80 ? 1 : 0)];
    [acc addSubview:tracks];

    NSAlert *a = [[NSAlert alloc] init];
    a.messageText = @"New Blank Disk";
    a.informativeText = @"The kind of disk, then where to save it. An ImageDisk's geometry is whatever the H89 "
                         "formats on it.";
    a.accessoryView = acc;
    [a addButtonWithTitle:@"Create…"];
    [a addButtonWithTitle:@"Cancel"];
    if ([a runModal] != NSAlertFirstButtonReturn) return;

    k = (int)[kind indexOfSelectedItem];
    const int s = [sides indexOfSelectedItem] == 1 ? 2 : 1;
    const int t = [tracks indexOfSelectedItem] == 1 ? 80 : 40;
    h89session_set_int(_session, "blank.kind", k);
    h89session_set_int(_session, "blank.sides", s);
    h89session_set_int(_session, "blank.tracks", t);

    NSString *dir = str(h89session_disks_path(_session));
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES
                                               attributes:nil error:NULL];
    NSSavePanel *p = [NSSavePanel savePanel];
    p.title = @"Save the New Disk";
    p.directoryURL = [NSURL fileURLWithPath:dir isDirectory:YES];
    p.nameFieldStringValue = [@"blank" stringByAppendingPathExtension:exts[k]];
    if ([p runModal] != NSModalResponseOK) return;
    NSString *path = [[p URL] path];
    char err[512];
    if (h89session_new_blank_disk(_session, [path fileSystemRepresentation], k, s, t, err, sizeof err) != 0) {
        [self alert:@"Cannot Create the Disk" text:str(err)];
        return;
    }
    [self loadMedia:path];
}

/* ---- text in and out ------------------------------------------------------------------------ */

- (void)copyScreenText:(id)sender
{
    (void)sender;
    static char buf[8192];
    if (h89session_screen_text(_session, buf, sizeof buf, 1) == 0) return;
    NSPasteboard *pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:str(buf) forType:NSPasteboardTypeString];
    [self say:@"The screen's text is on the clipboard"];
}

- (void)pasteToH19:(id)sender
{
    (void)sender;
    NSString *text = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (text) h89session_paste(_session, [text UTF8String]);
}

/* ---- FujiNet ---------------------------------------------------------------------------------- */

- (void)openWebUI:(id)sender
{
    (void)sender;
    if (!h89session_fujinet_running(_session)) {
        [self alert:@"FujiNet is not running" text:nil];
        return;
    }
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:str(h89session_fujinet_webui_url(_session))]];
}

- (void)restartFujiNet:(id)sender
{
    (void)sender;
    if (h89session_fujinet_restart(_session) != 0)
        [self alert:@"Cannot Restart FujiNet" text:[self lastError]];
    [self updateStatus];
}

- (void)refreshLog:(NSTimer *)timer
{
    (void)timer;
    static char buf[128 * 1024];
    const int n = h89session_fujinet_copy_log(_session, buf, sizeof buf);
    NSScrollView *scroll = (NSScrollView *)_logView.enclosingScrollView;
    const BOOL atEnd = !scroll || (NSMaxY(scroll.contentView.documentVisibleRect) >=
                                   NSMaxY(((NSView *)scroll.documentView).frame) - 4.0);
    NSString *text = n > 0 ? [NSString stringWithUTF8String:buf] : nil;
    [_logView setString:(text ?: @"(no FujiNet output yet)")];
    if (atEnd) [_logView scrollRangeToVisible:NSMakeRange(_logView.string.length, 0)];
}

- (void)showFujiNetLog:(id)sender
{
    (void)sender;
    if (!_logWindow) {
        NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 860, 600)];
        scroll.hasVerticalScroller = YES;
        scroll.autohidesScrollers = NO;
        _logView = [[NSTextView alloc] initWithFrame:scroll.bounds];
        _logView.editable = NO;
        _logView.richText = NO;
        _logView.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
        _logView.autoresizingMask = NSViewWidthSizable;
        scroll.documentView = _logView;
        _logWindow = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 860, 600)
                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                        backing:NSBackingStoreBuffered
                          defer:NO];
        _logWindow.title = @"FujiNet Console Log";
        _logWindow.releasedWhenClosed = NO;
        _logWindow.delegate = self;
        _logWindow.contentView = scroll;
        [_logWindow center];
    }
    if (!_logTimer)
        _logTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:self
                                                   selector:@selector(refreshLog:) userInfo:nil repeats:YES];
    [self refreshLog:nil];
    [_logWindow makeKeyAndOrderFront:nil];
}

- (void)windowWillClose:(NSNotification *)note
{
    if (note.object == _logWindow) {
        [_logTimer invalidate];
        _logTimer = nil;
    }
}
@end
