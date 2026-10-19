/*
 * fujitrace -- the debugger's FujiNet trace (core/debugger/fujitrace) on
 * bytes of our own, no card and no FujiNet: SLIP frames both ways, fed
 * whole and a byte at a time, escapes, short packets, the same packet and
 * the same exchange again (x N), a reply cut off by a request or a reset,
 * the ring's limit, clearing, and the empty trace.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "fujitrace.h"
#include "test_util.h"

#include <string>
#include <vector>

namespace
{

typedef std::vector<uint8_t> Bytes;

/// One SLIP frame, END at both ends as fujinet-lib's bus/h89 sends it.
Bytes
slip(const Bytes& b)
{
    Bytes out = {0xc0};
    for (uint8_t c : b)
    {
        if (c == 0xc0 || c == 0xdb)
        {
            out.push_back(0xdb);
            out.push_back(c == 0xc0 ? 0xdc : 0xdd);
        }
        else
        {
            out.push_back(c);
        }
    }
    out.push_back(0xc0);
    return out;
}

void
feed(FujiTrace& t, int dir, const Bytes& b)
{
    t.feed(dir, b.data(), b.size());
}

/// A byte at a time, as the card hands them over.
void
trickle(FujiTrace& t, int dir, const Bytes& b)
{
    for (uint8_t c : b)
    {
        t.feed(dir, &c, 1);
    }
}

std::vector<h89_fuji_packet>
all(FujiTrace& t)
{
    std::vector<h89_fuji_packet> p(FujiTrace::Max + 8);
    p.resize((size_t) t.read(p.data(), (int) p.size()));
    return p;
}

bool
has(const h89_fuji_packet& p, const char* text)
{
    return std::string(p.text).find(text) != std::string::npos;
}

}

int
main()
{
    // GET ADAPTERCONFIG: {device, command, length LE, checksum, fields}
    const Bytes req   = {0x70, 0xe8, 0x06, 0x00, 0x5e, 0x00};
    // its reply, with a C0 and a DB in it to escape
    Bytes       reply = {0x70, 0x06, 0x0e, 0x00, 0x00, 0x00, 'F', 'u', 'j', 'i', 0xc0, 0xdb, 'N', 0x00};

    FujiTrace t;
    h89_fuji_packet p[4];
    CHECK_EQ_INT(t.read(p, 4), 0);

    // a request a byte at a time, the reply in uneven pieces
    trickle(t, FujiTrace::ToFujiNet, slip(req));
    {
        const Bytes f = slip(reply);
        t.feed(FujiTrace::FromFujiNet, f.data(), 3);
        t.feed(FujiTrace::FromFujiNet, f.data() + 3, 9);   // splits the C0's escape
        t.feed(FujiTrace::FromFujiNet, f.data() + 12, f.size() - 12);
    }
    auto log = all(t);
    CHECK_EQ_INT(log.size(), 2);
    if (log.size() == 2)
    {
        CHECK_EQ_INT(log[0].to_host, 0);
        CHECK_EQ_INT(log[0].device, 0x70);
        CHECK_EQ_INT(log[0].command, 0xe8);
        CHECK_EQ_INT(log[0].length, 6);
        CHECK_EQ_INT(log[0].nbytes, 6);
        CHECK_EQ_INT(log[0].repeat, 1);
        CHECK(has(log[0], "H89 \xe2\x86\x92 FujiNet 70 E8 GET ADAPTERCONFIG (6 bytes)"));
        CHECK_EQ_INT(log[1].to_host, 1);
        CHECK_EQ_INT(log[1].command, 0x06);
        CHECK_EQ_INT(log[1].length, 14);
        CHECK_EQ_INT(log[1].nbytes, (int) reply.size());
        CHECK(memcmp(log[1].data, reply.data(), reply.size()) == 0);
        CHECK(has(log[1], "FujiNet \xe2\x86\x92 H89 70 06 ACK (14 bytes)"));
        CHECK(!has(log[1], "\xc3\x97"));
        CHECK(log[1].serial > log[0].serial);
    }

    // the same exchange three times more: still two entries, x4 each
    const uint64_t before = log.size() == 2 ? log[1].serial : 0;
    for (int i = 0; i < 3; ++i)
    {
        feed(t, FujiTrace::ToFujiNet, slip(req));
        trickle(t, FujiTrace::FromFujiNet, slip(reply));
    }
    log = all(t);
    CHECK_EQ_INT(log.size(), 2);
    if (log.size() == 2)
    {
        CHECK_EQ_INT(log[0].repeat, 4);
        CHECK_EQ_INT(log[1].repeat, 4);
        CHECK(has(log[0], "\xc3\x97" "4"));
        CHECK(has(log[1], "(14 bytes) \xc3\x97" "4"));
        CHECK(log[1].serial > before);
    }

    // the request again, before its reply: it shows at once
    feed(t, FujiTrace::ToFujiNet, slip(req));
    CHECK_EQ_INT(all(t).size(), 3);
    // a different reply: no longer the same exchange
    reply[6] = 'f';
    feed(t, FujiTrace::FromFujiNet, slip(reply));
    log = all(t);
    CHECK_EQ_INT(log.size(), 4);
    if (log.size() == 4)
    {
        CHECK_EQ_INT(log[0].repeat, 4);
        CHECK_EQ_INT(log[2].repeat, 1);
        CHECK_EQ_INT(log[3].repeat, 1);
        CHECK_EQ_INT(log[3].data[6], 'f');
    }

    // the same packet with no reply between: one entry, x3
    t.clear();
    CHECK_EQ_INT(t.read(p, 4), 0);
    const Bytes status = {0x70, 0x53, 0x06, 0x00, 0xc9, 0x00};
    for (int i = 0; i < 3; ++i)
    {
        feed(t, FujiTrace::ToFujiNet, slip(status));
    }
    log = all(t);
    CHECK_EQ_INT(log.size(), 1);
    if (log.size() == 1)
    {
        CHECK_EQ_INT(log[0].repeat, 3);
        CHECK(has(log[0], "53 STATUS (6 bytes) \xc3\x97" "3"));
        CHECK(log[0].serial > before);
    }

    // short packets: no header to read
    t.clear();
    feed(t, FujiTrace::FromFujiNet, slip({0x41, 0x42}));
    log = all(t);
    CHECK_EQ_INT(log.size(), 1);
    if (log.size() == 1)
    {
        CHECK_EQ_INT(log[0].device, -1);
        CHECK_EQ_INT(log[0].command, -1);
        CHECK_EQ_INT(log[0].length, -1);
        CHECK(has(log[0], "FujiNet \xe2\x86\x92 H89 2 bytes"));
    }
    // empty frames (END END) are no packets
    feed(t, FujiTrace::ToFujiNet, {0xc0, 0xc0, 0xc0});
    CHECK_EQ_INT(all(t).size(), 1);

    // half a reply, then the H89 sends again: the half is junk, not glued
    // onto the next reply
    t.clear();
    {
        const Bytes f = slip(reply);
        t.feed(FujiTrace::FromFujiNet, f.data(), 5);
    }
    feed(t, FujiTrace::ToFujiNet, slip(req));
    feed(t, FujiTrace::FromFujiNet, slip(reply));
    log = all(t);
    CHECK_EQ_INT(log.size(), 2);
    if (log.size() == 2)
    {
        CHECK_EQ_INT(log[1].nbytes, (int) reply.size());
        CHECK(memcmp(log[1].data, reply.data(), reply.size()) == 0);
    }

    // a reset: the H89 had taken part of a reply, the rest is thrown away;
    // a request half sent is cut off too
    t.clear();
    {
        const Bytes f = slip(reply);
        t.feed(FujiTrace::FromFujiNet, f.data(), 6);
        t.feed(FujiTrace::Dropped, f.data() + 6, f.size() - 6);
    }
    {
        const Bytes f = slip(req);
        t.feed(FujiTrace::ToFujiNet, f.data(), 3);
        t.feed(FujiTrace::Dropped, nullptr, 0);
    }
    // and a reset with nothing in flight logs nothing
    t.feed(FujiTrace::Dropped, nullptr, 0);
    log = all(t);
    CHECK_EQ_INT(log.size(), 2);
    if (log.size() == 2)
    {
        CHECK_EQ_INT(log[0].to_host, 1);
        CHECK_EQ_INT(log[0].nbytes, (int) reply.size());
        CHECK(has(log[0], "ACK (14 bytes), dropped at a reset"));
        CHECK_EQ_INT(log[1].to_host, 0);
        CHECK_EQ_INT(log[1].nbytes, 2);
        CHECK(has(log[1], "H89 \xe2\x86\x92 FujiNet 2 bytes, dropped at a reset"));
    }
    // dropped packets never count as repeats
    {
        const Bytes f = slip(reply);
        t.feed(FujiTrace::Dropped, f.data(), f.size());
    }
    CHECK_EQ_INT(all(t).size(), 3);

    // the ring keeps the newest Max, oldest first; read takes the newest
    t.clear();
    for (int i = 0; i < (int) FujiTrace::Max + 100; ++i)
    {
        feed(t, FujiTrace::ToFujiNet, slip({0x70, 0xf2, 0x06, 0x00, (uint8_t) i, (uint8_t) (i >> 8)}));
    }
    log = all(t);
    CHECK_EQ_INT(log.size(), FujiTrace::Max);
    if (log.size() == FujiTrace::Max)
    {
        CHECK_EQ_INT(log[0].data[4], 100);
        CHECK_EQ_INT(log.back().data[4], (FujiTrace::Max + 99) & 0xff);
        CHECK(has(log.back(), "F2 READ DEVICE SLOTS"));
    }
    CHECK_EQ_INT(t.read(p, 2), 2);
    CHECK_EQ_INT(p[1].serial, log.back().serial);
    CHECK_EQ_INT(t.read(p, 0), 0);
    CHECK_EQ_INT(t.read(nullptr, 4), 0);

    return t_done("fujitrace");
}
