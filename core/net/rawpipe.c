/*
 * rawpipe -- see rawpipe.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define sock_close closesocket
#define sock_errno WSAGetLastError()
#define SOCK_WOULDBLOCK(e) ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS || (e) == WSAEALREADY)
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define sock_close close
#define sock_errno errno
#define SOCK_WOULDBLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK || (e) == EINPROGRESS || (e) == EALREADY)
#endif

#include "rawpipe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

typedef struct {
    uint8_t buf[RAWPIPE_RING];
    size_t head, count;
} ring;

struct rawpipe {
    char host[128];
    int port;
    sock_t fd;
    rawpipe_state state;
    uint64_t next_try_ms;
    ring tx, rx;
    rawpipe_stats stats;
};

static uint64_t now_ms(void)
{
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

static int ring_put(ring *r, uint8_t b)
{
    if (r->count == RAWPIPE_RING)
        return 0;
    r->buf[(r->head + r->count) % RAWPIPE_RING] = b;
    r->count++;
    return 1;
}

static int ring_get(ring *r, uint8_t *b)
{
    if (!r->count)
        return 0;
    *b = r->buf[r->head];
    r->head = (r->head + 1) % RAWPIPE_RING;
    r->count--;
    return 1;
}

static void ring_clear(ring *r)
{
    r->head = r->count = 0;
}

static int set_nonblocking(sock_t fd)
{
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(fd, FIONBIO, &on) == 0 ? 0 : -1;
#else
    int fl = fcntl(fd, F_GETFL, 0);
    return (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) ? -1 : 0;
#endif
}

static void drop(rawpipe *p)
{
    if (p->fd != BAD_SOCK)
    {
        sock_close(p->fd);
        p->fd = BAD_SOCK;
        if (p->state == RAWPIPE_UP)
            p->stats.drops++;
    }
    p->state = RAWPIPE_DOWN;
    p->next_try_ms = now_ms() + RAWPIPE_RETRY_MS;
}

rawpipe *rawpipe_new(const char *host, int port)
{
    rawpipe *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
#ifdef _WIN32
    {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
#endif
    snprintf(p->host, sizeof p->host, "%s", host ? host : "127.0.0.1");
    p->port = port;
    p->fd = BAD_SOCK;
    p->state = RAWPIPE_DOWN;
    return p;
}

void rawpipe_free(rawpipe *p)
{
    if (!p)
        return;
    if (p->fd != BAD_SOCK)
        sock_close(p->fd);
#ifdef _WIN32
    WSACleanup();
#endif
    free(p);
}

static void start_connect(rawpipe *p)
{
    struct addrinfo hints, *res = NULL, *ai;
    char port[16];

    p->next_try_ms = now_ms() + RAWPIPE_RETRY_MS;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port, sizeof port, "%d", p->port);
    if (getaddrinfo(p->host, port, &hints, &res) != 0 || !res)
        return;

    for (ai = res; ai; ai = ai->ai_next)
    {
        sock_t fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        int one = 1;
        if (fd == BAD_SOCK)
            continue;
        if (set_nonblocking(fd) != 0)
        {
            sock_close(fd);
            continue;
        }
        /* a FujiBus round trip is a handful of tiny writes; Nagle would add
         * a delayed-ACK wait to every one */
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
        if (connect(fd, ai->ai_addr, (int)ai->ai_addrlen) == 0)
        {
            p->fd = fd;
            p->state = RAWPIPE_UP;
            p->stats.connects++;
            break;
        }
        if (SOCK_WOULDBLOCK(sock_errno))
        {
            p->fd = fd;
            p->state = RAWPIPE_CONNECTING;
            break;
        }
        sock_close(fd);
    }
    freeaddrinfo(res);
}

static void check_connect(rawpipe *p)
{
    fd_set wr, ex;
    struct timeval tv = {0, 0};
    int err = 0;
    socklen_t len = sizeof err;

    FD_ZERO(&wr);
    FD_ZERO(&ex);
    FD_SET(p->fd, &wr);
    FD_SET(p->fd, &ex);
    if (select((int)p->fd + 1, NULL, &wr, &ex, &tv) <= 0)
    {
        if (now_ms() > p->next_try_ms + 3000)
            drop(p);  /* a connect that never completes */
        return;
    }
    if (FD_ISSET(p->fd, &ex) ||
        getsockopt(p->fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0 || err != 0)
    {
        drop(p);
        return;
    }
    p->state = RAWPIPE_UP;
    p->stats.connects++;
}

static void pump(rawpipe *p)
{
    uint8_t buf[1024];

    /* out: as much of the tx ring as the socket takes */
    while (p->tx.count)
    {
        size_t n = p->tx.count, first = RAWPIPE_RING - p->tx.head;
        int sent;
        if (n > first)
            n = first;
        sent = (int)send(p->fd, (const char *)&p->tx.buf[p->tx.head], (int)n, MSG_NOSIGNAL);
        if (sent > 0)
        {
            p->tx.head = (p->tx.head + (size_t)sent) % RAWPIPE_RING;
            p->tx.count -= (size_t)sent;
            p->stats.bytes_out += (uint64_t)sent;
            continue;
        }
        if (sent < 0 && SOCK_WOULDBLOCK(sock_errno))
            break;
        drop(p);
        return;
    }

    /* in: whatever there is room for */
    while (p->rx.count < RAWPIPE_RING)
    {
        size_t room = RAWPIPE_RING - p->rx.count;
        int got = (int)recv(p->fd, (char *)buf, (int)(room < sizeof buf ? room : sizeof buf), 0);
        if (got > 0)
        {
            for (int i = 0; i < got; i++)
                ring_put(&p->rx, buf[i]);
            p->stats.bytes_in += (uint64_t)got;
            continue;
        }
        if (got < 0 && SOCK_WOULDBLOCK(sock_errno))
            break;
        drop(p);   /* 0: the peer closed; <0: an error */
        return;
    }
}

rawpipe_state rawpipe_poll(rawpipe *p)
{
    if (!p)
        return RAWPIPE_DOWN;
    if (p->state == RAWPIPE_DOWN && now_ms() >= p->next_try_ms)
        start_connect(p);
    if (p->state == RAWPIPE_CONNECTING)
        check_connect(p);
    if (p->state == RAWPIPE_UP)
        pump(p);
    return p->state;
}

rawpipe_state rawpipe_state_of(const rawpipe *p)
{
    return p ? p->state : RAWPIPE_DOWN;
}

int rawpipe_put(rawpipe *p, uint8_t b)
{
    return p ? ring_put(&p->tx, b) : 0;
}

size_t rawpipe_tx_free(const rawpipe *p)
{
    return p ? RAWPIPE_RING - p->tx.count : 0;
}

int rawpipe_get(rawpipe *p, uint8_t *b)
{
    return p ? ring_get(&p->rx, b) : 0;
}

size_t rawpipe_rx_avail(const rawpipe *p)
{
    return p ? p->rx.count : 0;
}

void rawpipe_reset(rawpipe *p)
{
    if (!p)
        return;
    drop(p);
    ring_clear(&p->tx);
    ring_clear(&p->rx);
    p->next_try_ms = 0;   /* reconnect at the next poll */
}

void rawpipe_get_stats(const rawpipe *p, rawpipe_stats *out)
{
    if (p)
        *out = p->stats;
    else
        memset(out, 0, sizeof *out);
}
