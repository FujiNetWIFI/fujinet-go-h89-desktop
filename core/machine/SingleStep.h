/*
 * SingleStep -- the H89 CPU board's single-step interrupt: the 555/556
 * timers behind GPP bit 0, as MAME's h89.cpp models them. With the bit set,
 * the opcode fetch of an EI arms it, the next opcode fetch counts, and the
 * one after that asserts interrupt level 2 (RST 10H): the CPU takes it when
 * that instruction is done, so exactly one instruction runs after a
 * debugger's EI and return into the program. Clearing the bit releases the
 * level and disarms it.
 *
 * VirtualH89 left this as a TODO (GeneralPurposePort.cpp); HDOS's DBUG
 * steps programs with it.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_SINGLESTEP_H
#define H89_SINGLESTEP_H

#include "GppListener.h"
#include "h89Types.h"

class Computer;

class SingleStep: public GppListener
{
  public:
    static const BYTE gppEnableBit_c = 0x01;
    static const int  level_c        = 2;

    explicit SingleStep(Computer* computer);
    ~SingleStep() override;

    /// Power-on and /RESET: disabled, disarmed.
    void reset();

    /// Every opcode fetch (M1), with the byte fetched.
    void fetched(BYTE opcode);
    /// The Z80's M1 hook.
    static void m1Hook(void* self, BYTE opcode)
    {
        static_cast<SingleStep*>(self)->fetched(opcode);
    }

    /// The debugger's view.
    bool enabled() const
    {
        return enabled_m;
    }
    bool asserted() const
    {
        return asserted_m;
    }

  private:
    void gppNewValue(BYTE gpo) override;
    void disarm();

    Computer* computer_m;
    bool      enabled_m;
    bool      sawEI_m;     // MAME's 555A latch
    bool      next_m;      // 555B
    bool      asserted_m;  // 556B: level 2 is up
};

#endif /* H89_SINGLESTEP_H */
