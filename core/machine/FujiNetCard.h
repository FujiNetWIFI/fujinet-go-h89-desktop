/*
 * FujiNetCard -- the H89-ESP32 FujiNet card (schematic v1.28): an 82C55 PPI
 * on the right-side I/O bus at 0xD0-0xD3 (decoded from /SER0 and A2), port A
 * in mode 2 (bidirectional, strobed) through a 74LVC245 to an ESP32.
 *
 * Registers, as fujinet-lib's bus/h89/portio.c drives them:
 *   0xD0  port A: OUT queues a byte for the ESP32 (/OBF falls, rises again
 *         when the ESP32 /ACKs); IN takes the byte the ESP32 strobed in
 *         (IBF clears)
 *   0xD2  port C: IN is the mode 2 status word
 *             b7 /OBF  b6 INTE1  b5 IBF  b4 INTE2  b3 INTRA  b2-0 PC2-PC0
 *         OUT sets PC0-PC2, the group B outputs:
 *             PC0  74LVC245 DIR (1 = H89 -> ESP32)
 *             PC1  74LVC245 /OE, and the ESP32's IO0 boot strap
 *             PC2  ESP32 EN (0 holds it in reset)
 *   0xD3  control: bit 7 set = mode word (clears port C, the INTEs, /OBF
 *         and IBF); bit 7 clear = port C bit set/reset, (bit << 1) | value
 *
 * The ESP32 side is FujiNet itself, over a raw byte pipe (core/net/rawpipe)
 * to the in-process runtime's BoIP listener: the bytes are SLIP-framed
 * FujiBus packets the H89 builds itself, which the RS232 bus speaks
 * natively -- so the card passes them through without looking at them, as
 * the real card's ESP32 relay does.
 *
 * What the ESP32 does with the handshake, and so what this models:
 *   - it /ACKs a byte the H89 writes a moment later (ackDelay cycles) when
 *     it is running; held in reset (EN low) or in its ROM download mode
 *     (EN rose while PC1 was low) it never does, and the H89 waits;
 *   - it strobes the next received byte in once IBF is clear, one byte at
 *     a time, while the 245 points its way (DIR = 0, /OE = 0);
 *   - EN falling is a reset: whatever was in flight is lost (the pipe is
 *     closed, and FujiNet sees the disconnect).
 * INTR is not wired to the H89 on this card, so nothing interrupts.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_FUJINETCARD_H
#define H89_FUJINETCARD_H

#include "ClockUser.h"
#include "IODevice.h"

#include <string>

struct rawpipe;

class FujiNetCard: public IODevice, public ClockUser
{
  public:
    static const BYTE basePort_c = 0xd0;

    /// host/port: FujiNet's BoIP listener (127.0.0.1:11512 in the app).
    FujiNetCard(const std::string& host, int port, unsigned ackDelayCycles = 40);
    ~FujiNetCard() override;

    BYTE in(BYTE addr) override;
    void out(BYTE addr, BYTE val) override;
    void reset() override;
    void notification(unsigned int cycleCount) override;

    enum Link
    {
        LinkNoRuntime = 0,  ///< the pipe has nowhere to go
        LinkWaiting,        ///< not connected (yet); retrying
        LinkUp,             ///< connected
        LinkEspReset,       ///< the H89 holds the ESP32 in reset
        LinkEspDownload,    ///< the ESP32 is in its ROM download mode
    };

    struct State
    {
        BYTE  mode, portC, status, outLatch, inLatch;
        bool  obfLow, ibf, inte1, inte2, en, oeLow, dirToEsp, download;
        Link  link;
        unsigned long long bytesToEsp, bytesFromEsp, txDropped, ackWaits;
        size_t rxQueued, txQueued;
        unsigned connects, drops;
    };
    State state() const;
    Link link() const;

  private:
    BYTE status() const;
    void setPortC(BYTE pc);
    void poll();
    void fillInput();

    rawpipe*  pipe_m;
    unsigned  ackDelay_m;

    BYTE      mode_m;
    BYTE      portC_m;     // PC0-PC2 output latch
    bool      inte1_m, inte2_m;
    bool      obfLow_m;    // a byte waits for the ESP32's /ACK
    unsigned  ackCountdown_m;
    BYTE      outLatch_m;
    bool      ibf_m;
    BYTE      inLatch_m;
    bool      download_m;

    unsigned  sincePoll_m;

    unsigned long long bytesToEsp_m, bytesFromEsp_m, txDropped_m, ackWaits_m;
};

#endif /* H89_FUJINETCARD_H */
