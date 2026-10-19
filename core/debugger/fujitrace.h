/*
 * fujitrace -- the FujiNet trace behind h89debug_fujinet_trace(): the bytes
 * on the FujiNet card, both ways, SLIP-decoded into FujiBus packets.
 *
 * The H89 frames its own packets (fujinet-lib's bus/h89) and the card
 * passes the bytes through, so this sees exactly what crosses the 82C55.
 * It knows nothing of the card or the machine: a test feeds it bytes of its
 * own. Byte-identical packets in a row, and byte-identical request/reply
 * exchanges in a row, are one entry with a count.
 *
 * Fed on the machine thread, read from windows: one mutex.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_FUJITRACE_H
#define H89_FUJITRACE_H

#include "h89debug.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

class FujiTrace
{
  public:
    static const size_t Max = 512;

    /// Which way the bytes went (FujiNetCard::setTrace's dir).
    enum Dir
    {
        FromFujiNet = 0,  ///< as the H89 takes them
        ToFujiNet   = 1,  ///< as FujiNet takes them
        Dropped     = 2,  ///< from FujiNet, thrown away by a reset
    };

    void feed(int dir, const uint8_t* buf, size_t len);
    /// The newest `max` packets, oldest first; returns the count.
    int  read(h89_fuji_packet* out, int max);
    void clear();

  private:
    struct Entry
    {
        h89_fuji_packet      p;
        std::vector<uint8_t> bytes;
        bool                 dropped;
    };

    void unslip(int toHost, const uint8_t* buf, size_t len, bool dropped);
    void packet(int toHost, bool dropped);
    void add(Entry& e);

    std::mutex           mtx_m;
    std::deque<Entry>    log_m;
    uint64_t             serial_m = 0;
    std::vector<uint8_t> acc_m[2];  // by to_host
    bool                 esc_m[2] = {false, false};
};

#endif /* H89_FUJITRACE_H */
