/// \file WallClock.h
///
/// \date Apr 21, 2009
/// \author Mark Garlanger
///
/// Modified for FujiNet Go H89 (modifications GPL-3.0-or-later):
///   No longer a process singleton: each MachineContext owns one, and
///   instance() returns the current machine's. The clock counts 2.048 MHz
///   "device time": with the speed-up board engaged the CPU's cycles are
///   divided by the speed factor first, so disks spin and the H19's
///   characters arrive at their real rates whatever the CPU does.
///   Callbacks are made with the device-time cycle count.
///

#ifndef WALLCLOCK_H_
#define WALLCLOCK_H_

/// \cond
#include <cstdio>
#include <list>
/// \endcond

class ClockUser;

///
/// \brief Virtual wall clock
///
/// Provides a common timebase (2.048 MHz cycles) for all the devices, and
/// calls each registered ClockUser as time passes.
///
class WallClock
{
  public:
    WallClock();
    ~WallClock();
    WallClock(WallClock const&)            = delete;
    WallClock& operator=(WallClock const&) = delete;

    bool registerUser(ClockUser* user);
    bool unregisterUser(ClockUser* user);

    /// The current machine's clock (MachineContext::current()).
    static WallClock* instance(void);

    /// Kept for the vendored callers; a no-op (time advances in addTicks).
    void addTimerEvent();

    /// Device-time cycles per second: always 2048000.
    unsigned long getTicksPerSecond()
    {
        return ticksPerSecond_c;
    }

    /// The CPU's clock multiplier (1 normally).
    void setSpeedFactor(unsigned factor);

    /// `cpuTicks` CPU cycles have passed.
    void addTicks(unsigned cpuTicks);

    long long unsigned int getClock();
    long long unsigned int getElapsedTime(long long unsigned int origTime);
    void printTime(FILE* file);

  private:
    static const unsigned long ticksPerSecond_c = 2048000;

    unsigned long long    clock_m;
    unsigned              speedFactor_m;
    unsigned              remainder_m;
    std::list<ClockUser*> users_m;
};

#endif // WALLCLOCK_H_
