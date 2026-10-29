/*
 * SettingsWindow -- the GNOME, KDE and Win32 Preferences' six pages on an
 * NSTabView. Rows are bound to fields of a local h89_config (or to settings
 * keys for the live view and sound options) through one table, so every
 * control is handled by the same few lines. The machine is checked as a
 * whole (h89_config_validate) on every change -- the line at the top says
 * whether it can be built, and OK is only enabled when it can -- and stored
 * on OK, with a power cycle if one is asked for.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#import "SettingsWindow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "h89config.h"
#include "h89roms.h"
#include "h89session.h"

NSString *const H89ViewSettingsChangedNotification = @"H89ViewSettingsChanged";

enum { K_COMBO, K_BIT, K_FLAG, K_NUMBER, K_SET_COMBO, K_SET_CHECK, K_MONITOR, K_BAUD, K_SW501 };
enum { G_NONE, G_H17, G_H37, G_MMS316, G_MMS320, G_SW501, G_FUJI };
enum { S_LIVE, S_VIEW, S_HOST };

#define NPAGES 6
#define WIN_W 760.0
#define WIN_H 680.0

static const char *const kPageKeys[NPAGES] = { "machine", "slots", "switches", "cpnet", "display", "fujinet" };
static const char *const kPageNames[NPAGES] = { "Machine", "Slots & Drives", "Switches", "CP/NET",
                                                "Display & Sound", "FujiNet" };

static const int kSpeedups[] = { 0, 2, 3, 4, 6, 8, 10, 16, 20, 40 };
static const char *const kSpeedupNames[] = { "None (2.048 MHz)", "2x", "3x", "4x", "6x", "8x", "10x", "16x", "20x",
                                             "40x", NULL };
static const int kPorts[] = { 0x78, 0x7c };
static const char *const kPortNames[] = { "170Q (0x78, JP1A)", "174Q (0x7C, JP1B)", NULL };
static const int kInts[] = { 0, 3, 4, 5 };
static const char *const kIntNames[] = { "None", "INT 3 (JP2A)", "INT 4 (JP2B)", "INT 5 (JP2C)", NULL };

static NSString *S(const char *s)
{
    NSString *r = s ? [NSString stringWithUTF8String:s] : nil;
    return r ? r : @"";
}

/* Items straight into the menu: addItemWithTitle: on the button drops an
 * item whose title is already there, and some SW501 options share one. */
static void add_item(NSPopUpButton *p, NSString *title)
{
    [[p menu] addItem:[[NSMenuItem alloc] initWithTitle:title action:NULL keyEquivalent:@""]];
}

static void fill_names(NSPopUpButton *p, const char *(*names)(int), const char *const *strs)
{
    for (int i = 0; names ? names(i) != NULL : strs[i] != NULL; i++)
        add_item(p, S(names ? names(i) : strs[i]));
}

/* Pages laid out from the top, as the other frontends' are. */
@interface H89FlippedView : NSView
@end

@implementation H89FlippedView
- (BOOL)isFlipped { return YES; }
@end

/* One bound control. */
@interface H89Row : NSObject
@property (nonatomic, strong) NSControl *control;
@property (nonatomic, strong) NSTextField *label;
@property (nonatomic) int kind;
@property (nonatomic) int group;
@property (nonatomic) int *field;
@property (nonatomic) const int *values;
@property (nonatomic) int nvalues;
@property (nonatomic) uint8_t *byte;
@property (nonatomic) uint8_t mask;
@property (nonatomic) const char *key;
@property (nonatomic) int def;
@property (nonatomic) int setkind;
@property (nonatomic) int swField;
@end

@implementation H89Row
@end

@interface H89SettingsWindow () <NSTextFieldDelegate>
@end

@implementation H89SettingsWindow {
    h89session *_session;
    void (^_done)(H89SettingsOutcome outcome);
    h89_config _cfg, _orig;
    BOOL _hostChanged;
    BOOL _syncing;
    BOOL _sw501Pending;
    NSMutableArray<H89Row *> *_rows;
    NSMutableArray<NSView *> *_pages;
    NSTabView *_tabs;
    NSTextField *_status, *_roms, *_cpnetDir, *_externalAddr;
    NSButton *_ok;
    CGFloat _sw501Y;
    int _monitors[16], _nmon;
}

- (instancetype)initWithSession:(h89session *)session done:(void (^)(H89SettingsOutcome outcome))done
{
    NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, WIN_W, WIN_H)
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                backing:NSBackingStoreBuffered
                                                  defer:YES];
    self = [super initWithWindow:w];
    if (!self)
        return nil;
    _session = session;
    _done = [done copy];
    _rows = [NSMutableArray array];
    _pages = [NSMutableArray array];
    [w setTitle:@"Settings"];
    [w setReleasedWhenClosed:NO];
    [w setDelegate:self];
    return self;
}

