/*
 * Machine -- see Machine.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 * (structure from VirtualH89's H89.cpp, BSD-3-Clause, Mark Garlanger)
 */

#include "Machine.h"

#include "AddressBus.h"
#include "DiskImage.h"
#include "CPNetDevice.h"
#include "FujiNetCard.h"
#include "GeneralPurposePort.h"
#include "GenericFloppyDrive.h"
#include "GenericSASIDrive.h"
#include "H19Terminal.h"
#include "INS8250.h"
#include "IntrController.h"
#include "MemoryDecoder.h"
#include "NMIPort.h"
#include "ROM.h"
#include "SystemMemory8K.h"
#include "h-17-1.h"
#include "h-17-4.h"
#include "h17.h"
#include "h37.h"
#include "h89-io.h"
#include "h89-timer.h"
#include "mms77316.h"
#include "mms77320.h"
#include "propertyutil.h"
#include "z80.h"

#include <stdexcept>

namespace
{

// Port addresses (VirtualH89's H89.h; octal in the manuals)
const BYTE H17_BaseAddress_c      = 0x7c;  // 174Q
const BYTE H37_BaseAddress_c      = 0x78;  // 170Q
const BYTE MMS77316_BaseAddress_c = 0x38;  // 070Q
const BYTE Serial_Console_c       = 0350;  // 0xE8
const BYTE Serial_Console_Int_c   = 3;
const BYTE Serial_AuxPort_c       = 0320;  // 0xD0
const BYTE Serial_ModemPort_c     = 0330;  // 0xD8
const BYTE Serial_LpPort_c        = 0340;  // 0xE0
const BYTE NMI_BaseAddress_1_c    = 0xf0;
const BYTE NMI_BaseAddress_2_c    = 0xfa;

const unsigned long cpuClockRate_c       = 2048000;
const unsigned      sliceCycles_c        = 4096;   // 2 mSec at 2.048 MHz
const unsigned      clockInterruptsPerS_c = 500;

std::string
binary8(uint8_t v)
{
    std::string s;
    for (int b = 7; b >= 0; --b)
    {
        s += (v & (1 << b)) ? '1' : '0';
    }
    return s;
}

int
countCard(const h89_config& c, int card)
{
    int n = 0;
    for (int s : c.slot)
    {
        n += (s == card);
    }
    return n;
}

}

Machine::Machine(const h89_config& cfg, const MachineRoms& roms, const MachineOptions& opts):
    cfg_m(cfg),
    sw501_m(h89_config_effective_sw501(&cfg)),
    slices_m(0),
    h17RamWritable_m(false)
{
    for (GenericSASIDrive*& d : mms320Drives_m)
    {
        d = nullptr;
    }

    char why[256];
    if (!h89_config_validate(&cfg_m, why, sizeof why))
    {
        throw std::runtime_error(why);
    }
    if (!roms.monitor || (roms.monitorSize != 2048 && roms.monitorSize != 4096))
    {
        throw std::runtime_error("The monitor ROM is missing or is not 2K or 4K.");
    }
    if (countCard(cfg_m, H89_CARD_H17) && (!roms.h17 || roms.h17Size != 2048))
    {
        throw std::runtime_error("The H17 controller needs its ROM (444-19); import the H89 ROMs.");
    }

    ctx_m.computer = this;
    MachineContext::Scope scope(&ctx_m);
    build(roms, opts);
    reset();
}

