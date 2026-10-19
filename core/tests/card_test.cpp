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

    // after /RESET the ESP32 is held in reset: no link, nothing dialled.
    // Every port is an input: port C reads its pins, and PC2 reads 1 (the
    // bus hold) although EN is low; port B reads FF.
    CHECK(card.link() == FujiNetCard::LinkEspReset);
    CHECK_EQ_INT(card.in(0xd2), 0x5c);
    CHECK_EQ_INT(card.in(0xd1), 0xff);

    // nothing answers outside mode 2: a byte written is never taken
    card.out(0xd0, 'R');
    card.notification(1024);
    CHECK_EQ_INT(card.in(0xd2) & 0x80, 0);
    CHECK_EQ_INT(card.state().bytesToEsp, 0);

    // Heath CP/M's BIOS sets up 320Q's 8250: 83H leaves PC0-PC3 inputs and
    // PC2 reads 0 from then on; PC4-PC7 are outputs, cleared
    card.out(0xd3, 0x83);
    CHECK_EQ_INT(card.in(0xd2), 0x08);
    card.out(0xd1, 0x5a);
    CHECK_EQ_INT(card.in(0xd1), 0xff);
    CHECK(card.link() == FujiNetCard::LinkEspReset);
    card.reset();
    CHECK_EQ_INT(card.in(0xd2), 0x5c);

    // portio.c's port_init(): mode 2 with EN and /OE (IO0) back up at once,
    // then the set-up mark in port B, which reads back
    card.out(0xd3, 0xc0);
    card.out(0xd2, 0x06);
    CHECK(card.link() == FujiNetCard::LinkWaiting || card.link() == FujiNetCard::LinkUp);
    CHECK_EQ_INT(card.in(0xd1), 0x00);
    card.out(0xd1, 0xa5);
    CHECK_EQ_INT(card.in(0xd1), 0xa5);

    // it dials FujiNet
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

    // BSR PC0 = 0, then the H89 -> ESP32 direction with the 245 enabled
    card.out(0xd3, 0x00);
    card.out(0xd3, 0x02);   // PC1 (/OE) = 0
    card.out(0xd3, 0x01);   // PC0 (DIR) = 1
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

    // bytes in: the 245 turned back, IBF rises per byte, port A takes it
    card.out(0xd3, 0x00);   // DIR = ESP32 -> H89
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

    // the 245 pointing the wrong way: the ESP32 still acks, nothing arrives
    card.out(0xd0, 'Q');
    CHECK(spin(card, [&] { return (card.in(0xd2) & 0x80) != 0; }));
    CHECK(card.state().txDropped == 1);
    card.notification(1024);
    CHECK(peerRead().empty());
    CHECK(g_traced[1].size() == 8);

    // bytes FujiNet sent that the H89 never took (the 245 points the other
    // way): EN falling throws them away, and the trace says so
    card.out(0xd3, 0x01);   // DIR = H89 -> ESP32
    send(g_peer, "LOST", 4, 0);
    CHECK(spin(card, [&] { return card.state().rxQueued == 4; }));

    // the INTE bits through bit set/reset
    card.out(0xd3, 0x09);   // PC4 -> INTE2
    card.out(0xd3, 0x0d);   // PC6 -> INTE1
    CHECK_EQ_INT(card.in(0xd2) & 0x50, 0x50);

    // EN low: the ESP32 is reset, FujiNet sees the connection close
    card.out(0xd3, 0x04);
    CHECK(card.link() == FujiNetCard::LinkEspReset);
    CHECK(g_traced[2] == "LOST");
    CHECK(g_traced[0] == "XYZ");
    bool closed = false;
    for (int i = 0; i < 40 && !closed; ++i)
    {
        char b;
        if (readable(g_peer, 25))
        {
            closed = recv(g_peer, &b, 1, 0) <= 0;
        }
    }
    CHECK(closed);
    sock_close(g_peer);
    g_peer = BAD_SOCK;

    // EN rising with IO0 (PC1) low: the ESP32's ROM loader -- no link, and
    // a byte written is never acknowledged
    card.out(0xd3, 0x02);   // PC1 = 0
    card.out(0xd3, 0x05);   // PC2 = 1
    CHECK(card.link() == FujiNetCard::LinkEspDownload);
    card.out(0xd0, 'Z');
    for (int i = 0; i < 50; ++i)
    {
        card.notification(1024);
    }
    CHECK((card.in(0xd2) & 0x80) == 0);

    // a mode word clears it all, the mark in port B too
    card.out(0xd3, 0xc0);
    CHECK_EQ_INT(card.in(0xd2), 0x80);
    CHECK(card.link() == FujiNetCard::LinkEspReset);
    CHECK_EQ_INT(card.in(0xd1), 0x00);

    // the control register reads as floating
    CHECK_EQ_INT(card.in(0xd3), 0xff);

    FujiNetCard::unsetTrace(g_traced);
    sock_close(g_listen);
    return t_done("card");
}