/* ---- building rows ------------------------------------------------------------------- */

- (NSTextField *)label:(NSString *)text page:(NSView *)page x:(CGFloat)x y:(CGFloat)y w:(CGFloat)w
{
    NSTextField *l = [NSTextField labelWithString:text];
    [l setFrame:NSMakeRect(x, y + 3, w, 18)];
    [l setLineBreakMode:NSLineBreakByTruncatingTail];
    [page addSubview:l];
    return l;
}

- (H89Row *)addRow:(int)kind page:(NSView *)page label:(NSString *)label
                 x:(CGFloat)x y:(CGFloat)y lw:(CGFloat)lw cw:(CGFloat)cw
{
    H89Row *r = [[H89Row alloc] init];
    r.kind = kind;
    if (kind == K_BIT || kind == K_FLAG || kind == K_SET_CHECK) {
        NSButton *b = [NSButton checkboxWithTitle:label target:self action:@selector(rowChanged:)];
        [b setFrame:NSMakeRect(x, y, lw + cw, 22)];
        r.control = b;
    } else if (kind == K_NUMBER) {
        NSTextField *f = [[NSTextField alloc] initWithFrame:NSMakeRect(x + lw, y, cw, 22)];
        r.label = [self label:label page:page x:x y:y w:lw];
        [f setDelegate:self];
        r.control = f;
    } else {
        NSPopUpButton *p = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(x + lw, y - 1, cw, 25) pullsDown:NO];
        r.label = [self label:label page:page x:x y:y w:lw];
        [p setTarget:self];
        [p setAction:@selector(rowChanged:)];
        r.control = p;
    }
    [page addSubview:r.control];
    [_rows addObject:r];
    return r;
}

- (H89Row *)combo:(NSView *)page label:(NSString *)label x:(CGFloat)x y:(CGFloat)y lw:(CGFloat)lw cw:(CGFloat)cw
            field:(int *)field names:(const char *(*)(int))names strs:(const char *const *)strs
           values:(const int *)values count:(int)nvalues
{
    H89Row *r = [self addRow:K_COMBO page:page label:label x:x y:y lw:lw cw:cw];
    NSPopUpButton *p = (NSPopUpButton *)r.control;
    int sel = *field;
    r.field = field;
    r.values = values;
    r.nvalues = nvalues;
    fill_names(p, names, strs);
    if (values)
        for (int i = 0; i < nvalues; i++)
            if (values[i] == *field)
                sel = i;
    [p selectItemAtIndex:sel];
    return r;
}

- (H89Row *)bit:(NSView *)page label:(NSString *)label x:(CGFloat)x y:(CGFloat)y w:(CGFloat)w
           byte:(uint8_t *)byte mask:(uint8_t)mask
{
    H89Row *r = [self addRow:K_BIT page:page label:label x:x y:y lw:w cw:0];
    r.byte = byte;
    r.mask = mask;
    [(NSButton *)r.control setState:(*byte & mask) ? NSControlStateValueOn : NSControlStateValueOff];
    return r;
}

- (H89Row *)flag:(NSView *)page label:(NSString *)label x:(CGFloat)x y:(CGFloat)y w:(CGFloat)w field:(int *)field
{
    H89Row *r = [self addRow:K_FLAG page:page label:label x:x y:y lw:w cw:0];
    r.field = field;
    [(NSButton *)r.control setState:*field ? NSControlStateValueOn : NSControlStateValueOff];
    return r;
}

- (H89Row *)number:(NSView *)page label:(NSString *)label x:(CGFloat)x y:(CGFloat)y lw:(CGFloat)lw field:(int *)field
{
    H89Row *r = [self addRow:K_NUMBER page:page label:label x:x y:y lw:lw cw:80];
    r.field = field;
    _syncing = YES;
    [(NSTextField *)r.control setStringValue:[NSString stringWithFormat:@"%d", *field]];
    _syncing = NO;
    return r;
}

- (H89Row *)setCheck:(NSView *)page label:(NSString *)label x:(CGFloat)x y:(CGFloat)y w:(CGFloat)w
                 key:(const char *)key def:(int)def kind:(int)sk
{
    H89Row *r = [self addRow:K_SET_CHECK page:page label:label x:x y:y lw:w cw:0];
    r.key = key;
    r.def = def;
    r.setkind = sk;
    [(NSButton *)r.control setState:h89session_get_int(_session, key, def) ? NSControlStateValueOn
                                                                           : NSControlStateValueOff];
    return r;
}

- (H89Row *)setCombo:(NSView *)page label:(NSString *)label x:(CGFloat)x y:(CGFloat)y lw:(CGFloat)lw cw:(CGFloat)cw
                 key:(const char *)key def:(int)def strs:(const char *const *)strs kind:(int)sk
{
    H89Row *r = [self addRow:K_SET_COMBO page:page label:label x:x y:y lw:lw cw:cw];
    r.key = key;
    r.def = def;
    r.setkind = sk;
    fill_names((NSPopUpButton *)r.control, NULL, strs);
    [(NSPopUpButton *)r.control selectItemAtIndex:h89session_get_int(_session, key, def)];
    return r;
}

