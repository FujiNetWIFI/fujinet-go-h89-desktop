/// \file MachineContext.cpp
///
/// See MachineContext.h.
///
/// Copyright (C) 2026 Thomas Cherryhomes
/// SPDX-License-Identifier: GPL-3.0-or-later
///

#include "MachineContext.h"

#include <cstdio>
#include <cstdlib>

namespace
{
thread_local MachineContext* t_current = nullptr;
}

MachineContext::MachineContext()
{
}

MachineContext::~MachineContext()
{
    if (t_current == this)
    {
        t_current = nullptr;
    }
}

MachineContext*
MachineContext::current()
{
    if (!t_current)
    {
        fprintf(stderr, "h89: device code ran with no machine context on this thread\n");
        abort();
    }
    return t_current;
}

MachineContext*
MachineContext::exchange(MachineContext* ctx)
{
    MachineContext* prev = t_current;
    t_current            = ctx;
    return prev;
}

MachineContext::Scope::Scope(MachineContext* ctx): prev_m(t_current)
{
    t_current = ctx;
}

MachineContext::Scope::~Scope()
{
    t_current = prev_m;
}
