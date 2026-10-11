/*
 * machine -- the H89 machine graph (core/machine/Machine) with test
 * programs of our own in place of a monitor ROM, so no Heath ROM is needed:
 * the 2 ms timer interrupt, the console 8250, ORG-0 and write-under-ROM,
 * the H88's map, the MMS 77318's unlock sequence and banks, the combined
 * interrupt controller, /RESET, and power-cycling (two machines in turn).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "GppListener.h"
#include "H19Terminal.h"
#include "IntrController.h"
#include "Machine.h"
#include "MemoryDecoder.h"
#include "h89-io.h"
#include "h89-timer.h"
#include "test_util.h"
#include "z80.h"

#include <stdexcept>
#include <vector>

namespace
{

/// A 4K "monitor": the timer handler at RST 08H, and at 0x0040 a program
/// that counts timer interrupts into 0x9000 and prints "OK" on the console.
std::vector<uint8_t>
timerRom()
{
    std::vector<uint8_t> rom(4096, 0);
    auto put = [&](unsigned at, std::initializer_list<uint8_t> b) {
        for (uint8_t x : b)
        {
            rom[at++] = x;
        }
    };
    put(0x0000, {0xf3, 0xc3, 0x40, 0x00});                  // DI; JP 0040
    put(0x0008, {0xf5,                                      // PUSH AF
                 0x3a, 0x00, 0x90, 0x3c, 0x32, 0x00, 0x90,  // (9000)++
                 0x3e, 0x02, 0xd3, 0xf2,                    // OUT (F2),2: clears it
                 0xf1, 0xfb, 0xc9});                        // POP AF; EI; RET
    put(0x0040, {0x31, 0x00, 0x80,                          // LD SP,8000
                 0xaf, 0x32, 0x00, 0x90,                    // (9000) = 0
                 // the console: 9600 baud, 8N1
                 0x3e, 0x80, 0xd3, 0xeb, 0x3e, 0x0c, 0xd3, 0xe8, 0xaf, 0xd3, 0xe9,
                 0x3e, 0x03, 0xd3, 0xeb,
                 0x3e, 'O', 0xd3, 0xe8,                     // OUT 'O'
                 0xdb, 0xed, 0xe6, 0x20, 0x28, 0xfa,        // wait THRE
                 0x3e, 'K', 0xd3, 0xe8,                     // OUT 'K'
                 0x3e, 0x02, 0xd3, 0xf2,                    // timer on
                 0xfb,                                      // EI
                 0x76, 0x18, 0xfd});                        // HALT; JR -3
    return rom;
}

h89_config
bareConfig()
{
    h89_config c;
    h89_config_default(&c);
    for (int& s : c.slot)
    {
        s = H89_CARD_EMPTY;
    }
    snprintf(c.monitor, sizeof c.monitor, "test");
    return c;
}

void
testTimerAndConsole()
{
    std::vector<uint8_t> rom = timerRom();
    h89_config           c   = bareConfig();
    MachineRoms          roms;
    roms.monitor     = rom.data();
    roms.monitorSize = rom.size();

    Machine               m(c, roms, MachineOptions());
    MachineContext::Scope s(&m.context());
    for (int i = 0; i < 101; ++i)
    {
        m.runSlice();
    }
    int ticks = m.memory().readByte(0x9000);
    printf("  %d timer interrupts in 100 slices\n", ticks);
    CHECK(ticks >= 98 && ticks <= 101);
    CHECK(m.terminal().screenText(false).rfind("OK", 0) == 0);
    CHECK(m.timer().interruptEnabled());

    // SHIFT+RESET: the CPU restarts at 0, the timer interrupt is off
    m.reset();
    CHECK(!m.timer().interruptEnabled());
    Z80::Registers r;
    m.cpu().getRegisters(r);
    CHECK_EQ_INT(r.pc, 0);
    CHECK(m.terminal().screenText(false).empty());
}

void
testOrg0()
{
    std::vector<uint8_t> rom(4096, 0xa5);
    h89_config           c = bareConfig();
    MachineRoms          roms;
    roms.monitor     = rom.data();
    roms.monitorSize = rom.size();

    Machine               m(c, roms, MachineOptions());
    MachineContext::Scope s(&m.context());

    // ROM mode: 0x0100 reads the ROM; a write there lands in the RAM under it
    CHECK_EQ_INT(m.memory().readByte(0x0100), 0xa5);
    m.memory().writeByte(0x0100, 0x3c);
    CHECK_EQ_INT(m.memory().readByte(0x0100), 0xa5);
    // ORG-0 (GPP bit 5): RAM at 0, holding what was written under the ROM
    m.io().out(0xf2, 0x20);
    CHECK_EQ_INT(m.memory().getCurrentLayoutNum(), 1);
    CHECK_EQ_INT(m.memory().readByte(0x0100), 0x3c);
    m.io().out(0xf2, 0x00);
    CHECK_EQ_INT(m.memory().readByte(0x0100), 0xa5);
    // the GPP reads back SW501
    CHECK_EQ_INT(m.io().in(0xf2), h89_config_effective_sw501(&c));
}

void
testH88()
{
    std::vector<uint8_t> rom(2048, 0x11);
    h89_config           c = bareConfig();
    c.model                = H89_MODEL_H88;
    c.memory               = H89_MEM_32K;
    MachineRoms roms;
    roms.monitor     = rom.data();
    roms.monitorSize = rom.size();

    Machine               m(c, roms, MachineOptions());
    MachineContext::Scope s(&m.context());
    // 32K above the ROM page: 0x2000-0x9FFF is RAM, 0xA000 up is not there
    m.memory().writeByte(0x9fff, 0x5a);
    CHECK_EQ_INT(m.memory().readByte(0x9fff), 0x5a);
    m.memory().writeByte(0xa000, 0x5a);
    CHECK_EQ_INT(m.memory().readByte(0xa000), 0x00);
    // no ORG-0
    m.io().out(0xf2, 0x20);
    CHECK_EQ_INT(m.memory().readByte(0x0000), 0x11);
}

void
testMms77318()
{
    std::vector<uint8_t> rom(4096, 0);
    h89_config           c = bareConfig();
    c.p503                 = H89_P503_MMS77318;
    MachineRoms roms;
    roms.monitor     = rom.data();
    roms.monitorSize = rom.size();

    Machine               m(c, roms, MachineOptions());
    MachineContext::Scope s(&m.context());

    // locked: only bit 5 (ORG-0) switches
    m.io().out(0xf2, 0x04);
    CHECK_EQ_INT(m.memory().getCurrentLayoutNum(), 0);
    m.io().out(0xf2, 0x20);
    CHECK_EQ_INT(m.memory().getCurrentLayoutNum(), 1);
    m.io().out(0xf2, 0x00);

    // the unlock sequence on bits 2/3, then bank = b5 | b2<<1 | b4<<2
    const uint8_t seq[] = {0x0c, 0x04, 0x08, 0x0c, 0x08};
    for (uint8_t v : seq)
    {
        m.io().out(0xf2, v);
    }
    m.io().out(0xf2, 0x04);
    CHECK_EQ_INT(m.memory().getCurrentLayoutNum(), 2);
    m.io().out(0xf2, 0x14);
    CHECK_EQ_INT(m.memory().getCurrentLayoutNum(), 6);
    m.io().out(0xf2, 0x34);
    CHECK_EQ_INT(m.memory().getCurrentLayoutNum(), 7);

    // banks 6 and 7 differ in their low 56K, share the top 8K
    m.memory().writeByte(0x1000, 0x66);
    m.memory().writeByte(0xf000, 0x77);
    m.io().out(0xf2, 0x14);
    CHECK(m.memory().readByte(0x1000) != 0x66);
    CHECK_EQ_INT(m.memory().readByte(0xf000), 0x77);

    // /RESET locks it again
    m.reset();
    CHECK_EQ_INT(m.memory().getCurrentLayoutNum(), 0);
    m.io().out(0xf2, 0x04);
    CHECK_EQ_INT(m.memory().getCurrentLayoutNum(), 0);
}

void
testInterruptController()
{
    std::vector<uint8_t> rom(4096, 0);
    h89_config           c = bareConfig();
    MachineRoms          roms;
    roms.monitor     = rom.data();
    roms.monitorSize = rom.size();
    Machine               m(c, roms, MachineOptions());
    MachineContext::Scope s(&m.context());

    IntrController& ic = m.intr();
    ic.raiseInterrupt(1);
    ic.raiseInterrupt(3);
    CHECK_EQ_INT(ic.readDataBus(), 0xdf);   // the highest level: RST 18H
    ic.lowerInterrupt(3);
    CHECK_EQ_INT(ic.readDataBus(), 0xcf);

    InterruptController* h37 = ic.port(IntrController::SrcH37);
    InterruptController* mms = ic.port(IntrController::SrcMMS77316);
    mms->setIntrq(true);
    CHECK_EQ_INT(ic.readDataBus(), 0xf7);   // RST 30H
    h37->setIntrq(true);
    CHECK_EQ_INT(ic.readDataBus(), 0xe7);   // the H37's INTRQ first: RST 20H
    h37->setIntrq(false);
    mms->setIntrq(false);
    h37->setDrq(true);
    CHECK_EQ_INT(ic.readDataBus(), 0xfb);   // a DRQ: EI
    h37->setDrq(false);
    h37->blockInterrupts(true);
    CHECK(ic.blocked());
    h37->blockInterrupts(false);
    ic.lowerInterrupt(1);
}

void
testBadConfig()
{
    std::vector<uint8_t> rom(100, 0);
    h89_config           c = bareConfig();
    MachineRoms          roms;
    roms.monitor     = rom.data();
    roms.monitorSize = rom.size();
    bool threw       = false;
    try
    {
        Machine m(c, roms, MachineOptions());
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    CHECK(threw);

    // an H17 without its ROM
    std::vector<uint8_t> mon(4096, 0);
    roms.monitor     = mon.data();
    roms.monitorSize = mon.size();
    c.slot[2]        = H89_CARD_H17;
    threw            = false;
    try
    {
        Machine m(c, roms, MachineOptions());
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    CHECK(threw);
}

}

int
main()
{
    // every test builds and destroys a machine: that each one's devices
    // only ever see their own machine is what makes the later ones pass
    testTimerAndConsole();
    testOrg0();
    testH88();
    testMms77318();
    testInterruptController();
    testBadConfig();
    testTimerAndConsole();
    return t_done("machine");
}
