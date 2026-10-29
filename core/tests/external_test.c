/*
 * external -- "Use external FujiNet": with the switch on, the session starts
 * no runtime of its own and the FujiNet card dials the given address. A
 * stand-in listener on a spare port plays the FujiNet and must see the
 * connection. Needs the ROMs (77 without; and 77 on Windows, which has no
 * BSD sockets here).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "h89session.h"
#include "test_tmpdir.h"
#include "test_util.h"

#ifdef _WIN32
int main(void)
{
    printf("external: SKIP: uses BSD sockets\n");
    return 77;
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    char home[1024], cfgdir[1100], datadir[1100], zip[1200], msg[256], what[256], addr[64];
    h89session_paths paths;
    h89session *session;
    h89session_start_opts opts;
    struct sockaddr_in sin;
    socklen_t sinlen = sizeof sin;
    int listener, conn = -1;

    test_tmpdir(home, sizeof home, "external");
    snprintf(cfgdir, sizeof cfgdir, "%s/config", home);
    snprintf(datadir, sizeof datadir, "%s/data", home);
    memset(&paths, 0, sizeof paths);
    paths.config_dir = cfgdir;
    paths.data_dir = datadir;
    paths.fujinet_lib = "";   /* no runtime at all: only the address can be dialled */
    session = h89session_new(&paths);
    CHECK(session != NULL);
    if (!session)
        return t_done("external");

    snprintf(zip, sizeof zip, "%s/h89.zip", t_rom_dir());
    h89session_import_roms(session, zip, msg, sizeof msg);
    snprintf(zip, sizeof zip, "%s/heath_tlb.zip", t_rom_dir());
    h89session_import_roms(session, zip, msg, sizeof msg);
    if (h89session_missing_roms(session, what, sizeof what) > 0) {
        printf("external: SKIP: the H89 ROMs are not in %s\n", t_rom_dir());
        h89session_free(session);
        return 77;
    }

    listener = socket(AF_INET, SOCK_STREAM, 0);
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (listener < 0 || bind(listener, (struct sockaddr *)&sin, sizeof sin) || listen(listener, 1) ||
        getsockname(listener, (struct sockaddr *)&sin, &sinlen))
        return 1;
    snprintf(addr, sizeof addr, "127.0.0.1:%d", ntohs(sin.sin_port));

    h89session_default_opts(session, &opts);
    opts.enable_audio = 0;
    opts.enable_fujinet = 1;
    opts.external_fujinet = 1;
    opts.fujinet_addr = "not an address";
    CHECK(h89session_start(session, &opts) != 0);
    CHECK(!h89session_is_running(session));

    opts.fujinet_addr = addr;
    CHECK(h89session_start(session, &opts) == 0);
    CHECK(h89session_fujinet_external(session) == 1);
    CHECK(h89session_fujinet_running(session) == 0);
    CHECK(strcmp(h89session_boip_endpoint(session), addr) == 0);
    CHECK(h89session_fujinet_webui_url(session)[0] == '\0');
    CHECK(h89session_fujinet_restart(session) != 0);

    {
        struct pollfd pfd = {listener, POLLIN, 0};
        if (poll(&pfd, 1, 15000) > 0)
            conn = accept(listener, NULL, NULL);
    }
    CHECK(conn >= 0);   /* the card dialled the stand-in FujiNet */
    if (conn >= 0)
        close(conn);

    h89session_stop(session);
    CHECK(h89session_fujinet_external(session) == 0);
    h89session_free(session);
    close(listener);
    return t_done("external");
}
#endif
