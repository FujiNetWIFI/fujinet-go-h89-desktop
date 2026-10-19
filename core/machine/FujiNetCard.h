/*
 * FujiNetCard -- the H89-ESP32 FujiNet card, Rev 1.1 as reworked for
 * fujinet-firmware PR #2042: an 82C55A on the right-side I/O bus at
 * 0xD0-0xD3 (decoded from /SER0 and A2), port A in mode 2 (bidirectional,
 * strobed) through a 74LVC245 to an ESP32.
 *
 * Registers, as fujinet-lib's bus/h89/portio.c drives them:
 *   0xD0  port A: OUT queues a byte for the ESP32 (/OBF falls, rises again
 *         when the ESP32 /ACKs); IN takes the byte the ESP32 strobed in
 *         (IBF clears)
 *   0xD2  port C: IN is the mode 2 status word
 *             b7 /OBF  b6 INTE1  b5 IBF  b4 INTE2  b3 INTRA  b2-0 PC2-PC0
 *         OUT sets PC0-PC2, the group B outputs:
 *             PC0  74LVC245 DIR (1 = H89 -> ESP32); the ESP32 reads it on IO22
 *             PC1  74LVC245 /OE; the ESP32 reads it on IO21
 *             PC2  wired to nothing: the rework leaves JP2 (EN) open
 *   0xD1  port B: the eight lamps D3-D10, each lit while its bit is 0. While
 *         a mode word makes it an output it reads back what was written,
 *         which fujinet-lib uses to mark the chip as set up; as an input it
 *         reads FF and the lamps are dark.
 *   0xD3  control: bit 7 set = mode word (clears port B, port C, the INTEs,
 *         /OBF and IBF); bit 7 clear = port C bit set/reset, (bit << 1) | value
 *
 * Outside mode 2 port C reads its pins, not the status word, and port A has
 * no handshake. After /RESET every port is an input that the 82C55A's bus
 * hold pulls up, and nothing on the card pulls PC0-PC2 down: they read 1.
 * It is not a measurement.
 *
 * The ESP32 side is FujiNet itself, over a raw byte pipe (core/net/rawpipe)
 * to the in-process runtime's BoIP listener: the bytes are SLIP-framed
 * FujiBus packets the H89 builds itself, which the RS232 bus speaks
 * natively -- so the card passes them through without looking at them, as
 * the real card's ESP32 relay does.
 *
 * What the ESP32 does with the handshake, and so what this models:
 *   - it runs from power-on; nothing the H89 writes starts, stops or
 *     restarts it, and neither does the H89's RESET;
 *   - it /ACKs a byte the H89 writes a moment later (ackDelay cycles);
 *   - it strobes the next received byte in once IBF is clear, one byte at
 *     a time, while the 245 points its way (DIR = 0, /OE = 0);
 *   - a reply in flight when the H89 resets is lost: R1 holds /OE high
 *     until a program sets the 82C55A up again, and the ESP32 gives up.
 * INTR reaches J3 through Q1, and J3 is left open, so nothing interrupts.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_FUJINETCARD_H
#define H89_FUJINETCARD_H

#include "ClockUser.h"
#include "IODevice.h"

#include <cstddef>
#include <cstdint>
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
    };

    struct State
    {
        BYTE  mode, portB, portC, status, outLatch, inLatch;
        BYTE  lamps;  ///< D3-D10, bit n set while PBn's lamp is lit
        bool  obfLow, ibf, inte1, inte2, oeLow, dirToEsp;
        Link  link;
        unsigned long long bytesToEsp, bytesFromEsp, txDropped, ackWaits;
        size_t rxQueued, txQueued;
        unsigned connects, drops;
    };
    State state() const;
    Link link() const;

    /// The bytes on the link, for the debugger's FujiNet trace: dir 0 from
    /// FujiNet as the H89 takes them, 1 the H89's as FujiNet takes them,
    /// 2 from FujiNet thrown away by a reset. One hook for the process, so
    /// it outlives the card a power cycle rebuilds.
    typedef void (*TraceFn)(void* user, int dir, const uint8_t* buf, size_t len);
    static void setTrace(TraceFn fn, void* user);
    /// Drops the hook if `user` holds it, once no call to it is running.
    static void unsetTrace(void* user);

  private:
    BYTE status() const;
    BYTE portCPins() const;
    void setPortC(BYTE pc);
    void poll();
    void fillInput();
    void dropReply();

    bool mode2() const { return (mode_m & 0x40) != 0; }
    bool portBIn() const { return (mode_m & 0x02) != 0; }
    bool pcLowIn() const { return (mode_m & 0x01) != 0; }
    bool pcHighIn() const { return (mode_m & 0x08) != 0; }

    rawpipe*  pipe_m;
    unsigned  ackDelay_m;

    BYTE      mode_m;
    BYTE      portB_m;     // port B output latch
    BYTE      portC_m;     // PC0-PC2 output latch
    BYTE      pcLatch_m;   // PC3-PC7 output latch, outside mode 2
    bool      inte1_m, inte2_m;
    bool      obfLow_m;    // a byte waits for the ESP32's /ACK
    unsigned  ackCountdown_m;
    BYTE      outLatch_m;
    bool      ibf_m;
    BYTE      inLatch_m;

    unsigned  sincePoll_m;

    unsigned long long bytesToEsp_m, bytesFromEsp_m, txDropped_m, ackWaits_m;
};

#endif /* H89_FUJINETCARD_H */
