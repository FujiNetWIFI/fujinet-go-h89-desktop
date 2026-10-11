/*
 * rawpipe -- a non-blocking TCP client that is just a byte pipe.
 *
 * What the H89's FujiNet card (core/machine/FujiNetCard.cpp) hangs off: the
 * bytes the H89 writes into the card's 82C55 go out unchanged, and whatever
 * comes back is handed to the card byte by byte. Nothing here understands
 * FujiBus or SLIP -- the H89 software (fujinet-lib's bus/h89) frames its own
 * packets, exactly as it does over the real card's ESP32.
 *
 * Owned and polled by the emulator thread; never blocks. The connection is
 * made lazily and remade the same way whenever it drops (FujiNet restarted
 * from its web UI, or not up yet), at most every RAWPIPE_RETRY_MS.
 *
 * One file for POSIX and Winsock.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_RAWPIPE_H
#define H89_RAWPIPE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAWPIPE_RETRY_MS 250
#define RAWPIPE_RING     8192

typedef enum {
    RAWPIPE_DOWN = 0,       /* not connected; will retry */
    RAWPIPE_CONNECTING,     /* a connect() is in flight */
    RAWPIPE_UP              /* connected */
} rawpipe_state;

typedef struct rawpipe rawpipe;

/* host is a numeric address or a name; nothing is connected yet. */
rawpipe *rawpipe_new(const char *host, int port);
void     rawpipe_free(rawpipe *p);

/* Moves bytes both ways and (re)connects as needed. Call often: every few
 * hundred microseconds of emulated time while the card is busy is plenty. */
rawpipe_state rawpipe_poll(rawpipe *p);
rawpipe_state rawpipe_state_of(const rawpipe *p);

/* Queue a byte for the peer; 0 when the ring is full (back-pressure). */
int    rawpipe_put(rawpipe *p, uint8_t b);
size_t rawpipe_tx_free(const rawpipe *p);

/* The next byte from the peer; 0 when there is none. */
int    rawpipe_get(rawpipe *p, uint8_t *b);
size_t rawpipe_rx_avail(const rawpipe *p);

/* Close now and drop both rings (the card's ESP32 going into reset); the
 * next poll reconnects. */
void rawpipe_reset(rawpipe *p);

/* Counters for the debugger. */
typedef struct {
    uint64_t bytes_out, bytes_in;
    unsigned connects, drops;
} rawpipe_stats;
void rawpipe_get_stats(const rawpipe *p, rawpipe_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* H89_RAWPIPE_H */
