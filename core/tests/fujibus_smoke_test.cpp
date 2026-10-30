/*
 * fujibus_smoke -- the FujiNet card against the real FujiNet: the session
 * starts the in-process runtime (libfujinet, the RS232 bus listening for
 * BoIP on 127.0.0.1:11512), the card dials it, and the H89's side of one
 * FujiBus exchange is driven through the 82C55 exactly as fujinet-lib's
 * bus/h89 does it -- portio.c's port_init, a SLIP-framed GET ADAPTER CONFIG
 * byte by byte with the /OBF-/ACK handshake, then the reply byte by byte
 * through IBF. The reply must be an ACK from the FujiNet device with a
 * correct checksum and the adapter configuration behind it, and the
 * debugger's FujiNet trace must have seen both.
 *
 * No Heath ROMs and no CPU: the steps are the port reads and writes the
 * Z80 code makes. Exits 77 (skipped) when the runtime is not built.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "FujiNetCard.h"
#include "MachineContext.h"
#include "h89debug.h"
#include "session_internal.h"
#include "test_tmpdir.h"
#include "test_util.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace
{

enum
{
    SLIP_END     = 0xc0,
    SLIP_ESCAPE  = 0xdb,
    SLIP_ESC_END = 0xdc,
    SLIP_ESC_ESC = 0xdd,
    FUJI_DEVICE  = 0x70,
    CMD_GET_ADAPTER_CONFIG = 0xe8,
    PACKET_ACK   = 6,
};

/// fujinet-lib's fuji_calc_checksum: an end-around-carry byte sum.
unsigned
checksum(const std::vector<BYTE>& b, unsigned seed = 0)
{
    unsigned chk = seed;
    for (BYTE c : b)
    {
        chk = ((chk + c) >> 8) + ((chk + c) & 0xff);
    }
    return chk & 0xff;
}

/// Runs the card's clock until `done`, or `ms` of real time.
template <class F>
bool
spin(FujiNetCard& card, int ms, F done)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end)
    {
        card.notification(64);
        if (done())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return false;
}

/// port_putc: write port A, wait out /OBF.
bool
putByte(FujiNetCard& card, BYTE b)
{
    card.out(0xd0, b);
    return spin(card, 2000, [&] { return (card.in(0xd2) & 0x80) != 0; });
}

/// port_getc_timeout: wait for IBF, read port A.
int
getByte(FujiNetCard& card, int ms)
{
    if (!spin(card, ms, [&] { return (card.in(0xd2) & 0x20) != 0; }))
    {
        return -1;
    }
    return card.in(0xd0);
}

}

int
main()
{
    char home[1024], cfgdir[1100], datadir[1100];
    test_tmpdir(home, sizeof home, "fujibus");
    snprintf(cfgdir, sizeof cfgdir, "%s/config", home);
    snprintf(datadir, sizeof datadir, "%s/data", home);

    h89session_paths paths{};
    paths.config_dir = cfgdir;
    paths.data_dir   = datadir;
    h89session* s    = h89session_new(&paths);
    CHECK(s != nullptr);
    if (!s)
    {
        return t_done("fujibus_smoke");
    }
    if (fujinet_start(s) != 0)
    {
        printf("fujibus_smoke: SKIP: no FujiNet runtime (%s)\n", h89session_last_error(s));
        h89session_free(s);
        return 77;
    }
    CHECK(fujinet_wait_for_boip(s, 5000) == 0);
    h89debug_fujinet_hook(s);

    {
        MachineContext        ctx;
        MachineContext::Scope scope(&ctx);
        FujiNetCard           card("127.0.0.1", H89SESSION_BOIP_PORT, 40);

        // portio.c's port_init: port A in mode 2, port B an input
        card.out(0xd3, 0xc2);
        CHECK(spin(card, 5000, [&] { return card.link() == FujiNetCard::LinkUp; }));

        // the request: header {device, command, length, checksum, fields}
        std::vector<BYTE> req = {FUJI_DEVICE, CMD_GET_ADAPTER_CONFIG, 6, 0, 0, 0};
        req[4]                = (BYTE) checksum(req);
        bool sent             = putByte(card, SLIP_END);
        for (BYTE b : req)
        {
            if (b == SLIP_END || b == SLIP_ESCAPE)
            {
                sent = sent && putByte(card, SLIP_ESCAPE);
                b    = b == SLIP_END ? SLIP_ESC_END : SLIP_ESC_ESC;
            }
            sent = sent && putByte(card, b);
        }
        sent = sent && putByte(card, SLIP_END);
        CHECK(sent);

        // the reply, unframed as port_getbuf_slip_dual does
        std::vector<BYTE> reply;
        int               c;
        do
        {
            c = getByte(card, 5000);
        } while (c >= 0 && c != SLIP_END);
        do
        {
            c = getByte(card, 2000);
        } while (c == SLIP_END);
        while (c >= 0 && c != SLIP_END)
        {
            if (c == SLIP_ESCAPE)
            {
                c = getByte(card, 2000);
                c = c == SLIP_ESC_END ? SLIP_END : c == SLIP_ESC_ESC ? SLIP_ESCAPE : c;
            }
            reply.push_back((BYTE) c);
            c = getByte(card, 2000);
        }
        CHECK(c == SLIP_END);
        CHECK(reply.size() > 6);
        if (reply.size() > 6)
        {
            unsigned len = reply[2] | (reply[3] << 8);
            CHECK_EQ_INT(reply[0], FUJI_DEVICE);
            CHECK_EQ_INT(reply[1], PACKET_ACK);
            CHECK_EQ_INT((int) len, (int) reply.size());
            BYTE got  = reply[4];
            reply[4] = 0;
            CHECK_EQ_INT(got, (int) checksum(reply));
            printf("fujibus_smoke: GET ADAPTER CONFIG -> %u bytes; hostname \"%.64s\"\n",
                   len - 6, reply.size() > 6 + 33 ? (const char*) &reply[6 + 33] : "");
        }
        FujiNetCard::State st = card.state();
        printf("fujibus_smoke: %llu bytes to FujiNet, %llu back\n", st.bytesToEsp, st.bytesFromEsp);

        // the trace: the request and its reply, SLIP-decoded
        h89_fuji_packet tr[8];
        const int       n       = h89debug_fujinet_trace(h89session_debugger(s), tr, 8);
        bool            request = false, ack = false;
        for (int i = 0; i < n; ++i)
        {
            printf("fujibus_smoke: trace: %s\n", tr[i].text);
            request = request || (!tr[i].to_host && tr[i].command == CMD_GET_ADAPTER_CONFIG);
            ack     = ack || (tr[i].to_host && tr[i].command == PACKET_ACK && tr[i].nbytes == (int) reply.size());
        }
        CHECK(request);
        CHECK(ack);
    }

    h89session_free(s);
    return t_done("fujibus_smoke");
}
