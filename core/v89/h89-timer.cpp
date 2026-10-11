/// \file h89-timer.cpp
///
/// \date Mar 26, 2009
/// \author Mark Garlanger
///
/// Modified for FujiNet Go H89 (modifications GPL-3.0-or-later): see
/// h89-timer.h.
///

#include "h89-timer.h"
#include "computer.h"
#include "cpu.h"
#include "logger.h"

H89Timer::H89Timer(Computer*     computer,
                   CPU*          cpu,
                   unsigned char intlvl): GppListener(h89timer_gpp2msIntEnBit_c),
                                          computer_m(computer),
                                          cpu_m(cpu),
                                          intEnabled_m(false),
                                          count_m(0),
                                          intLevel(intlvl)
{
    debugss(ssTimer, INFO, "\n");
    GppListener::addListener(this);
}

H89Timer::~H89Timer()
{
    debugss(ssTimer, INFO, "\n");
}

void
H89Timer::reset()
{
    intEnabled_m = false;
    count_m      = 0;
}

void
H89Timer::slice()
{
    count_m++;

    // Only if interrupt is enabled.
    if (intEnabled_m)
    {
        debugss(ssTimer, VERBOSE, "raising Interrupt\n");
        computer_m->raiseINT(intLevel);
    }
}

void
H89Timer::gppNewValue(BYTE gpo)
{
    intEnabled_m = ((gpo & h89timer_gpp2msIntEnBit_c) != 0);
}