- (void)setGroup:(int)group from:(NSUInteger)start
{
    for (NSUInteger i = start; i < [_rows count]; i++)
        _rows[i].group = group;
}

/* ---- the state of the whole ------------------------------------------------------------ */

- (BOOL)hasCard:(int)card
{
    for (int i = 0; i < H89_SLOTS; i++)
        if (_cfg.slot[i] == card)
            return YES;
    return NO;
}

static void show_row(H89Row *r, BOOL on)
{
    [r.control setHidden:!on];
    [r.label setHidden:!on];
}

- (void)changed
{
    char why[256];
    const int ok = h89_config_validate(&_cfg, why, sizeof why);
    if (!ok)
        [_status setStringValue:[NSString stringWithFormat:@"This machine cannot be built: %@", S(why)]];
    else if (memcmp(&_cfg, &_orig, sizeof _cfg) == 0)
        [_status setStringValue:@"Configuration: as it is now"];
    else
        [_status setStringValue:@"Configuration: changed — applied by a power cycle"];
    [_status setTextColor:ok ? [NSColor labelColor] : [NSColor systemRedColor]];
    [_ok setEnabled:ok != 0];
    for (H89Row *r in _rows) {
        switch (r.group) {
        case G_H17: show_row(r, [self hasCard:H89_CARD_H17]); break;
        case G_H37: show_row(r, [self hasCard:H89_CARD_H37]); break;
        case G_MMS316: show_row(r, [self hasCard:H89_CARD_MMS77316]); break;
        case G_MMS320: show_row(r, [self hasCard:H89_CARD_MMS77320]); break;
        case G_FUJI: [r.control setEnabled:[self hasCard:H89_CARD_FUJINET]]; break;
        default: break;
        }
    }
    /* after the action that got here: it may be one of the rows rebuilt */
    if (!_sw501Pending) {
        _sw501Pending = YES;
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_sw501Pending = NO;
            [self rebuildSW501];
        });
    }
}

/* SW501's fields are the monitor's; with "from the slots" on, the device
 * fields show what the slots make them and cannot be changed. */
- (void)rebuildSW501
{
    const char *mon = _cfg.monitor;
    const uint8_t eff = h89_config_effective_sw501(&_cfg);
    const uint8_t automask = _cfg.sw501_auto ? h89_sw501_auto_mask(mon) : 0;
    const int n = h89_sw501_field_count(mon);
    NSMutableArray<H89Row *> *keep = [NSMutableArray array];

    if ([_pages count] < NPAGES)
        return;
    for (H89Row *r in _rows) {
        if (r.group == G_SW501) {
            [r.control removeFromSuperview];
            [r.label removeFromSuperview];
            continue;
        }
        [keep addObject:r];
    }
    [_rows setArray:keep];
    _syncing = YES;
    for (int f = 0; f < n; f++) {
        H89Row *r = [self addRow:K_SW501 page:_pages[2] label:S(h89_sw501_field_name(mon, f))
                               x:16 y:_sw501Y + f * 28 lw:220 cw:260];
        NSPopUpButton *p = (NSPopUpButton *)r.control;
        const uint8_t mask = h89_sw501_field_mask(mon, f);
        r.group = G_SW501;
        r.swField = f;
        for (int o = 0; o < h89_sw501_option_count(mon, f); o++) {
            add_item(p, S(h89_sw501_option_name(mon, f, o)));
            if ((uint8_t)(eff & mask) == h89_sw501_option_value(mon, f, o))
                [p selectItemAtIndex:o];
        }
        [p setEnabled:(mask & automask) == 0];
    }
    _syncing = NO;
}

- (void)fillMonitors:(H89Row *)r
{
    NSPopUpButton *p = (NSPopUpButton *)r.control;
    int sel = -1;
    [p removeAllItems];
    _nmon = h89session_monitors(_session, _monitors, 15);
    for (int i = 0; i < _nmon; i++) {
        const h89_rom_info *info = h89_rom_at(_monitors[i]);
        add_item(p, S(info ? info->desc : "?"));
        if (info && !strcmp(info->file, _cfg.monitor))
            sel = i;
    }
    if (sel < 0 && _cfg.monitor[0]) {
        const int idx = h89_rom_find_file(_cfg.monitor);
        const h89_rom_info *info = idx >= 0 ? h89_rom_at(idx) : NULL;
        add_item(p, [NSString stringWithFormat:@"%@ (not imported)", S(info ? info->desc : _cfg.monitor)]);
        _monitors[_nmon] = idx;
        sel = _nmon++;
    }
    [p selectItemAtIndex:sel];
}

