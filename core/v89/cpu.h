/// \file cpu.h
///
/// \date Mar 7, 2009
/// \author Mark Garlanger
///
/// Modified for FujiNet Go H89 (modifications GPL-3.0-or-later):
///   execute()/step() replaced by runCycles()/stop(); speedFactor() added.
///

#ifndef CPU_H_
#define CPU_H_



#include "h89Types.h"

/// \cond
#include <string>
/// \endcond

class AddressBus;
class IOBus;

///
/// \brief  Abstract processor.
///
/// Basic processor abstraction class.
///
class CPU
{
  public:
    static const unsigned long NO_INTR_INST = ((unsigned long) -1);
    typedef unsigned long intrCheck (void* arg,
                                     int   level);
  private:

  public:

    CPU();
    virtual ~CPU();

    virtual void addClockTicks(void)           = 0;
    /// Run until `budget` more clock cycles have been used (or stop() is
    /// called); returns the cycles actually run. Replaces execute()/step():
    /// the host paces the CPU in 2 ms slices instead of a SIGALRM.
    virtual long runCycles(long budget)        = 0;
    /// Make a runCycles() in progress return at the next instruction.
    virtual void stop(void)                    = 0;
    virtual void reset(void)                   = 0;
    virtual void raiseINT(void)                = 0;
    virtual void lowerINT(void)                = 0;
    virtual void raiseNMI(void)                = 0;
    virtual void setIOBus(IOBus* io)           = 0;
    virtual void setAddressBus(AddressBus* ab) = 0;
    virtual void continueRunning(void)         = 0;
    virtual void waitState(void)               = 0;
    virtual std::string dumpDebug()            = 0;
    virtual void setSpeedup(int factor)        = 0;
    virtual void enableFast(void)              = 0;
    /// The current clock multiplier (1, or the speed-up factor while the
    /// speed-up board's GPP bit is set).
    virtual unsigned speedFactor(void)         = 0;

};

#endif // CPU_H_
