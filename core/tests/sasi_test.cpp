/*
 * sasi -- the MMS 77320 SASI controller and its Xebec drives (VirtualH89's
 * MMS77320 + GenericSASIDrive) through the card's ports, as a driver does:
 * select the unit, send a 6-byte command, move a block in the data phase,
 * read the status and message bytes. A fresh unit file is created at the
 * drive's capacity with its geometry trailer; a block written is in the
 * file, at its place, and reads back -- also from a new controller on the
 * same file (the app's data/sasi folder across runs).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "GenericSASIDrive.h"
#include "MachineContext.h"
#include "computer.h"
#include "mms77320.h"
#include "test_tmpdir.h"
#include "test_util.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

/// The card raises and lowers its interrupt through the machine.
struct NoComputer: public Computer
{
    int  level = 0;
    void raiseINT(int l) override
    {
        level = l;
    }
    void lowerINT(int) override
    {
        level = 0;
    }
    void raiseNMI() override
    {
    }
    void continueCPU() override
    {
    }
    void waitCPU() override
    {
    }
    void writeProtectH17RAM() override
    {
    }
    void writeEnableH17RAM() override
    {
    }
    AddressBus& getAddressBus() override
    {
        return *(AddressBus*) nullptr;
    }
};

const BYTE BASE = 0x7c;
const BYTE REQ = 0x80, POUT = 0x40, MSG = 0x20, CMD = 0x10, BSY = 0x08;

struct Bus
{
    MMS77320 card{BASE, 5, 0};

    BYTE status()
    {
        return card.in(BASE + 1);
    }
    /// Selects `unit`: the drive answers BUSY, then asks for a command.
    bool select(int unit)
    {
        card.out(BASE + 2, (BYTE) unit);
        card.out(BASE + 1, 0x40);
        card.out(BASE + 1, 0x00);
        return (status() & (REQ | CMD | POUT | BSY)) == (REQ | CMD | POUT | BSY);
    }
    /// One command; `out` is written in the data-out phase, `in` filled in
    /// the data-in phase. Returns the status byte, or -1 on a phase error.
    int command(const std::vector<BYTE>& cdb, const std::vector<BYTE>* out,
                std::vector<BYTE>* in)
    {
        for (BYTE b : cdb)
        {
            if ((status() & (REQ | CMD | POUT)) != (REQ | CMD | POUT))
            {
                return -1;
            }
            card.out(BASE, b);
        }
        size_t at = 0;
        // data phase until the drive turns to status (CMD, in)
        for (int guard = 0; guard < 100000; ++guard)
        {
            BYTE s = status();
            if (!(s & REQ))
            {
                return -1;
            }
            if (s & CMD)
            {
                break;
            }
            if (s & POUT)
            {
                card.out(BASE, out && at < out->size() ? (*out)[at] : 0);
                ++at;
            }
            else
            {
                BYTE b = card.in(BASE);
                if (in)
                {
                    in->push_back(b);
                }
            }
        }
        if ((status() & (REQ | CMD | POUT | MSG)) != (REQ | CMD))
        {
            return -1;
        }
        int st = card.in(BASE);
        if ((status() & (REQ | CMD | MSG)) != (REQ | CMD | MSG))
        {
            return -1;
        }
        card.in(BASE);   // the message byte
        return st;
    }
};

std::vector<BYTE>
readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<BYTE>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}

int
main()
{
    char dir[1024];
    test_tmpdir(dir, sizeof dir, "sasi");
    std::string unit0 = std::string(dir) + "/MMS77320-0";

    MachineContext        ctx;
    NoComputer            computer;
    ctx.computer = &computer;
    MachineContext::Scope scope(&ctx);

    std::vector<BYTE> block(512);   // the Xebec's 512-byte sectors
    for (size_t i = 0; i < block.size(); ++i)
    {
        block[i] = (BYTE) (i ^ 0x5a);
    }

    {
        Bus               bus;
        GenericSASIDrive* d = GenericSASIDrive::getInstance("XEBEC_ST506", unit0, 0);
        CHECK(d != nullptr);
        if (!d)
        {
            return t_done("sasi");
        }
        bus.card.connectDrive(0, d);

        // a new unit file: the whole drive plus the geometry trailer
        std::vector<BYTE> f = readFile(unit0);
        CHECK(f.size() > 128 && f.size() % 512 == 128);
        CHECK(f.size() > 128 && strchr((const char*) &f[f.size() - 128], 'c') != nullptr);

        // TEST DRIVE READY
        CHECK(bus.select(0));
        CHECK_EQ_INT(bus.command({0x00, 0, 0, 0, 0, 0}, nullptr, nullptr), 0);

        // WRITE block 5, then READ it
        CHECK(bus.select(0));
        CHECK_EQ_INT(bus.command({0x0a, 0, 0, 5, 1, 0}, &block, nullptr), 0);
        std::vector<BYTE> got;
        CHECK(bus.select(0));
        CHECK_EQ_INT(bus.command({0x08, 0, 0, 5, 1, 0}, nullptr, &got), 0);
        CHECK(got == block);

        // past the end: an error status, and REQUEST SENSE says why (0x21)
        CHECK(bus.select(0));
        CHECK_EQ_INT(bus.command({0x08, 0x1f, 0xff, 0xff, 1, 0}, nullptr, nullptr) & 0x02, 0x02);
        got.clear();
        CHECK(bus.select(0));
        CHECK_EQ_INT(bus.command({0x03, 0, 0, 0, 4, 0}, nullptr, &got), 0);
        CHECK(!got.empty() && (got[0] & 0x7f) == 0x21);

        // no unit 3: selecting it gets no BUSY
        CHECK(!bus.select(3));
    }   // (the controller owns its drives)

    // the block is in the file at its place, and a new controller reads it
    std::vector<BYTE> f = readFile(unit0);
    CHECK(f.size() >= 6 * 512 && std::equal(block.begin(), block.end(), f.begin() + 5 * 512));
    {
        Bus               bus;
        GenericSASIDrive* d = GenericSASIDrive::getInstance("XEBEC_ST506", unit0, 0);
        bus.card.connectDrive(0, d);
        std::vector<BYTE> got;
        CHECK(bus.select(0));
        CHECK_EQ_INT(bus.command({0x08, 0, 0, 5, 1, 0}, nullptr, &got), 0);
        CHECK(got == block);
    }
    return t_done("sasi");
}
