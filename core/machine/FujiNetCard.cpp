/*
 * FujiNetCard -- see FujiNetCard.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "FujiNetCard.h"

#include "rawpipe.h"

namespace
{
const BYTE PC_DIR = 0x01;   // 245 DIR: 1 = H89 -> ESP32
const BYTE PC_OE  = 0x02;   // 245 /OE (and the ESP32's IO0 strap)
const BYTE PC_EN  = 0x04;   // ESP32 EN

// how often the pipe is serviced while the ESP32 runs: 512 cycles of
// 2.048 MHz time, a quarter of a millisecond
const unsigned pollCycles_c = 512;
// a status read finding nothing services it too, but not more often than this
const unsigned statusPollCycles_c = 64;
}

FujiNetCard::FujiNetCard(const std::string& host, int port, unsigned ackDelayCycles):
    IODevice(basePort_c, 4),
    pipe_m(nullptr),
    ackDelay_m(ackDelayCycles ? ackDelayCycles : 1),
    bytesToEsp_m(0),
    bytesFromEsp_m(0),
    txDropped_m(0),
    ackWaits_m(0)
{
    if (port > 0)
    {
        pipe_m = rawpipe_new(host.c_str(), port);
    }
    reset();
}

FujiNetCard::~FujiNetCard()
{
    rawpipe_free(pipe_m);
}

void
FujiNetCard::reset()
{
    // /RESET on the 82C55: every port an input, latches clear. The ESP32
    // is held in reset until software sets the mode and raises EN, but PC2
    // reads 1 meanwhile: the bus hold pulls it up against EN's divider.
    mode_m         = 0x9b;
    portB_m        = 0;
    portC_m        = 0;
    pcLatch_m      = 0;
    pc2Held_m      = true;
    inte1_m        = false;
    inte2_m        = false;
    obfLow_m       = false;
    ackCountdown_m = 0;
    outLatch_m     = 0;
    ibf_m          = false;
    inLatch_m      = 0xff;
    download_m     = false;
    sincePoll_m    = 0;
    if (pipe_m)
    {
        rawpipe_reset(pipe_m);
    }
}

BYTE
FujiNetCard::status() const
{
    if (!mode2())
    {
        return portCPins();
    }
    bool intra = (!obfLow_m && inte1_m) || (ibf_m && inte2_m);
    return (BYTE) ((obfLow_m ? 0 : 0x80) | (inte1_m ? 0x40 : 0) | (ibf_m ? 0x20 : 0) |
                   (inte2_m ? 0x10 : 0) | (intra ? 0x08 : 0) |
                   (pcLowIn() ? (portCPins() & 0x07) : (portC_m & 0x07)));
}

BYTE
FujiNetCard::portCPins() const
{
    // An output reads its latch. An input reads its line: /OBF (PC7), IBF
    // (PC5), /OE (PC1) and DIR (PC0) through their dividers to ground, so
    // 0; /ACK (PC6) and /STB (PC4) from the ESP32, and INTR (PC3, not
    // wired), held at 1; PC2 as the bus hold left it.
    BYTE hi = pcHighIn() ? 0x50 : (BYTE) (pcLatch_m & 0xf0);
    BYTE lo = pcLowIn() ? (BYTE) (0x08 | (pc2Held_m ? PC_EN : 0))
                        : (BYTE) ((pcLatch_m & 0x08) | (portC_m & 0x07));
    return (BYTE) (hi | lo);
}

void
FujiNetCard::setPortC(BYTE pc)
{
    BYTE was = portC_m;
    portC_m  = pc & 0x07;

    bool enWas = (was & PC_EN) != 0;
    bool enNow = (portC_m & PC_EN) != 0;

    if (enWas && !enNow)
    {
        // into reset: whatever the ESP32 had in flight is gone
        if (pipe_m)
        {
            rawpipe_reset(pipe_m);
        }
        ibf_m      = false;
        download_m = false;
    }
    else if (!enWas && enNow)
    {
        // out of reset; IO0 (PC1, the 245's /OE) low selects the ROM loader
        download_m = (portC_m & PC_OE) == 0;
        if (pipe_m)
        {
            rawpipe_reset(pipe_m);
        }
        sincePoll_m = pollCycles_c; // connect at once
    }
}

BYTE
FujiNetCard::in(BYTE addr)
{
    switch (addr - basePort_c)
    {
        case 0: // port A: the byte the ESP32 strobed in
        {
            if (!mode2())
            {
                return 0xff; // no strobe outside mode 2
            }
            BYTE val = inLatch_m;
            ibf_m    = false;
            fillInput();
            return val;
        }

        case 1: // port B: wired to nothing, so the latch or the bus hold
            return portBIn() ? 0xff : portB_m;

        case 2: // port C: the status word
            if (!ibf_m && sincePoll_m >= statusPollCycles_c)
            {
                poll();
            }
            return status();

        case 3: // the control register cannot be read
        default:
            return 0xff;
    }
}

void
FujiNetCard::out(BYTE addr, BYTE val)
{
    switch (addr - basePort_c)
    {
        case 0: // port A: a byte for the ESP32
            if (!mode2())
            {
                break; // no /OBF outside mode 2, so the ESP32 never takes it
            }
            outLatch_m     = val;
            obfLow_m       = true;
            ackCountdown_m = ackDelay_m;
            break;

        case 1: // port B
            if (!portBIn())
            {
                portB_m = val;
            }
            break;

        case 2: // port C: PC0-PC2 when they are outputs; PC3-PC7 outside mode 2
            if (!pcLowIn())
            {
                setPortC(val);
            }
            if (!mode2())
            {
                BYTE outs = (BYTE) ((pcHighIn() ? 0 : 0xf0) | (pcLowIn() ? 0 : 0x08));
                pcLatch_m = (BYTE) ((pcLatch_m & ~outs) | (val & outs));
            }
            break;

        case 3:
            if (val & 0x80)
            {
                // a mode word clears every output latch and both INTEs; the
                // bus hold no longer wins on PC2 once EN's divider has it
                mode_m    = val;
                inte1_m   = false;
                inte2_m   = false;
                obfLow_m  = false;
                ibf_m     = false;
                portB_m   = 0;
                pcLatch_m = 0;
                pc2Held_m = false;
                setPortC(0);
            }
            else
            {
                unsigned bit = (val >> 1) & 0x07;
                bool     set = (val & 0x01) != 0;
                if (bit <= 2)
                {
                    if (!pcLowIn())
                    {
                        setPortC(set ? (BYTE) (portC_m | (1 << bit)) : (BYTE) (portC_m & ~(1 << bit)));
                    }
                }
                else if (mode2())
                {
                    if (bit == 4)
                    {
                        inte2_m = set;
                    }
                    else if (bit == 6)
                    {
                        inte1_m = set;
                    }
                    // the other mode 2 handshake lines are not settable
                }
                else if (bit == 3 ? !pcLowIn() : !pcHighIn())
                {
                    pcLatch_m = set ? (BYTE) (pcLatch_m | (1 << bit)) : (BYTE) (pcLatch_m & ~(1 << bit));
                }
            }
            break;

        default:
            break;
    }
}

FujiNetCard::Link
FujiNetCard::link() const
{
    if (!pipe_m)
    {
        return LinkNoRuntime;
    }
    if (!(portC_m & PC_EN))
    {
        return LinkEspReset;
    }
    if (download_m)
    {
        return LinkEspDownload;
    }
    return rawpipe_state_of(pipe_m) == RAWPIPE_UP ? LinkUp : LinkWaiting;
}

void
FujiNetCard::poll()
{
    sincePoll_m = 0;
    if (!pipe_m || !(portC_m & PC_EN) || download_m)
    {
        return;
    }
    rawpipe_poll(pipe_m);
    fillInput();
}

void
FujiNetCard::fillInput()
{
    // The ESP32 strobes the next byte in once IBF is clear, while the 245
    // points from it to the H89.
    if (ibf_m || !mode2() || !pipe_m || !(portC_m & PC_EN) || download_m)
    {
        return;
    }
    if ((portC_m & PC_DIR) || (portC_m & PC_OE))
    {
        return;
    }
    uint8_t b;
    if (rawpipe_get(pipe_m, &b))
    {
        inLatch_m = b;
        ibf_m     = true;
        ++bytesFromEsp_m;
    }
}

void
FujiNetCard::notification(unsigned int cycleCount)
{
    sincePoll_m += cycleCount;

    if (obfLow_m && (portC_m & PC_EN) && !download_m && pipe_m)
    {
        if (ackCountdown_m > cycleCount)
        {
            ackCountdown_m -= cycleCount;
        }
        else
        {
            ackCountdown_m = 0;
            // The ESP32 reads D0-D7 through the 245 as it /ACKs: the byte
            // only gets there if the 245 points that way.
            if ((portC_m & PC_DIR) && !(portC_m & PC_OE))
            {
                if (rawpipe_put(pipe_m, outLatch_m))
                {
                    ++bytesToEsp_m;
                    obfLow_m = false;
                }
                else
                {
                    ++ackWaits_m; // FujiNet is not keeping up: hold /OBF
                }
            }
            else
            {
                ++txDropped_m;
                obfLow_m = false;
            }
        }
    }

    if (sincePoll_m >= pollCycles_c)
    {
        poll();
    }
    else if (!ibf_m)
    {
        fillInput();
    }
}

FujiNetCard::State
FujiNetCard::state() const
{
    State s;
    s.mode         = mode_m;
    s.portB        = portBIn() ? 0xff : portB_m;
    s.portC        = portC_m;
    s.status       = status();
    s.outLatch     = outLatch_m;
    s.inLatch      = inLatch_m;
    s.obfLow       = obfLow_m;
    s.ibf          = ibf_m;
    s.inte1        = inte1_m;
    s.inte2        = inte2_m;
    s.en           = (portC_m & PC_EN) != 0;
    s.oeLow        = (portC_m & PC_OE) == 0;
    s.dirToEsp     = (portC_m & PC_DIR) != 0;
    s.download     = download_m;
    s.link         = link();
    s.bytesToEsp   = bytesToEsp_m;
    s.bytesFromEsp = bytesFromEsp_m;
    s.txDropped    = txDropped_m;
    s.ackWaits     = ackWaits_m;
    s.rxQueued     = rawpipe_rx_avail(pipe_m);
    s.txQueued     = pipe_m ? RAWPIPE_RING - rawpipe_tx_free(pipe_m) : 0;
    rawpipe_stats st;
    rawpipe_get_stats(pipe_m, &st);
    s.connects = st.connects;
    s.drops    = st.drops;
    return s;
}
