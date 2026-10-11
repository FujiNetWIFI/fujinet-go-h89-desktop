/// \file h89-timer.h
///
/// \date   Mar 26, 2009
/// \author Mark Garlanger
///
/// Modified for FujiNet Go H89 (modifications GPL-3.0-or-later):
///   No SIGALRM, no setitimer, no thread: the host calls slice() once per
///   emulated 2 ms (every 4096 device-time cycles), after giving the CPU
///   that slice's budget. That is what VirtualH89's signal handler did,
///   minus the work that is not safe in a signal handler.
///

#ifndef H89TIMER_H_
#define H89TIMER_H_

#include "GppListener.h"

class CPU;
class Computer;

///
/// \brief The H89's 2 mSec timer
///
/// Interrupts the CPU on level 1 (RST 1) every 2 mSec while GPP bit 1 is
/// set; writing the GPP clears the interrupt (GeneralPurposePort::out).
///
class H89Timer: public GppListener
{
  public:
    H89Timer(Computer*     computer,
             CPU*          cpu,
             unsigned char intlvl = 1);
    virtual ~H89Timer() override;

    void reset();

    /// One 2 mSec period has passed.
    void slice();

    /// 2 mSec periods since power on (the debugger shows it).
    unsigned long count() const
    {
        return count_m;
    }
    bool interruptEnabled() const
    {
        return intEnabled_m;
    }

  private:
    virtual void gppNewValue(BYTE gpo) override;

    static const BYTE h89timer_gpp2msIntEnBit_c = 0b00000010;

    Computer*         computer_m;
    CPU*              cpu_m;
    bool              intEnabled_m;
    unsigned long     count_m;
    unsigned char     intLevel;
};

#endif // H89TIMER_H_