- (void)refreshRomsText
{
    char what[512];
    if (h89session_missing_roms(_session, what, sizeof what) > 0)
        [_roms setStringValue:[NSString stringWithFormat:@"Still needed: %@", S(what)]];
    else
        [_roms setStringValue:@"Everything this machine needs is imported"];
}

/* ---- handlers ------------------------------------------------------------------------------- */

- (H89Row *)rowFor:(id)control
{
    for (H89Row *r in _rows)
        if (r.control == control)
            return r;
    return nil;
}

- (void)onRow:(H89Row *)r
{
    NSInteger sel;
    if (_syncing || !r)
        return;
    switch (r.kind) {
    case K_COMBO:
        sel = [(NSPopUpButton *)r.control indexOfSelectedItem];
        if (sel < 0)
            return;
        if (r.values)
            *r.field = sel < r.nvalues ? r.values[sel] : *r.field;
        else
            *r.field = (int)sel;
        [self changed];
        return;
    case K_MONITOR:
        sel = [(NSPopUpButton *)r.control indexOfSelectedItem];
        if (sel >= 0 && sel < _nmon && _monitors[sel] >= 0)
            snprintf(_cfg.monitor, sizeof _cfg.monitor, "%s", h89_rom_at(_monitors[sel])->file);
        [self changed];
        return;
    case K_BAUD:
        sel = [(NSPopUpButton *)r.control indexOfSelectedItem];
        if (sel >= 0)
            _cfg.sw401 = (uint8_t)((_cfg.sw401 & 0xf0) | (sel + 1));
        [self changed];
        return;
    case K_SW501: {
        const uint8_t mask = h89_sw501_field_mask(_cfg.monitor, r.swField);
        sel = [(NSPopUpButton *)r.control indexOfSelectedItem];
        if (sel >= 0)
            _cfg.sw501 = (uint8_t)((_cfg.sw501 & (uint8_t)~mask) |
                                   h89_sw501_option_value(_cfg.monitor, r.swField, (int)sel));
        [self changed];
        return;
    }
    case K_BIT:
        if ([(NSButton *)r.control state] == NSControlStateValueOn)
            *r.byte |= r.mask;
        else
            *r.byte &= (uint8_t)~r.mask;
        [self changed];
        return;
    case K_FLAG:
        *r.field = [(NSButton *)r.control state] == NSControlStateValueOn;
        [self changed];
        return;
    case K_NUMBER:
        *r.field = [[(NSTextField *)r.control stringValue] intValue];
        [self changed];
        return;
    case K_SET_CHECK:
    case K_SET_COMBO: {
        const int v = r.kind == K_SET_CHECK ? [(NSButton *)r.control state] == NSControlStateValueOn
                                            : (int)[(NSPopUpButton *)r.control indexOfSelectedItem];
        if (v < 0 || h89session_get_int(_session, r.key, r.def) == v)
            return;
        h89session_set_int(_session, r.key, v);
        if (!strcmp(r.key, "fujinet_external"))
            [_externalAddr setEnabled:v != 0];
        if (r.setkind == S_HOST) {
            _hostChanged = YES;
        } else if (r.setkind == S_VIEW) {
            h89session_apply_view(_session);
            [[NSNotificationCenter defaultCenter] postNotificationName:H89ViewSettingsChangedNotification
                                                                object:self];
        }
        return;
    }
    default:
        return;
    }
}

- (void)rowChanged:(id)sender
{
    [self onRow:[self rowFor:sender]];
}

- (void)controlTextDidChange:(NSNotification *)note
{
    [self onRow:[self rowFor:[note object]]];
}

- (void)volumeChanged:(id)sender
{
    h89session_set_volume(_session, [(NSSlider *)sender intValue]);
}

- (void)importRoms:(id)sender
{
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    (void)sender;
    [panel setAllowsMultipleSelection:YES];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setTitle:@"Import System ROMs"];
    [panel setMessage:@"MAME's h89.zip and heath_tlb.zip, or loose ROM images"];
    [panel beginSheetModalForWindow:[self window] completionHandler:^(NSModalResponse response) {
        NSMutableString *log = [NSMutableString string];
        int total = 0;
        if (response != NSModalResponseOK)
            return;
        for (NSURL *url in [panel URLs]) {
            char msg[1024];
            const int n = h89session_import_roms(self->_session, [url fileSystemRepresentation], msg, sizeof msg);
            if (n > 0)
                total += n;
            if ([log length])
                [log appendString:@"\n\n"];
            [log appendString:S(msg)];
        }
        for (H89Row *r in self->_rows)
            if (r.kind == K_MONITOR)
                [self fillMonitors:r];
        [self refreshRomsText];
        [self changed];
        /* once the panel's sheet is gone */
        dispatch_async(dispatch_get_main_queue(), ^{
            NSAlert *alert = [[NSAlert alloc] init];
            [alert setMessageText:total > 0 ? @"ROMs Imported" : @"Nothing Imported"];
            [alert setInformativeText:log];
            [alert setAlertStyle:total > 0 ? NSAlertStyleInformational : NSAlertStyleWarning];
            [alert beginSheetModalForWindow:[self window] completionHandler:nil];
        });
    }];
}

