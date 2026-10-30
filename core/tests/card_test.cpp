/*
 * card -- the FujiNet card's 82C55 (core/machine/FujiNetCard) against a
 * loopback TCP peer standing in for FujiNet's BoIP listener: the sequence
 * fujinet-lib's bus/h89/portio.c drives, byte for byte, and the bytes the
 * debugger's FujiNet trace is handed.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define sock_close closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define sock_close close
#endif

#include "FujiNetCard.h"
#include "MachineContext.h"
#include "test_util.h"

#include <chrono>
#include <string>
#include <thread>

namespace
{

sock_t g_listen = BAD_SOCK, g_peer = BAD_SOCK;

// what the debugger's FujiNet trace was handed, by dir
std::string g_traced[3];

void
onTrace(void* user, int dir, const uint8_t* buf, size_t len)
{
    (void) user;
    if (dir >= 0 && dir < 3)
    {
        g_traced[dir].append((const char*) buf, len);
    }
}

int
listenAny()
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    g_listen = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = 0;
    if (bind(g_listen, (sockaddr*) &a, sizeof a) != 0 || listen(g_listen, 1) != 0)
    {
        return -1;
    }
    socklen_t len = sizeof a;
    getsockname(g_listen, (sockaddr*) &a, &len);
    return ntohs(a.sin_port);
}

bool
readable(sock_t s, int ms)
{
    fd_set r;
    FD_ZERO(&r);
    FD_SET(s, &r);
    timeval tv{0, ms * 1000};
    return select((int) s + 1, &r, nullptr, nullptr, &tv) > 0;
}

/// Runs the card's clock until `done` or about a second of real time.
template <class F>
bool
spin(FujiNetCard& card, F done)
{
    for (int i = 0; i < 2000; ++i)
    {
        card.notification(64);
        if (done())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    return false;
}

std::string
peerRead()
{
    std::string out;
    while (readable(g_peer, 50))
    {
        char buf[256];
        int  n = (int) recv(g_peer, buf, sizeof buf, 0);
        if (n <= 0)
        {
            break;
        }
        out.append(buf, (size_t) n);
    }
    return out;
}

}

int
main()
{
    int port = listenAny();
    CHECK(port > 0);

    MachineContext        ctx;
    MachineContext::Scope scope(&ctx);
    FujiNetCard           card("127.0.0.1", port, 40);
    FujiNetCard::setTrace(onTrace, g_traced);

    // after /RESET every port is an input: port C reads its pins (PC0-PC2,
    // /OBF and IBF held at 1 by the bus hold, INTR at 0 by Q1), port B
    // reads FF and its lamps are dark. The ESP32 runs from power-on, so the
    // card dials FujiNet at once, whatever the 82C55A is doing.
    CHECK_EQ_INT(card.in(0xd2), 0xf7);
    CHECK_EQ_INT(card.in(0xd1), 0xff);
    CHECK_EQ_INT(card.state().lamps, 0x00);
    bool accepted = spin(card, [&] {
        if (g_peer == BAD_SOCK && readable(g_listen, 0))
        {
            g_peer = accept(g_listen, nullptr, nullptr);
        }
        return g_peer != BAD_SOCK && card.link() == FujiNetCard::LinkUp;
    });
    CHECK(accepted);
    if (!accepted)
    {
        return t_done("card");
    }

    // nothing answers outside mode 2: a byte written is never taken
    card.out(0xd0, 'R');
    card.notification(1024);
    CHECK_EQ_INT(card.in(0xd2) & 0x80, 0x80);
    CHECK_EQ_INT(card.state().bytesToEsp, 0);

    // Heath CP/M's BIOS sets up 320Q's 8250: 83H leaves PC0-PC3 inputs and
    // makes PC4-PC7 outputs, cleared; port B is an input
    card.out(0xd3, 0x83);
    CHECK_EQ_INT(card.in(0xd2), 0x07);
    card.out(0xd1, 0x5a);
    CHECK_EQ_INT(card.in(0xd1), 0xff);
    card.reset();
    CHECK_EQ_INT(card.in(0xd2), 0xf7);

    // the book's card_on: one mode word leaves PC0-PC2 at 0, and port B an
    // output at 00, every lamp lit
    card.out(0xd3, 0xc0);
    CHECK_EQ_INT(card.in(0xd2), 0x80);
    CHECK_EQ_INT(card.in(0xd1), 0x00);
    CHECK_EQ_INT(card.state().lamps, 0xff);

    // an older port_init() raises PC1 and PC2 and leaves A5H in port B:
    // neither line goes anywhere, and the mark reads back (four lamps lit)
    card.out(0xd2, 0x06);
    CHECK_EQ_INT(card.in(0xd2) & 0x07, 0x06);
    card.out(0xd1, 0xa5);
    CHECK_EQ_INT(card.in(0xd1), 0xa5);
    CHECK_EQ_INT(card.state().lamps, 0x5a);
    CHECK(card.link() == FujiNetCard::LinkUp);

    // an older program still steers the 245 the card no longer has, with
    // BSR: PC0 = 0, PC1 (/OE) = 0, PC0 (DIR) = 1. The bits read back and
    // steer nothing
    card.out(0xd3, 0x00);
    card.out(0xd3, 0x02);
    card.out(0xd3, 0x01);
    CHECK_EQ_INT(card.in(0xd2) & 0x07, 0x05);

    // a byte out: /OBF falls, rises when the ESP32 has it; it arrives
    const char* frame = "\xc0\x70\x01\x06\x00\x77\x00\xc0";
    for (int i = 0; i < 8; ++i)
    {
        card.out(0xd0, (BYTE) frame[i]);
        CHECK((card.in(0xd2) & 0x80) == 0);
        CHECK(spin(card, [&] { return (card.in(0xd2) & 0x80) != 0; }));
    }
    std::string got;
    for (int i = 0; i < 20 && got.size() < 8; ++i)
    {
        card.notification(1024);
        got += peerRead();
    }
    CHECK(got == std::string(frame, 8));
    CHECK(g_traced[1] == std::string(frame, 8));

    // bytes in, with PC0 turned back as an older program does: IBF rises
    // per byte, port A takes it
    card.out(0xd3, 0x00);
    send(g_peer, "XYZ", 3, 0);
    std::string in;
    for (int i = 0; i < 3; ++i)
    {
        CHECK(spin(card, [&] { return (card.in(0xd2) & 0x20) != 0; }));
        in += (char) card.in(0xd0);
    }
    CHECK(in == "XYZ");
    CHECK(g_traced[0] == "XYZ");
    CHECK((card.in(0xd2) & 0x20) == 0);

    // port C steers nothing: with PC0 = 0 a byte written still reaches
    // FujiNet, and with PC0 = 1 one still comes in
    card.out(0xd0, 'Q');
    CHECK(spin(card, [&] { return (card.in(0xd2) & 0x80) != 0; }));
    std::string out;
    for (int i = 0; i < 20 && out.empty(); ++i)
    {
        card.notification(1024);
        out += peerRead();
    }
    CHECK(out == "Q");
    CHECK(g_traced[1] == std::string(frame, 8) + "Q");
    card.out(0xd3, 0x01);   // PC0 = 1
    send(g_peer, "P", 1, 0);
    CHECK(spin(card, [&] { return (card.in(0xd2) & 0x20) != 0; }));
    CHECK_EQ_INT(card.in(0xd0), 'P');
    CHECK(g_traced[0] == "XYZP");
    card.out(0xd3, 0x00);   // PC0 = 0

    // the INTE bits through bit set/reset
    card.out(0xd3, 0x09);   // PC4 -> INTE2
    card.out(0xd3, 0x0d);   // PC6 -> INTE1
    CHECK_EQ_INT(card.in(0xd2) & 0x50, 0x50);

    // neither PC2 nor a mode word touches the ESP32: the link stays up
    card.out(0xd3, 0x04);   // PC2 = 0
    card.out(0xd3, 0x05);   // PC2 = 1
    card.out(0xd3, 0xc0);
    CHECK_EQ_INT(card.in(0xd2), 0x80);
    CHECK_EQ_INT(card.in(0xd1), 0x00);
    for (int i = 0; i < 20; ++i)
    {
        card.notification(1024);
    }
    CHECK(card.link() == FujiNetCard::LinkUp);
    CHECK(!readable(g_peer, 25));

    // the H89's RESET reaches only the 82C55A: the ESP32 stays connected,
    // but a reply it had in flight (here held back by its first byte, left
    // unread in port A) is lost
    send(g_peer, "XLOST", 5, 0);
    CHECK(spin(card, [&] { return card.state().rxQueued == 4; }));
    CHECK((card.in(0xd2) & 0x20) != 0);
    card.reset();
    CHECK(card.link() == FujiNetCard::LinkUp);
    CHECK_EQ_INT(card.state().rxQueued, 0);
    CHECK(g_traced[2] == "LOST");
    CHECK(g_traced[0] == "XYZPX");
    card.out(0xd3, 0xc0);
    for (int i = 0; i < 20; ++i)
    {
        card.notification(1024);
    }
    CHECK((card.in(0xd2) & 0x20) == 0);
    send(g_peer, "OK", 2, 0);
    std::string after;
    for (int i = 0; i < 2; ++i)
    {
        CHECK(spin(card, [&] { return (card.in(0xd2) & 0x20) != 0; }));
        after += (char) card.in(0xd0);
    }
    CHECK(after == "OK");
    CHECK(!readable(g_peer, 25));
    CHECK_EQ_INT(card.state().connects, 1);
    CHECK_EQ_INT(card.state().drops, 0);

    // the control register reads as floating
    CHECK_EQ_INT(card.in(0xd3), 0xff);

    FujiNetCard::unsetTrace(g_traced);
    sock_close(g_listen);
    return t_done("card");
}
