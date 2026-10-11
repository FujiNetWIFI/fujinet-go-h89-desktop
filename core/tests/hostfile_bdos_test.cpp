/*
 * hostfile_bdos -- CP/NET's host-folder server (VirtualH89's HostFileBdos)
 * through its message interface, as the CP/NET device hands it the
 * client's requests: select A:, make a file, write two records, close it;
 * the host file is there with those 256 bytes; open it again, read them
 * back, read past the end; find it by a wildcard search; delete it.
 * Runs on every platform (Windows CI included): the server's file I/O is
 * the part that is not plain POSIX there.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "HostFileBdos.h"
#include "MachineContext.h"
#include "NetworkServer.h"
#include "test_tmpdir.h"
#include "test_util.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

enum
{
    F_SELECT = 14,
    F_OPEN   = 15,
    F_CLOSE  = 16,
    F_SFIRST = 17,
    F_DELETE = 19,
    F_READ   = 20,
    F_WRITE  = 21,
    F_MAKE   = 22,
};

struct Client
{
    HostFileBdos& srv;
    uint8_t       buf[512];
    uint8_t*      msg = buf + sizeof(NetworkServer::ndos);

    /// One request: function `fn` with the message bytes already in msg[];
    /// returns the reply's length.
    int call(uint8_t fn, int len)
    {
        NetworkServer::ndos* h = (NetworkServer::ndos*) buf;
        h->mcode               = 0;
        h->mdid                = 0;
        h->msid                = 1;
        h->mfunc               = fn;
        h->msize               = (uint8_t) (len - 1);
        return srv.sendMsg(buf, (int) sizeof(NetworkServer::ndos) + len);
    }
};

/// "TEST    TXT" as an FCB (drive 0: the current one).
void
fcb(uint8_t* p, const char* name, const char* ext)
{
    memset(p, 0, 36);
    memset(p + 1, ' ', 11);
    memcpy(p + 1, name, strlen(name));
    memcpy(p + 9, ext, strlen(ext));
}

}

int
main()
{
    char root[1024];
    test_tmpdir(root, sizeof root, "cpnet");

    MachineContext        ctx;
    MachineContext::Scope scope(&ctx);

    PropertyUtil::PropertyMapT props;
    props["hostfilebdos_root_dir"] = root;
    HostFileBdos srv(props, {}, 0, 1);
    Client       c{srv};

    // select A:
    c.msg[0] = 0;
    c.call(F_SELECT, 1);
    CHECK_EQ_INT(c.msg[0], 0);

    // make TEST.TXT, write two records, close
    c.msg[0] = 0;
    fcb(c.msg + 1, "TEST", "TXT");
    CHECK_EQ_INT(c.call(F_MAKE, 37), 37);
    CHECK_EQ_INT(c.msg[0], 0);
    uint8_t f[36];
    memcpy(f, c.msg + 1, 36);
    std::vector<uint8_t> data(256);
    for (size_t i = 0; i < data.size(); ++i)
    {
        data[i] = (uint8_t) (i == 10 ? '\n' : i == 11 ? '\r' : i * 3);   // binary, with CR/LF
    }
    for (int rec = 0; rec < 2; ++rec)
    {
        c.msg[0] = 0;
        memcpy(c.msg + 1, f, 36);
        memcpy(c.msg + 37, &data[(size_t) rec * 128], 128);
        CHECK_EQ_INT(c.call(F_WRITE, 37 + 128), 37);
        CHECK_EQ_INT(c.msg[0], 0);
        memcpy(f, c.msg + 1, 36);
    }
    c.msg[0] = 0;
    memcpy(c.msg + 1, f, 36);
    c.call(F_CLOSE, 37);
    CHECK_EQ_INT(c.msg[0], 0);

    // the host file: 256 bytes as written (no text-mode translation)
    std::string found;
    for (auto& e : std::filesystem::recursive_directory_iterator(root))
    {
        std::string n = e.path().filename().string();
        for (char& ch : n)
        {
            ch = (char) tolower((unsigned char) ch);
        }
        if (n == "test.txt")
        {
            found = e.path().string();
        }
    }
    CHECK(!found.empty());
    if (!found.empty())
    {
        std::ifstream        in(found, std::ios::binary);
        std::vector<uint8_t> got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(got == data);
    }

    // open it again and read both records, then the end
    c.msg[0] = 0;
    fcb(c.msg + 1, "TEST", "TXT");
    CHECK_EQ_INT(c.call(F_OPEN, 37), 37);
    CHECK_EQ_INT(c.msg[0], 0);
    memcpy(f, c.msg + 1, 36);
    std::vector<uint8_t> back;
    for (int rec = 0; rec < 3; ++rec)
    {
        c.msg[0] = 0;
        memcpy(c.msg + 1, f, 36);
        int n = c.call(F_READ, 37);
        if (rec < 2)
        {
            CHECK_EQ_INT(c.msg[0], 0);
            CHECK(n >= 37 + 128);
            back.insert(back.end(), c.msg + 37, c.msg + 37 + 128);
            memcpy(f, c.msg + 1, 36);
        }
        else
        {
            CHECK_EQ_INT(c.msg[0], 1);   // end of file
        }
    }
    CHECK(back == data);
    c.msg[0] = 0;
    memcpy(c.msg + 1, f, 36);
    c.call(F_CLOSE, 37);

    // a wildcard search finds it
    c.msg[0] = 0;
    c.msg[1] = 0;
    fcb(c.msg + 2, "????????", "???");
    int n = c.call(F_SFIRST, 38);
    CHECK(n == 33 || n == 129);
    CHECK(c.msg[0] != 255);
    if (n == 33)
    {
        CHECK(memcmp(c.msg + 2, "TEST    TXT", 11) == 0);
    }

    // delete it: gone, and opening it fails
    c.msg[0] = 0;
    fcb(c.msg + 1, "TEST", "TXT");
    c.call(F_DELETE, 37);
    CHECK_EQ_INT(c.msg[0], 0);
    CHECK(!std::filesystem::exists(found));
    c.msg[0] = 0;
    fcb(c.msg + 1, "TEST", "TXT");
    c.call(F_OPEN, 37);
    CHECK_EQ_INT(c.msg[0], 255);

    return t_done("hostfile_bdos");
}
