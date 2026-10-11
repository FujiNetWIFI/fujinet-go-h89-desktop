/*
 * Machine -- one H89, built from an h89_config: VirtualH89's machine graph
 * (H89::buildSystem) without the globals.
 *
 * Owns every device through unique_ptr and its own MachineContext (the
 * clock and the GPP listeners the vendored devices register with), so a
 * power cycle is destroying one Machine and building the next -- nothing of
 * the old machine survives to be called by the new one.
 *
 * Not thread-safe: the host (Host.cpp) runs it on its thread and every other
 * thread reaches it under the host's run lock, inside a MachineContext::Scope
 * for it.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 * (structure from VirtualH89's H89.cpp, BSD-3-Clause, Mark Garlanger)
 */

#ifndef H89_MACHINE_H
#define H89_MACHINE_H

#include "MachineContext.h"
#include "computer.h"
#include "h89config.h"
#include "h89drives.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class AddressBus;
class DiskImage;
class CPNetDevice;
class DiskDrive;
class FujiNetCard;
class GeneralPurposePort;
class GenericFloppyDrive;
class GenericSASIDrive;
class H17;
class H19Terminal;
class H89Timer;
class H89_IO;
class INS8250;
class IntrController;
class MMS77316;
class MMS77320;
class MemoryDecoder;
class NMIPort;
class SystemMemory8K;
class Z80;
class Z_89_37;

struct MachineRoms
{
    const uint8_t* monitor     = nullptr;   ///< 2K or 4K, at 0x0000
    size_t         monitorSize = 0;
    const uint8_t* h17         = nullptr;   ///< 444-19, 2K at 0x1800 (or null)
    size_t         h17Size     = 0;
};

struct MachineOptions
{
    std::string boipHost = "127.0.0.1";    ///< the FujiNet card's peer
    int         boipPort = 0;              ///< 0 = no FujiNet runtime
    std::string sasiDir;                   ///< MMS 77320 unit files
};

class Machine: public Computer
{
  public:
    /// Throws std::runtime_error with a sentence for the user when the
    /// configuration cannot be built.
    Machine(const h89_config& cfg, const MachineRoms& roms, const MachineOptions& opts);
    ~Machine() override;
    Machine(const Machine&)            = delete;
    Machine& operator=(const Machine&) = delete;

    MachineContext& context()
    {
        return ctx_m;
    }

    /// One emulated 2 ms: the CPU's budget for it (4096 cycles at 2.048 MHz,
    /// times the speed-up factor), then the timer. Call inside a Scope.
    void runSlice();

    /// SHIFT+RESET on the H19: the H89's /RESET -- CPU, devices, GPP -- and
    /// the terminal's own reset.
    void reset();

    uint64_t slices() const
    {
        return slices_m;
    }

    // ---- the parts (null when not fitted) ----
    Z80&                cpu()
    {
        return *cpu_m;
    }
    H19Terminal&        terminal()
    {
        return *terminal_m;
    }
    MemoryDecoder&      memory()
    {
        return *memory_m;
    }
    GeneralPurposePort& gpp()
    {
        return *gpp_m;
    }
    H89Timer&           timer()
    {
        return *timer_m;
    }
    IntrController&     intr()
    {
        return *intr_m;
    }
    H89_IO&             io()
    {
        return *io_m;
    }
    INS8250&            consolePort()
    {
        return *console_m;
    }
    INS8250*            serialPort(int which)  // 0 aux (0xD0), 1 modem (0xD8), 2 lp (0xE0)
    {
        return which >= 0 && which < 3 ? serial_m[which].get() : nullptr;
    }
    H17*                h17()
    {
        return h17_m.get();
    }
    Z_89_37*            h37()
    {
        return h37_m.get();
    }
    MMS77316*           mms316()
    {
        return mms316_m.get();
    }
    MMS77320*           mms320()
    {
        return mms320_m.get();
    }
    FujiNetCard*        card()
    {
        return card_m.get();
    }
    CPNetDevice*        cpnet()
    {
        return cpnet_m.get();
    }
    DiskDrive*          h17Drive(int unit);
    GenericFloppyDrive* h37Drive(int unit);
    GenericFloppyDrive* mms316Drive(int unit);
    GenericSASIDrive*   mms320Drive(int unit);

    /// What each drive holds (Drives.cpp manages these), by H89_DRIVE_ID.
    std::shared_ptr<DiskImage>& mounted(int id)
    {
        return mounted_m[H89_DRIVE_CTRL(id) % H89_CTRL_COUNT][H89_DRIVE_UNIT(id)];
    }

    const h89_config&   config() const
    {
        return cfg_m;
    }
    uint8_t             sw501() const
    {
        return sw501_m;
    }

    // ---- Computer ----
    void        raiseINT(int level) override;
    void        lowerINT(int level) override;
    void        raiseNMI(void) override;
    void        continueCPU(void) override;
    void        waitCPU() override;
    void        writeProtectH17RAM() override;
    void        writeEnableH17RAM() override;
    AddressBus& getAddressBus() override;
    bool        h17RamWritable() const
    {
        return h17RamWritable_m;
    }

  private:
    void build(const MachineRoms& roms, const MachineOptions& opts);

    // The context is declared first so it is destroyed last: every device
    // below unregisters from it as it goes.
    MachineContext ctx_m;
    h89_config     cfg_m;
    uint8_t        sw501_m;
    uint64_t       slices_m;
    bool           h17RamWritable_m;

    std::unique_ptr<Z80>                   cpu_m;
    std::unique_ptr<H89_IO>                io_m;
    std::unique_ptr<IntrController>        intr_m;
    std::unique_ptr<AddressBus>            ab_m;
    std::shared_ptr<SystemMemory8K>        sysMem_m;
    std::shared_ptr<MemoryDecoder>         memory_m;
    std::unique_ptr<GeneralPurposePort>    gpp_m;
    std::unique_ptr<H89Timer>              timer_m;
    std::unique_ptr<NMIPort>               nmi1_m, nmi2_m;
    std::unique_ptr<INS8250>               console_m;
    std::unique_ptr<H19Terminal>           terminal_m;
    std::unique_ptr<INS8250>               serial_m[3];
    std::unique_ptr<FujiNetCard>           card_m;
    std::unique_ptr<H17>                   h17_m;
    std::shared_ptr<DiskDrive>             h17Drives_m[H89_H17_DRIVES];
    std::unique_ptr<Z_89_37>               h37_m;
    std::unique_ptr<GenericFloppyDrive>    h37Drives_m[H89_H37_DRIVES];
    std::unique_ptr<MMS77316>              mms316_m;
    std::unique_ptr<GenericFloppyDrive>    mms316Drives_m[H89_MMS316_DRIVES];
    std::unique_ptr<MMS77320>              mms320_m;
    GenericSASIDrive*                      mms320Drives_m[H89_MMS320_DRIVES];
    std::unique_ptr<CPNetDevice>           cpnet_m;
    std::shared_ptr<DiskImage>             mounted_m[H89_CTRL_COUNT][16];
};

#endif /* H89_MACHINE_H */
