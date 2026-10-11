///
/// \name clockUser.cpp
///
/// \brief notification class for clock ticks
///
/// \date Aug 11, 2012
/// \author Mark Garlanger
///
/// Modified for FujiNet Go H89 (modifications GPL-3.0-or-later):
///   Registers with its machine's clock (see MachineContext.h).
///

#include "ClockUser.h"

#include "WallClock.h"

ClockUser::ClockUser(): wallClock_m(WallClock::instance())
{
    wallClock_m->registerUser(this);
}

ClockUser::~ClockUser()
{
    wallClock_m->unregisterUser(this);
}
