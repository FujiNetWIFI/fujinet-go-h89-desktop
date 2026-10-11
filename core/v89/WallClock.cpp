/// \file WallClock.cpp
///
/// \date Apr 21, 2009
/// \author Mark Garlanger
///
/// Modified for FujiNet Go H89 (modifications GPL-3.0-or-later): see
/// WallClock.h.
///

#include "WallClock.h"
#include "ClockUser.h"
#include "MachineContext.h"

WallClock::WallClock(): clock_m(0),
                        speedFactor_m(1),
                        remainder_m(0)
{
}

WallClock::~WallClock()
{
}

WallClock*
WallClock::instance(void)
{
    return &MachineContext::current()->clock();
}

void
WallClock::addTimerEvent()
{
}

void
WallClock::setSpeedFactor(unsigned factor)
{
    speedFactor_m = factor ? factor : 1;
    remainder_m   = 0;
}

void
WallClock::addTicks(unsigned cpuTicks)
{
    unsigned ticks = cpuTicks;

    if (speedFactor_m > 1)
    {
        unsigned total = remainder_m + cpuTicks;
        ticks        = total / speedFactor_m;
        remainder_m  = total % speedFactor_m;
        if (!ticks)
        {
            return;
        }
    }

    clock_m += ticks;

    for (ClockUser* clockUser : users_m)
    {
        clockUser->notification(ticks);
    }
}

long long unsigned int
WallClock::getClock()
{
    return clock_m;
}

long long unsigned int
WallClock::getElapsedTime(long long unsigned int origTime)
{
    return clock_m - origTime;
}

void
WallClock::printTime(FILE* file)
{
    // determine seconds, millisec, microseconds..
    unsigned long long time     = clock_m;
    unsigned long long millisec = time >> 11;
    unsigned long long seconds  = millisec / 1000;

    millisec %= 1000;

    fprintf(file, "%05lld.%03lld:%04lld - ", seconds, millisec, time & 0x7ff);
}

bool
WallClock::registerUser(ClockUser* user)
{
    users_m.push_back(user);
    return true;
}

bool
WallClock::unregisterUser(ClockUser* user)
{
    users_m.remove(user);
    return true;
}
