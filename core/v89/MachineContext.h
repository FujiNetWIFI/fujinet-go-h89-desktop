/// \file MachineContext.h
///
/// The per-machine replacement for VirtualH89's process-wide singletons.
///
/// VirtualH89 kept the wall clock (WallClock::_inst), the list of
/// general-purpose-port listeners (GppListener::notifications) and the
/// machine itself (the global `H89 h89`) in statics, which is fine for an
/// emulator that builds one machine per process and never tears it down --
/// and a use-after-free the moment a desktop app power-cycles: the next
/// machine's GPP write would notify the previous machine's freed devices.
///
/// Each Machine (core/machine/Machine.cpp) now owns a MachineContext. The
/// vendored devices still reach "their" clock and listener list through the
/// same calls (WallClock::instance(), GppListener::addListener()), which
/// resolve through MachineContext::current() -- a thread-local the Machine
/// sets on whichever thread is building, running or inspecting it (Scope).
/// Devices remember the context they registered with and unregister from
/// it on destruction.
///
/// Copyright (C) 2026 Thomas Cherryhomes
/// SPDX-License-Identifier: GPL-3.0-or-later
///

#ifndef MACHINECONTEXT_H_
#define MACHINECONTEXT_H_

#include "h89Types.h"
#include "WallClock.h"

#include <vector>

class Computer;
class GppListener;

class MachineContext
{
  public:
    MachineContext();
    ~MachineContext();
    MachineContext(const MachineContext&)            = delete;
    MachineContext& operator=(const MachineContext&) = delete;

    WallClock& clock()
    {
        return clock_m;
    }

    /// The machine the devices talk to (VirtualH89's global `h89`).
    Computer* computer = nullptr;

    /// GPP listeners, in registration order (VirtualH89's static list).
    std::vector<GppListener*> gppListeners;

    /// The context of the machine this thread is working on; never null
    /// while any device code runs (aborts with a message otherwise).
    static MachineContext* current();

    /// Sets this thread's current context and returns the previous one (for
    /// code that cannot use a Scope: a lock and its unlock).
    static MachineContext* exchange(MachineContext* ctx);

    /// Makes `ctx` current on this thread for the scope's lifetime.
    class Scope
    {
      public:
        explicit Scope(MachineContext* ctx);
        ~Scope();
        Scope(const Scope&)            = delete;
        Scope& operator=(const Scope&) = delete;

      private:
        MachineContext* prev_m;
    };

  private:
    WallClock clock_m;
};

#endif // MACHINECONTEXT_H_
