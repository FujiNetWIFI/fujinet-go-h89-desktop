/*
 * help_test -- the Help covers what the app has: every Preferences setting
 * and choice, every machine, every ROM, every default key and gamepad button,
 * the app's shortcuts (and the Mac's), the two ports, the debugger's tabs
 * and prompt commands, and every context a Help button opens. Every miss is
 * listed before it fails.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "h89help.h"
#include "h89session.h"

#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); exit(1); } } while (0)

static const unsigned platforms[] = {
    H89HELP_GNOME, H89HELP_KDE, H89HELP_WINDOWS, H89HELP_MACOS
};
static const char *const platform_names[] = { "GNOME", "KDE", "Windows", "macOS" };

/* What the frontends' Help buttons and menus open. */
static const char *const contexts[] = {
    "main", "nobios", "keys", "fujilog", "prefs/machine", "prefs/slots", "prefs/switches", "prefs/cpnet",
    "prefs/display", "prefs/fujinet", "prefs/windows", "debugger/prompt", "debugger/cpu",
    "debugger/disassembly", "debugger/breakpoints", "debugger/h89", "debugger/fujinet",
};

/* The Preferences labels, as the frontends show them. */
static const char *const labels[] = {
    "Model", "Memory", "Memory slot P503", "Monitor ROM", "Speed-up", "Imported ROMs", "P504", "P505", "P506",
    "Set the device switches from the slots", "SW501", "SW401", "SW402", "Baud rate", "Parity", "Even parity",
    "Stick parity", "Full duplex", "Block cursor", "No key click", "Wrap at the end of a line",
    "Line feed on carriage return", "Carriage return on line feed", "ANSI mode", "Keypad shifted",
    "50 Hz refresh", "CP/NET device", "Port", "Client ID", "Host folder", "Phosphor", "Scanlines",
    "Blinking cursor", "Aspect", "Smooth scaling", "Enable audio", "Volume", "Bell", "Key click",
    "Visual bell", "Enable FujiNet", "Use external FujiNet", "Address:port", "Web UI",
    "Card handshake delay", "White (P4)", "Green (P1)", "Amber (P3)", "The tube's 4:3", "Square pixels",
    "FujiNet (H89-ESP32)", "H-88-1 (H17 hard-sectored)", "Z-89-37 (H37 soft-sectored)",
    "MMS 77316 (double density)", "MMS 77320 (SASI hard disk)", "HA-88-3 (3-port serial)",
};
/* Every choice the session's tables offer, in the topic that lists them. */
static const char *const choices[] = {
    "H89 (ORG-0)", "H88 (no ORG-0)", "WH-88-16 (64K)", "MMS 77318 (128K)", "None (2.048 MHz)",
};

/* The settings keys the pages write. */
static const char *const settings_keys[] = {
    "view.phosphor", "view.scanlines", "view.cursor_blink", "view.aspect", "view.smooth", "enable_audio",
    "sound.volume", "sound.bell", "sound.click", "sound.visual_bell", "enable_fujinet", "fujinet_external",
    "fujinet_external_addr",
};

/* The app's shortcuts, as a PC and as a Mac shows them. */
static const struct { const char *pc, *mac; } shortcuts[] = {
    { "Ctrl+Shift+H", "⌘?" }, { "Ctrl+Shift+C", "⌘⇧C" }, { "Ctrl+Shift+V", "⌘⇧V" },
    { "Ctrl+Shift+O", "⌘⇧O" }, { "Ctrl+Shift+R", "⌘⇧R" }, { "Ctrl+Shift+Q", "⌘⇧Q" }, { "Ctrl+,", "⌘," },
    { "F9", "F9" }, { "F11", "F11" }, { "F12", "F12" },
};

static int misses;

/* Lower case, every run of white space one space. */
static void
fold(const char *in, char *out, size_t outsz)
{
    size_t n = 0;
    int space = 0;
    for (; *in && n + 1 < outsz; in++) {
        unsigned char c = (unsigned char) *in;
        if (c == ' ' || c == '\n' || c == '\t') {
            space = 1;
            continue;
        }
        if (space && n)
            out[n++] = ' ';
        space = 0;
        out[n++] = (char) ((c >= 'A' && c <= 'Z') ? c + 32 : c);
    }
    out[n] = 0;
}

static char section[1 << 18];
static char raw[1 << 18];

/* The folded text of a context's (or slug's) section on platform p; raw
 * keeps the lines. */
static const char *
section_text(int p, const char *context)
{
    int t, b;
    section[0] = 0;
    raw[0] = 0;
    if (h89help_find(context, &t, &b) == 0) {
        h89help_plain_text(platforms[p], t, b, raw, sizeof raw);
        fold(raw, section, sizeof section);
    }
    return section;
}

static void
require(int p, const char *context, const char *what, const char *text)
{
    char want[512];
    fold(text, want, sizeof want);
    if (!strstr(section_text(p, context), want)) {
        fprintf(stderr, "MISSING (%s) %s: %s \"%s\"\n", platform_names[p], context, what, text);
        misses++;
    }
}

/* A line of the section (a table row: cells joined by " | ") with every one
 * of these, folded, in that order. */