- (void)chooseCpnetFolder:(id)sender
{
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    (void)sender;
    [panel setCanChooseFiles:NO];
    [panel setCanChooseDirectories:YES];
    [panel setCanCreateDirectories:YES];
    [panel setAllowsMultipleSelection:NO];
    [panel setTitle:@"The CP/NET Host Folder"];
    [panel setPrompt:@"Choose"];
    [panel beginSheetModalForWindow:[self window] completionHandler:^(NSModalResponse response) {
        const char *path;
        if (response != NSModalResponseOK || ![panel URL])
            return;
        path = [[panel URL] fileSystemRepresentation];
        snprintf(self->_cfg.cpnet_root, sizeof self->_cfg.cpnet_root, "%s", path);
        [self->_cpnetDir setStringValue:S(path)];
        [self changed];
    }];
}

- (void)openWebUI:(id)sender
{
    NSURL *url;
    (void)sender;
    if (!h89session_fujinet_running(_session))
        return;
    url = [NSURL URLWithString:S(h89session_fujinet_webui_url(_session))];
    if (url)
        [[NSWorkspace sharedWorkspace] openURL:url];
}

/* ---- closing ---------------------------------------------------------------------------------- */

- (void)finish:(BOOL)accepted
{
    H89SettingsOutcome outcome = H89SettingsNothing;
    const BOOL changedCfg = memcmp(&_cfg, &_orig, sizeof _cfg) != 0;
    const BOOL running = h89session_is_running(_session) != 0;

    if (accepted && changedCfg) {
        const BOOL needs = running && h89session_config_needs_power_cycle(_session, &_cfg);
        h89session_set_config(_session, &_cfg);
        if (!running) {
            outcome = H89SettingsPowerCycle;    /* the delegate starts it */
        } else if (needs) {
            NSAlert *alert = [[NSAlert alloc] init];
            [alert setMessageText:@"Power Cycle the H89?"];
            [alert setInformativeText:@"The new configuration takes effect when the H89 is switched off and on. "
                                      @"The disks are written back first, and go back in their drives."];
            [alert addButtonWithTitle:@"Power Cycle"];
            [alert addButtonWithTitle:@"Not Now"];
            if ([alert runModal] == NSAlertFirstButtonReturn)
                outcome = H89SettingsPowerCycle;
        }
    }
    h89session_settings_flush(_session);
    /* FujiNet or audio switched: a restart, which builds the new
     * configuration too */
    if (_hostChanged)
        outcome = H89SettingsRestart;
    _hostChanged = NO;
    [[self window] orderOut:nil];
    if (_done)
        _done(outcome);
}

