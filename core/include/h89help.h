/*
 * h89help -- the app's Help: data/help/help.md, built in by
 * tools/help/make-help.py as topics of blocks of styled spans, which every
 * frontend lays out with its own toolkit.
 *
 * A frontend shows the blocks whose platforms include its own bit. Ids are
 * GitHub's heading slugs ("controllers-and-keys"); a Help button names a
 * context instead ("prefs/Machine", "debugger/cpu"), which stays put when a
 * heading is renamed. Everything here is constant data and reentrant.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef H89HELP_H
#define H89HELP_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    H89HELP_GNOME = 1, H89HELP_KDE = 2, H89HELP_WINDOWS = 4, H89HELP_MACOS = 8
};

typedef enum {
    H89HELP_H2 = 0, H89HELP_H3,
    H89HELP_PARA,
    H89HELP_BULLET, H89HELP_BULLET2,    /* "- " and "  - " */
    H89HELP_NUMBERED,                     /* a step; number is its own */
    H89HELP_NOTE,
    H89HELP_PRE,                          /* one TEXT span, lines as they are */
    H89HELP_TABLE_HEAD,                   /* starts a table; its rows follow */
    H89HELP_TABLE_ROW
} h89help_block_kind;

typedef enum {
    H89HELP_TEXT = 0, H89HELP_BOLD, H89HELP_ITALIC, H89HELP_CODE,
    H89HELP_KBD,                          /* keys; mac_text is the Mac's */
    H89HELP_LINK,                         /* target "#slug" or a URL */
    H89HELP_CELL                          /* tables: the next column starts */
} h89help_span_kind;

typedef struct {
    int kind;
    const char *text;                       /* UTF-8 */
    const char *mac_text;                   /* KBD on macOS, or NULL: text */
    const char *target;
} h89help_span;

typedef struct {
    int kind;
    unsigned platforms;                     /* H89HELP_* bits */
    int number;
    const char *id;                         /* headings: the slug */
    const char *context;                    /* headings: or NULL */
    const h89help_span *spans;            /* headings: one TEXT span */
    int nspans;
    /* Tables: the width of each column but the last, in ems, for frontends
     * that set tab stops (the last column wraps under itself). */
    const unsigned char *col_em;
    int ncol_em;
} h89help_block;

typedef struct {
    const char *id;
    const char *title;
    const char *context;                    /* or NULL */
    const h89help_block *blocks;
    int nblocks;
} h89help_topic;

int h89help_topic_count(void);
const h89help_topic *h89help_topic_at(int t);

/* "slug", "#slug" or a context: its topic, and the heading's block (-1 for
 * the topic itself). 0, or -1 if there is none. */
int h89help_find(const char *id, int *topic, int *block);

/* The span's text as platform shows it. */
const char *h89help_span_text(const h89help_span *sp, unsigned platform);

/* A heading block's text. */
const char *h89help_heading(const h89help_block *b);

/* The block after the section headed by block (the next heading at its
 * level or above, or the topic's end); -1: the whole topic. */
int h89help_section_end(int topic, int block);

/* The section headed by block (-1: from the topic's title) as text on
 * platform: a line per block, cells separated by " | ". Returns its length
 * (truncated to dstsz - 1). */
int h89help_plain_text(unsigned platform, int topic, int block, char *dst, int dstsz);

/* Sections that have every word of query (case-insensitive), best first:
 * where it is in a title, then how often. block -1 is a topic's start, up
 * to its first heading. Returns how many were written. */
typedef struct {
    int topic, block, score;
} h89help_hit;
int h89help_search(unsigned platform, const char *query, h89help_hit *out, int max);

/* The first match of a word of query in text at byte from or after: where
 * it starts and its length, for highlighting. 1, or 0 if there is none. */
int h89help_match(const char *text, const char *query, int from, int *start, int *len);

#ifdef __cplusplus
}
#endif

#endif /* H89HELP_H */
