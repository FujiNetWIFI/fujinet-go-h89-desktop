///
/// \file GppListener.cpp
///
/// Register parties interested in GPIO bits, and notify them whenever bits change.
///
/// \date Mar 05, 2016
/// \author Douglas Miller
///
/// Modified for FujiNet Go H89 (modifications GPL-3.0-or-later): the
/// listeners live in the machine's MachineContext (see MachineContext.h).
///
#include "GppListener.h"
#include "MachineContext.h"

#include <algorithm>

using namespace std;

GppListener::GppListener(BYTE bits): interestedBits_m(bits),
                                     context_m(nullptr)
{
}

GppListener::~GppListener()
{
    if (context_m)
    {
        auto& v = context_m->gppListeners;
        v.erase(std::remove(v.begin(), v.end(), this), v.end());
    }
}

void
GppListener::addListener(GppListener* listener)
{
    MachineContext* ctx = MachineContext::current();
    listener->context_m = ctx;
    ctx->gppListeners.push_back(listener);
}

void
GppListener::notifyListeners(BYTE gpo,
                             BYTE diffs)
{
    // copy: a listener may (in principle) register another while notified
    std::vector<GppListener*> listeners = MachineContext::current()->gppListeners;
    for (GppListener* listener : listeners)
    {
        if ((listener->interestedBits_m & diffs) != 0)
        {
            listener->gppNewValue(gpo);
        }
    }
}