- (void)okPressed:(id)sender
{
    char why[256];
    (void)sender;
    if (!h89_config_validate(&_cfg, why, sizeof why)) {
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:@"This Machine Cannot Be Built"];
        [alert setInformativeText:S(why)];
        [alert setAlertStyle:NSAlertStyleWarning];
        [alert beginSheetModalForWindow:[self window] completionHandler:nil];
        return;
    }
    if (h89session_get_int(_session, "fujinet_external", 0)) {
        NSString *addr = [[_externalAddr stringValue]
            stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        char hostName[64];
        int port;
        if (h89session_parse_hostport([addr UTF8String], hostName, sizeof hostName, &port) != 0) {
            NSAlert *alert = [[NSAlert alloc] init];
            [alert setMessageText:@"External FujiNet"];
            [alert setInformativeText:[NSString stringWithFormat:@"\"%@\" is not an address:port. "
                                                                 @"Example: 127.0.0.1:11512", addr]];
            [alert setAlertStyle:NSAlertStyleWarning];
            [alert beginSheetModalForWindow:[self window] completionHandler:nil];
            return;
        }
    }
    {
        NSString *addr = [[_externalAddr stringValue]
            stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (![addr isEqualToString:S(h89session_get_str(_session, "fujinet_external_addr", ""))]) {
            h89session_set_str(_session, "fujinet_external_addr", [addr UTF8String]);
            _hostChanged = YES;
        }
    }
    [self finish:YES];
}

- (void)cancelPressed:(id)sender
{
    (void)sender;
    [self finish:NO];
}

- (BOOL)windowShouldClose:(NSWindow *)sender
{
    (void)sender;
    [self finish:NO];
    return NO;
}

- (void)tabView:(NSTabView *)tabView didSelectTabViewItem:(NSTabViewItem *)tabViewItem
{
    (void)tabView;
    (void)tabViewItem;
    [self changed];
}

/* ---- the pages ----------------------------------------------------------------------------------- */

- (void)buildMachine:(NSView *)p
{
    H89Row *r;
    NSButton *btn;
    CGFloat y = 12;
    [self combo:p label:@"Model" x:16 y:y lw:160 cw:300 field:&_cfg.model names:h89_model_name strs:NULL
         values:NULL count:0];
    y += 30;
    [self combo:p label:@"Memory" x:16 y:y lw:160 cw:300 field:&_cfg.memory names:h89_memory_name strs:NULL
         values:NULL count:0];
    y += 30;
    [self combo:p label:@"Memory slot P503" x:16 y:y lw:160 cw:300 field:&_cfg.p503 names:h89_p503_name strs:NULL
         values:NULL count:0];
    y += 30;
    r = [self addRow:K_MONITOR page:p label:@"Monitor ROM" x:16 y:y lw:160 cw:420];
    [self fillMonitors:r];
    y += 30;
    [self combo:p label:@"Speed-up" x:16 y:y lw:160 cw:300 field:&_cfg.speedup names:NULL strs:kSpeedupNames
         values:kSpeedups count:10];
    y += 40;
    [self label:@"System ROMs (imported from MAME's h89.zip and heath_tlb.zip):" page:p x:16 y:y w:580];
    y += 24;
    _roms = [self label:@"" page:p x:16 y:y w:580];
    btn = [NSButton buttonWithTitle:@"Import…" target:self action:@selector(importRoms:)];
    [btn setFrame:NSMakeRect(600, y - 3, 100, 28)];
    [p addSubview:btn];
    [self refreshRomsText];
}

- (void)buildSlots:(NSView *)p
{
    static const char *const slot[] = { "Slot P504", "Slot P505", "Slot P506" };
    NSUInteger start;
    CGFloat y = 12;
    for (int i = 0; i < H89_SLOTS; i++, y += 28)
        [self combo:p label:S(slot[i]) x:16 y:y lw:110 cw:220 field:&_cfg.slot[i] names:h89_card_name strs:NULL
             values:NULL count:0];
    y += 8;
    start = [_rows count];
    for (int i = 0; i < H89_H17_DRIVES; i++, y += 28)
        [self combo:p label:[NSString stringWithFormat:@"H17 drive %d", i + 1] x:16 y:y lw:110 cw:220
              field:&_cfg.h17_drive[i] names:h89_h17_drive_name strs:NULL values:NULL count:0];
    [self setGroup:G_H17 from:start];
    y += 8;
    start = [_rows count];
    for (int i = 0; i < H89_H37_DRIVES; i++, y += 28)
        [self combo:p label:[NSString stringWithFormat:@"Z-89-37 drive %d", i + 1] x:16 y:y lw:110 cw:220
              field:&_cfg.h37_drive[i] names:h89_fdd_name strs:NULL values:NULL count:0];
    [self setGroup:G_H37 from:start];
    y = 12;
    start = [_rows count];
    for (int i = 0; i < H89_MMS316_DRIVES; i++, y += 28)
        [self combo:p label:[NSString stringWithFormat:@"77316 unit %d (%@)", i + 1, i < 4 ? @"8\"" : @"5\""]
                  x:370 y:y lw:130 cw:200 field:&_cfg.mms316_drive[i] names:h89_fdd_name strs:NULL
             values:NULL count:0];
    [self setGroup:G_MMS316 from:start];
    y += 8;
    start = [_rows count];
    [self combo:p label:@"77320 port" x:370 y:y lw:130 cw:200 field:&_cfg.mms320_port names:NULL strs:kPortNames
         values:kPorts count:2];
    y += 28;
    [self combo:p label:@"77320 interrupt" x:370 y:y lw:130 cw:200 field:&_cfg.mms320_intr names:NULL
           strs:kIntNames values:kInts count:4];
    y += 28;
    for (int i = 0; i < H89_MMS320_DRIVES; i++, y += 28)
        [self combo:p label:[NSString stringWithFormat:@"77320 unit %d", i] x:370 y:y lw:130 cw:200
              field:&_cfg.mms320_drive[i] names:h89_sasi_name strs:NULL values:NULL count:0];
    [self setGroup:G_MMS320 from:start];
}

- (void)buildSwitches:(NSView *)p
{
    static const char *const bauds[] = { "110", "150", "300", "600", "1200", "1800", "2000", "2400", "3600",
                                         "4800", "7200", "9600", "19200", NULL };
    static const struct { const char *name; uint8_t mask; } sw402[] = {
        { "Block cursor", 0x01 }, { "No key click", 0x02 }, { "Wrap at the end of a line", 0x04 },
        { "Line feed on carriage return", 0x08 }, { "Carriage return on line feed", 0x10 },
        { "ANSI mode at power-up", 0x20 }, { "Keypad shifted", 0x40 }, { "50 Hz refresh", 0x80 },
    };
    H89Row *r;
    CGFloat y = 12;
    int code;
    [self label:@"SW501 (CPU board): read by the monitor at power-up" page:p x:16 y:y w:600];
    y += 24;
    [self flag:p label:@"Set the device switches from the slots" x:16 y:y w:400 field:&_cfg.sw501_auto];
    y += 28;
    _sw501Y = y;
    y += 8 * 28;
    [self label:@"SW401 (H19: the line)" page:p x:16 y:y w:300];
    [self label:@"SW402 (H19: the terminal)" page:p x:370 y:y w:300];
    y += 24;
    r = [self addRow:K_BAUD page:p label:@"Baud rate" x:16 y:y lw:110 cw:120];
    fill_names((NSPopUpButton *)r.control, NULL, bauds);
    code = _cfg.sw401 & 0x0f;
    [(NSPopUpButton *)r.control selectItemAtIndex:code >= 1 && code <= 13 ? code - 1 : 11];
    [self bit:p label:@"Parity" x:16 y:y + 28 w:300 byte:&_cfg.sw401 mask:0x10];
    [self bit:p label:@"Even parity" x:16 y:y + 52 w:300 byte:&_cfg.sw401 mask:0x20];
    [self bit:p label:@"Stick parity" x:16 y:y + 76 w:300 byte:&_cfg.sw401 mask:0x40];
    [self bit:p label:@"Full duplex" x:16 y:y + 100 w:300 byte:&_cfg.sw401 mask:0x80];
    for (int i = 0; i < 8; i++)
        [self bit:p label:S(sw402[i].name) x:370 y:y + i * 24 w:300 byte:&_cfg.sw402 mask:sw402[i].mask];
}

- (void)buildCpnet:(NSView *)p
{
    NSButton *btn;
    CGFloat y = 12;
    [self label:@"A host folder served to CP/NET as its drives: a, b, … are folders inside it"
           page:p x:16 y:y w:660];
    y += 28;
    [self flag:p label:@"CP/NET device" x:16 y:y w:300 field:&_cfg.cpnet_enabled];
    y += 30;
    [self number:p label:@"Port" x:16 y:y lw:110 field:&_cfg.cpnet_port];
    y += 30;
    [self number:p label:@"Client ID" x:16 y:y lw:110 field:&_cfg.cpnet_clientid];
    y += 30;
    [self label:@"Host folder" page:p x:16 y:y w:110];
    _cpnetDir = [self label:_cfg.cpnet_root[0] ? S(_cfg.cpnet_root) : @"(the default)" page:p x:126 y:y w:460];
    [_cpnetDir setLineBreakMode:NSLineBreakByTruncatingMiddle];
    btn = [NSButton buttonWithTitle:@"Choose…" target:self action:@selector(chooseCpnetFolder:)];
    [btn setFrame:NSMakeRect(600, y - 3, 100, 28)];
    [p addSubview:btn];
}

- (void)buildDisplay:(NSView *)p
{
    static const char *const phosphors[] = { "White (P4)", "Green (P1)", "Amber (P3)", NULL };
    static const char *const aspects[] = { "The tube's 4:3", "Square pixels", NULL };
    NSSlider *volume;
    CGFloat y = 12;
    [self setCombo:p label:@"Phosphor" x:16 y:y lw:110 cw:200 key:"view.phosphor" def:0 strs:phosphors kind:S_VIEW];
    y += 30;
    [self setCheck:p label:@"Scanlines" x:16 y:y w:300 key:"view.scanlines" def:0 kind:S_VIEW];
    y += 26;
    [self setCheck:p label:@"Blinking cursor" x:16 y:y w:300 key:"view.cursor_blink" def:1 kind:S_VIEW];
    y += 26;
    [self setCombo:p label:@"Aspect" x:16 y:y lw:110 cw:200 key:"view.aspect" def:0 strs:aspects kind:S_VIEW];
    y += 30;
    [self setCheck:p label:@"Smooth scaling" x:16 y:y w:300 key:"view.smooth" def:1 kind:S_VIEW];
    y += 40;
    [self setCheck:p label:@"Enable audio (applied when Settings closes)" x:16 y:y w:400 key:"enable_audio" def:1
              kind:S_HOST];
    y += 28;
    [self label:@"Volume" page:p x:16 y:y w:110];
    volume = [NSSlider sliderWithValue:h89session_get_int(_session, "sound.volume", 70) minValue:0 maxValue:100
                                target:self action:@selector(volumeChanged:)];
    [volume setContinuous:YES];
    [volume setFrame:NSMakeRect(126, y, 220, 24)];
    [p addSubview:volume];
    y += 34;
    [self setCheck:p label:@"Bell" x:16 y:y w:300 key:"sound.bell" def:1 kind:S_LIVE];
    y += 26;
    [self setCheck:p label:@"Key click (unless SW402 or ESC x2 turns it off)" x:16 y:y w:400 key:"sound.click"
               def:1 kind:S_LIVE];
    y += 26;
    [self setCheck:p label:@"Visual bell (flash the screen)" x:16 y:y w:400 key:"sound.visual_bell" def:0
              kind:S_LIVE];
}

- (void)buildFujinet:(NSView *)p
{
    H89Row *r;
    NSButton *btn;
    CGFloat y = 12;
    [self setCheck:p label:@"Enable FujiNet (applied when Settings closes)" x:16 y:y w:400 key:"enable_fujinet"
               def:1 kind:S_HOST];
    y += 30;
    [self setCheck:p label:@"Use external FujiNet (dial another instance)" x:16 y:y w:400
               key:"fujinet_external" def:0 kind:S_HOST];
    y += 30;
    [self label:@"Address:port" page:p x:16 y:y + 3 w:110];
    _externalAddr = [NSTextField textFieldWithString:S(h89session_get_str(_session, "fujinet_external_addr", ""))];
    [_externalAddr setPlaceholderString:@"127.0.0.1:11512"];
    [_externalAddr setFrame:NSMakeRect(126, y, 240, 24)];
    [_externalAddr setEnabled:h89session_get_int(_session, "fujinet_external", 0) != 0];
    [p addSubview:_externalAddr];
    y += 34;
    [self label:(h89session_fujinet_external(_session)
                     ? @"Web UI: FujiNet is external"
                     : [NSString stringWithFormat:@"Web UI: %@", S(h89session_fujinet_webui_url(_session))])
           page:p x:16 y:y w:400];
    btn = [NSButton buttonWithTitle:@"Open" target:self action:@selector(openWebUI:)];
    [btn setFrame:NSMakeRect(420, y - 3, 80, 28)];
    [p addSubview:btn];
    y += 34;
    r = [self number:p label:@"Card handshake delay (cycles)" x:16 y:y lw:220 field:&_cfg.fujinet_ack_delay];
    r.group = G_FUJI;
}

- (void)build
{
    H89FlippedView *content = [[H89FlippedView alloc] initWithFrame:NSMakeRect(0, 0, WIN_W, WIN_H)];
    NSButton *cancel;

    [[self window] setContentView:content];
    [_rows removeAllObjects];
    [_pages removeAllObjects];

    _status = [NSTextField labelWithString:@""];
    [_status setFrame:NSMakeRect(16, 12, WIN_W - 32, 18)];
    [_status setLineBreakMode:NSLineBreakByTruncatingTail];
    [content addSubview:_status];

    _tabs = [[NSTabView alloc] initWithFrame:NSMakeRect(12, 38, WIN_W - 24, WIN_H - 38 - 56)];
    for (int i = 0; i < NPAGES; i++) {
        NSTabViewItem *item = [[NSTabViewItem alloc] initWithIdentifier:S(kPageKeys[i])];
        H89FlippedView *page = [[H89FlippedView alloc] initWithFrame:NSMakeRect(0, 0, WIN_W - 40, WIN_H - 140)];
        [item setLabel:S(kPageNames[i])];
        [item setView:page];
        [_tabs addTabViewItem:item];
        [_pages addObject:page];
    }
    [content addSubview:_tabs];

    [self buildMachine:_pages[0]];
    [self buildSlots:_pages[1]];
    [self buildSwitches:_pages[2]];
    [self buildCpnet:_pages[3]];
    [self buildDisplay:_pages[4]];
    [self buildFujinet:_pages[5]];

    cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancelPressed:)];
    [cancel setKeyEquivalent:@"\033"];
    [cancel setFrame:NSMakeRect(WIN_W - 16 - 2 * 100 - 8, WIN_H - 44, 100, 30)];
    [content addSubview:cancel];
    _ok = [NSButton buttonWithTitle:@"OK" target:self action:@selector(okPressed:)];
    [_ok setKeyEquivalent:@"\r"];
    [_ok setFrame:NSMakeRect(WIN_W - 16 - 100, WIN_H - 44, 100, 30)];
    [content addSubview:_ok];

    /* after the tabs are filled, so adding them does not run it */
    [_tabs setDelegate:self];
}

- (void)showOnPage:(NSString *)page
{
    const char *want = page ? [page UTF8String] : NULL;
    if (![[self window] isVisible]) {
        h89session_get_config(_session, &_cfg);
        _orig = _cfg;
        _hostChanged = NO;
        [self build];
        [self changed];
        if (!want)
            want = getenv("H89_PREFS_PAGE");
        [[self window] center];
    }
    for (int i = 0; want && i < NPAGES; i++)
        if (!strcmp(want, kPageKeys[i]))
            [_tabs selectTabViewItemAtIndex:i];
    [[self window] makeKeyAndOrderFront:nil];
}

@end
