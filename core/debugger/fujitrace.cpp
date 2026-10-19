/*
 * fujitrace -- see fujitrace.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "fujitrace.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "fuji_names.h"

namespace
{

const uint8_t slipEnd_c    = 0xc0;
const uint8_t slipEsc_c    = 0xdb;
const uint8_t slipEscEnd_c = 0xdc;
const uint8_t slipEscEsc_c = 0xdd;

// longer than any FujiBus packet; the rest of a runaway one is not kept
const size_t accMax_c = 4096;

const char*
commandName(int id)
{
    for (const auto& c : fuji_command_names)
    {
        if (c.id == id)
        {
            return c.name;
        }
    }
    return "";
}

void
describe(h89_fuji_packet& p, bool dropped)
{
    const char* way = p.to_host ? "FujiNet \xe2\x86\x92 H89" : "H89 \xe2\x86\x92 FujiNet";
    const char* lost = dropped ? ", dropped at a reset" : "";
    char        times[16] = "";
    if (p.repeat > 1)
    {
        snprintf(times, sizeof times, " \xc3\x97%u", (unsigned) p.repeat);
    }
    if (p.device >= 0)
    {
        const char* name = commandName(p.command);
        snprintf(p.text, sizeof p.text, "%s %02X %02X%s%s (%d bytes)%s%s", way, p.device, p.command,
                 *name ? " " : "", name, p.nbytes, times, lost);
    }
    else
    {
        snprintf(p.text, sizeof p.text, "%s %d byte%s%s%s", way, p.nbytes, p.nbytes == 1 ? "" : "s",
                 times, lost);
    }
}

}

void
FujiTrace::feed(int dir, const uint8_t* buf, size_t len)
{
    std::lock_guard<std::mutex> lk(mtx_m);
    switch (dir)
    {
        case ToFujiNet:
            // the H89 sends again: what is left of a reply is junk
            acc_m[1].clear();
            esc_m[1] = false;
            unslip(0, buf, len, false);
            break;

        case FromFujiNet:
            unslip(1, buf, len, false);
            break;

        case Dropped:
            // a reset: a packet half sent is cut off, and the rest of
            // FujiNet's reply never reaches the H89
            if (!acc_m[0].empty())
            {
                packet(0, true);
            }
            unslip(1, buf, len, true);
            if (!acc_m[1].empty())
            {
                packet(1, true);
            }
            esc_m[0] = esc_m[1] = false;
            break;

        default:
            break;
    }
}

void
FujiTrace::unslip(int toHost, const uint8_t* buf, size_t len, bool dropped)
{
    std::vector<uint8_t>& acc = acc_m[toHost];
    for (size_t i = 0; i < len; ++i)
    {
        const uint8_t b = buf[i];
        if (b == slipEnd_c)
        {
            if (!acc.empty())
            {
                packet(toHost, dropped);
            }
            esc_m[toHost] = false;
            continue;
        }
        if (esc_m[toHost])
        {
            esc_m[toHost] = false;
            if (acc.size() < accMax_c)
            {
                acc.push_back(b == slipEscEnd_c ? slipEnd_c : b == slipEscEsc_c ? slipEsc_c : b);
            }
        }
        else if (b == slipEsc_c)
        {
            esc_m[toHost] = true;
        }
        else if (acc.size() < accMax_c)
        {
            acc.push_back(b);
        }
    }
}

void
FujiTrace::packet(int toHost, bool dropped)
{
    std::vector<uint8_t>& acc = acc_m[toHost];
    Entry                 e;
    memset(&e.p, 0, sizeof e.p);
    e.p.to_host = toHost;
    e.p.nbytes  = (int) acc.size();
    memcpy(e.p.data, acc.data(), std::min(acc.size(), sizeof e.p.data));
    // the FujiBus header: device, command, length (LE), checksum, fields
    const bool header = acc.size() >= 6;
    e.p.device  = header ? acc[0] : -1;
    e.p.command = header ? acc[1] : -1;
    e.p.length  = header ? (acc[2] | (acc[3] << 8)) : -1;
    e.p.repeat  = 1;
    e.dropped   = dropped;
    e.bytes.swap(acc);
    acc.clear();
    add(e);
}

void
FujiTrace::add(Entry& e)
{
    auto same = [](const Entry& a, const Entry& b) {
        return !a.dropped && !b.dropped && a.p.to_host == b.p.to_host && a.bytes == b.bytes;
    };
    if (!e.dropped && !log_m.empty())
    {
        // the same packet again
        Entry& last = log_m.back();
        if (same(last, e))
        {
            ++last.p.repeat;
            last.p.serial = ++serial_m;
            describe(last.p, false);
            return;
        }
        // the same reply to the same request again: the request is in, as
        // the newest entry, behind the last time's pair
        const size_t n = log_m.size();
        if (n >= 3 && e.p.to_host)
        {
            Entry& req = log_m[n - 3];
            Entry& rep = log_m[n - 2];
            if (!req.p.to_host && rep.p.to_host && req.p.repeat == rep.p.repeat && last.p.repeat == 1 &&
                same(last, req) && same(rep, e))
            {
                log_m.pop_back();
                ++req.p.repeat;
                ++rep.p.repeat;
                req.p.serial = ++serial_m;
                rep.p.serial = ++serial_m;
                describe(req.p, false);
                describe(rep.p, false);
                return;
            }
        }
    }
    e.p.serial = ++serial_m;
    describe(e.p, e.dropped);
    log_m.push_back(std::move(e));
    if (log_m.size() > Max)
    {
        log_m.pop_front();
    }
}

int
FujiTrace::read(h89_fuji_packet* out, int max)
{
    std::lock_guard<std::mutex> lk(mtx_m);
    if (!out || max <= 0)
    {
        return 0;
    }
    const size_t start = log_m.size() > (size_t) max ? log_m.size() - (size_t) max : 0;
    int          n     = 0;
    for (size_t i = start; i < log_m.size(); ++i)
    {
        out[n++] = log_m[i].p;
    }
    return n;
}

void
FujiTrace::clear()
{
    std::lock_guard<std::mutex> lk(mtx_m);
    log_m.clear();
}