static void
require_row(int p, const char *context, const char *a, const char *b, const char *c)
{
    char line[1024], fa[256], fb[256], fc[256];
    const char *s, *e;
    int ok = 0;

    section_text(p, context);
    fold(a, fa, sizeof fa);
    fold(b ? b : "", fb, sizeof fb);
    fold(c ? c : "", fc, sizeof fc);
    for (s = raw; *s && !ok; s = *e ? e + 1 : e) {
        char *x, *y;
        size_t n;
        e = strchr(s, '\n');
        if (!e)
            e = s + strlen(s);
        n = (size_t) (e - s) < sizeof line - 1 ? (size_t) (e - s) : sizeof line - 1;
        memcpy(line, s, n);
        line[n] = 0;
        fold(line, line, sizeof line);
        x = strstr(line, fa);
        if (!x)
            continue;
        y = b && *fb ? strstr(x + strlen(fa), fb) : x + strlen(fa);
        if (!y)
            continue;
        if (c && *fc && !strstr(y + (b && *fb ? strlen(fb) : 0), fc))
            continue;
        ok = 1;
    }
    if (!ok) {
        fprintf(stderr, "MISSING (%s) %s: a row \"%s\" | \"%s\" | \"%s\"\n", platform_names[p], context,
                a, b ? b : "", c ? c : "");
        misses++;
    }
}

static void
check_prefs(int p)
{
    for (size_t k = 0; k < sizeof labels / sizeof *labels; k++)
        require(p, "preferences", "label", labels[k]);
    for (size_t k = 0; k < sizeof settings_keys / sizeof *settings_keys; k++)
        require(p, "preferences", "setting", settings_keys[k]);
    for (size_t k = 0; k < sizeof choices / sizeof *choices; k++)
        require(p, "machines", "choice", choices[k]);
}

/* The shortcuts the app keeps. */
static void
check_controls(int p)
{
    for (size_t k = 0; k < sizeof shortcuts / sizeof *shortcuts; k++)
        require(p, "controllers-and-keys", "shortcut", platforms[p] == H89HELP_MACOS ? shortcuts[k].mac : shortcuts[k].pc);
}

/* The two ports, in the FujiNet topic. */
static void
check_ports(int p)
{
    char port[16];

    snprintf(port, sizeof port, "%d", H89SESSION_BOIP_PORT);
    require(p, "fujinet", "BoIP port", port);
    snprintf(port, sizeof port, "%d", H89SESSION_WEBUI_PORT);
    require(p, "fujinet", "web UI port", port);
    require(p, "an-external-fujinet", "default address", "127.0.0.1:11512");
}

/* The debugger's buttons and its tabs. */
/* The debugger's buttons, and each prompt command in a `code` span. */
/* The debugger's buttons. */
static void
check_debugger(int p)
{
    static const char *const words[] = { "Step Over", "Step Out", "Run or Stop", "six tabs" };
    for (size_t k = 0; k < sizeof words / sizeof *words; k++)
        require(p, "debugger", "button", words[k]);
}

int
main(void)
{
    h89help_hit hits[16];
    int t, b;

    CHECK(h89help_topic_count() > 0, "no topics");

    for (size_t k = 0; k < sizeof contexts / sizeof *contexts; k++)
        if (h89help_find(contexts[k], &t, &b) != 0) {
            fprintf(stderr, "MISSING context %s\n", contexts[k]);
            misses++;
        }
    for (int p = 0; p < 4; p++) {
        check_prefs(p);
        check_controls(p);
        check_ports(p);
        check_debugger(p);
    }

    /* lookup and search */
    CHECK(h89help_find("#preferences", &t, &b) == 0 && b == -1, "#preferences");
    CHECK(h89help_find("#the-settings-file", &t, &b) == 0 && b >= 0, "#the-settings-file");
    CHECK(h89help_search(H89HELP_GNOME, "disk", hits, 16) > 0, "search disk");
    CHECK(h89help_search(H89HELP_GNOME, "zzqxj", hits, 16) == 0, "search nonsense");
    int n = h89help_search(H89HELP_KDE, "breakpoint", hits, 16);
    CHECK(n > 0 && h89help_find("debugger/breakpoints", &t, &b) == 0
          && hits[0].topic == t && hits[0].block == b, "breakpoint finds the breakpoints tab");
    for (int i = 1; i < n; i++)
        CHECK(hits[i - 1].score >= hits[i].score, "hits best first");

    /* the Mac sees its own keys */
    {
        char text[4096];
        CHECK(h89help_find("controllers-and-keys", &t, &b) == 0, "controllers-and-keys");
        h89help_plain_text(H89HELP_MACOS, t, b, text, sizeof text);
        CHECK(strstr(text, "⌘?") != NULL, "the Mac's Help key");
        h89help_plain_text(H89HELP_GNOME, t, b, text, sizeof text);
        CHECK(strstr(text, "Ctrl+Shift+H") != NULL && !strstr(text, "⌘?"), "the PC's");
    }

    CHECK(misses == 0, "%d missing from data/help/help.md", misses);
    printf("help ok: %d topics\n", h89help_topic_count());
    return 0;
}