void
Machine::build(const MachineRoms& roms, const MachineOptions& opts)
{
    cpu_m.reset(new Z80(this, cpuClockRate_c, clockInterruptsPerS_c));
    io_m.reset(new H89_IO);
    cpu_m->setIOBus(io_m.get());

    gpp_m.reset(new GeneralPurposePort(this, binary8(sw501_m)));
    io_m->addDevice(gpp_m.get());

    // ---- memory ----
    ROM monitor((int) roms.monitorSize);
    monitor.setBaseAddress(0);
    monitor.initialize(const_cast<BYTE*>(roms.monitor), (WORD) roms.monitorSize);

    sysMem_m = std::make_shared<SystemMemory8K>();
    sysMem_m->installROM(&monitor);
    if (roms.h17 && roms.h17Size == 2048)
    {
        ROM h17rom(2048);
        h17rom.setBaseAddress(6 * 1024);
        h17rom.initialize(const_cast<BYTE*>(roms.h17), 2048);
        sysMem_m->installROM(&h17rom);
    }
    // the H-17's scratch RAM, write-protected until its control port says
    sysMem_m->enableRAM(0x1400, 1024);
    sysMem_m->writeProtect(0x1400, 1024);

    MemoryLayout::MemorySize_t memSize = MemoryLayout::Mem_64k;
    switch (cfg_m.memory)
    {
        case H89_MEM_16K:
            memSize = MemoryLayout::Mem_16k;
            break;
        case H89_MEM_32K:
            memSize = MemoryLayout::Mem_32k;
            break;
        case H89_MEM_48K:
            memSize = MemoryLayout::Mem_48k;
            break;
        default:
            memSize = MemoryLayout::Mem_64k;
            break;
    }
    if (cfg_m.p503 == H89_P503_WH8816)
    {
        memSize = MemoryLayout::Mem_64k;
    }
    if (cfg_m.model == H89_MODEL_H88 && memSize == MemoryLayout::Mem_64k)
    {
        // without ORG-0 the board decodes 48K above the ROM page
        memSize = MemoryLayout::Mem_48k;
    }

    const char* decoder = cfg_m.p503 == H89_P503_MMS77318 ? "MMS77318"
                          : cfg_m.model == H89_MODEL_H88  ? "H88"
                                                          : "H89";

    // ---- the CPU's speed-up board ----
    if (cfg_m.speedup > 1 && cfg_m.p503 != H89_P503_MMS77318)
    {
        cpu_m->setSpeedup(cfg_m.speedup);
        cpu_m->enableFast();
    }

    // ---- the slots ----
    intr_m.reset(new IntrController(cpu_m.get()));

    for (int slot = 0; slot < H89_SLOTS; ++slot)
    {
        switch (cfg_m.slot[slot])
        {
            case H89_CARD_FUJINET:
                card_m.reset(new FujiNetCard(opts.boipHost, opts.boipPort,
                                             (unsigned) cfg_m.fujinet_ack_delay));
                io_m->addDevice(card_m.get());
                break;

            case H89_CARD_H17:
                h17_m.reset(new H17(H17_BaseAddress_c));
                for (int i = 0; i < H89_H17_DRIVES; ++i)
                {
                    std::shared_ptr<DiskDrive> drive;
                    if (cfg_m.h17_drive[i] == H89_H17DRV_H17_1)
                    {
                        drive = std::make_shared<H_17_1>();
                    }
                    else if (cfg_m.h17_drive[i] == H89_H17DRV_H17_4)
                    {
                        drive = std::make_shared<H_17_4>();
                    }
                    if (drive)
                    {
                        h17Drives_m[i] = drive;
                        h17_m->connectDrive((BYTE) i, drive);
                    }
                }
                io_m->addDiskDevice(h17_m.get());
                break;

            case H89_CARD_H37:
                h37_m.reset(new Z_89_37(this, H37_BaseAddress_c,
                                        intr_m->port(IntrController::SrcH37)));
                for (int i = 0; i < H89_H37_DRIVES; ++i)
                {
                    const char* type = h89_fdd_type_id(cfg_m.h37_drive[i]);
                    if (type && *type)
                    {
                        h37Drives_m[i].reset(GenericFloppyDrive::getInstance(type));
                        if (h37Drives_m[i])
                        {
                            h37_m->connectDrive((BYTE) i, h37Drives_m[i].get());
                        }
                    }
                }
                io_m->addDiskDevice(h37_m.get());
                break;

            case H89_CARD_MMS77316:
                mms316_m.reset(new MMS77316(MMS77316_BaseAddress_c,
                                            intr_m->port(IntrController::SrcMMS77316)));
                for (int i = 0; i < H89_MMS316_DRIVES; ++i)
                {
                    const char* type = h89_fdd_type_id(cfg_m.mms316_drive[i]);
                    if (type && *type)
                    {
                        mms316Drives_m[i].reset(GenericFloppyDrive::getInstance(type));
                        if (mms316Drives_m[i])
                        {
                            mms316_m->connectDrive((BYTE) i, mms316Drives_m[i].get());
                        }
                    }
                }
                io_m->addDiskDevice(mms316_m.get());
                break;

            case H89_CARD_MMS77320:
            {
                mms320_m.reset(new MMS77320(cfg_m.mms320_port, cfg_m.mms320_intr,
                                            cfg_m.mms320_dipsw));
                std::string dir = opts.sasiDir.empty() ? std::string(".") : opts.sasiDir;
                for (int i = 0; i < H89_MMS320_DRIVES; ++i)
                {
                    const char* type = h89_sasi_type_id(cfg_m.mms320_drive[i]);
                    if (type && *type)
                    {
                        std::string media = dir + "/MMS77320-" + std::to_string(i);
                        GenericSASIDrive* d = GenericSASIDrive::getInstance(type, media, i);
                        if (d)
                        {
                            mms320Drives_m[i] = d;
                            mms320_m->connectDrive((BYTE) i, d);
                        }
                    }
                }
                io_m->addDiskDevice(mms320_m.get());
                break;
            }

            case H89_CARD_H88_3:
                // three more 8250s; their interrupt lines are not connected
                serial_m[0].reset(new INS8250(this, Serial_AuxPort_c));
                serial_m[1].reset(new INS8250(this, Serial_ModemPort_c));
                serial_m[2].reset(new INS8250(this, Serial_LpPort_c));
                for (auto& p : serial_m)
                {
                    io_m->addDevice(p.get());
                }
                break;

            default:
                break;
        }
    }

    if (cfg_m.cpnet_enabled)
    {
        PropertyUtil::PropertyMapT props;
        props["cpnetdevice_port"]      = PropertyUtil::sprintf("0x%02x", cfg_m.cpnet_port);
        props["cpnetdevice_clientid"]  = PropertyUtil::sprintf("0x%02x", cfg_m.cpnet_clientid);
        props["cpnetdevice_server00"]  = "HostFileBdos";
        props["hostfilebdos_root_dir"] = cfg_m.cpnet_root;
        cpnet_m.reset(CPNetDevice::install_CPNetDevice(props));
        if (cpnet_m)
        {
            io_m->addDevice(cpnet_m.get());
        }
    }

    ab_m.reset(new AddressBus(intr_m.get()));
    cpu_m->setAddressBus(ab_m.get());
    timer_m.reset(new H89Timer(this, cpu_m.get()));

    nmi1_m.reset(new NMIPort(cpu_m.get(), NMI_BaseAddress_1_c, 2));
    nmi2_m.reset(new NMIPort(cpu_m.get(), NMI_BaseAddress_2_c, 2));
    io_m->addDevice(nmi1_m.get());
    io_m->addDevice(nmi2_m.get());

    memory_m = MemoryDecoder::createMemoryDecoder(decoder, sysMem_m, memSize);
    ab_m->installMemory(memory_m);

    // ---- the console: the H19 on the 8250 at 0xE8 ----
    console_m.reset(new INS8250(this, Serial_Console_c, Serial_Console_Int_c));
    terminal_m.reset(new H19Terminal(cfg_m.sw401, cfg_m.sw402));
    console_m->attachDevice(terminal_m.get());
    terminal_m->attachUart(console_m.get());
    io_m->addDevice(console_m.get());
}

Machine::~Machine()
{
    MachineContext::Scope scope(&ctx_m);

    // Disks out of their drives first (the drives hold them too).
    for (auto& ctrl : mounted_m)
    {
        for (auto& m : ctrl)
        {
            m.reset();
        }
    }

    // Devices before what they point at: the controllers hold raw pointers
    // to their drives, everything to the CPU, the CPU to the buses.
    cpnet_m.reset();
    mms320_m.reset();
    mms316_m.reset();
    for (auto& d : mms316Drives_m)
    {
        d.reset();
    }
    h37_m.reset();
    for (auto& d : h37Drives_m)
    {
        d.reset();
    }
    h17_m.reset();
    for (auto& d : h17Drives_m)
    {
        d.reset();
    }
    card_m.reset();
    for (auto& p : serial_m)
    {
        p.reset();
    }
    console_m.reset();
    terminal_m.reset();
    nmi1_m.reset();
    nmi2_m.reset();
    timer_m.reset();
    gpp_m.reset();
    ab_m.reset();
    memory_m.reset();
    sysMem_m.reset();
    intr_m.reset();
    io_m.reset();
    cpu_m.reset();
}

void
Machine::reset()
{
    MachineContext::Scope scope(&ctx_m);

    cpu_m->reset();
    ab_m->reset();
    io_m->reset();
    timer_m->reset();
    // the GPP's reset (in io_m) wrote 0 to it, which only notifies on a
    // change: put every listener back to the power-on state explicitly
    GppListener::notifyListeners(0, 0xff);
    for (BYTE l = 0; l < 8; ++l)
    {
        intr_m->lowerInterrupt(l);
    }
    sysMem_m->writeProtect(0x1400, 1024);
    h17RamWritable_m = false;
    terminal_m->powerOn();
}

void
Machine::runSlice()
{
    unsigned factor = cpu_m->speedFactor();
    cpu_m->runCycles((long) sliceCycles_c * (long) factor);
    timer_m->slice();
    ++slices_m;
}

DiskDrive*
Machine::h17Drive(int unit)
{
    return (unit >= 0 && unit < H89_H17_DRIVES) ? h17Drives_m[unit].get() : nullptr;
}

GenericFloppyDrive*
Machine::h37Drive(int unit)
{
    return (unit >= 0 && unit < H89_H37_DRIVES) ? h37Drives_m[unit].get() : nullptr;
}

GenericFloppyDrive*
Machine::mms316Drive(int unit)
{
    return (unit >= 0 && unit < H89_MMS316_DRIVES) ? mms316Drives_m[unit].get() : nullptr;
}

GenericSASIDrive*
Machine::mms320Drive(int unit)
{
    return (unit >= 0 && unit < H89_MMS320_DRIVES) ? mms320Drives_m[unit] : nullptr;
}

// ---- Computer ----

void
Machine::raiseINT(int level)
{
    intr_m->raiseInterrupt((BYTE) level);
}

void
Machine::lowerINT(int level)
{
    intr_m->lowerInterrupt((BYTE) level);
}

void
Machine::raiseNMI(void)
{
    cpu_m->raiseNMI();
}

void
Machine::continueCPU(void)
{
    cpu_m->continueRunning();
}

void
Machine::waitCPU()
{
    cpu_m->waitState();
}

void
Machine::writeProtectH17RAM()
{
    sysMem_m->writeProtect(0x1400, 1024);
    h17RamWritable_m = false;
}

void
Machine::writeEnableH17RAM()
{
    sysMem_m->writeEnable(0x1400, 1024);
    h17RamWritable_m = true;
}

AddressBus&
Machine::getAddressBus()
{
    return *ab_m;
}
