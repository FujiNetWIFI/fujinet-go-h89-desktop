/*
 * IntrController -- see IntrController.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IntrController.h"

#include "cpu.h"

namespace
{
const BYTE intrqOpcode[IntrController::SrcCount] = {
    0xe7, // H37: RST 20H (interrupt level 4)
    0xf7, // MMS 77316: RST 30H (level 6)
};
const BYTE EI_c = 0xfb;
}

IntrController::IntrController(CPU* cpu): InterruptController(cpu),
                                          blocked_m(false)
{
    for (int i = 0; i < SrcCount; ++i)
    {
        ports_m[i].owner = this;
        ports_m[i].src   = (Source) i;
    }
}

IntrController::~IntrController()
{
}

void
IntrController::setINTLine()
{
    update(false);
}

void
IntrController::update(bool fdcEdge)
{
    bool fdc = false;
    for (const SourceState& s : src_m)
    {
        fdc = fdc || s.intrq || s.drq;
    }

    if ((!blocked_m && intLevel_m != 0) || fdc)
    {
        cpu_m->raiseINT();
        // VirtualH89's soft-sectored controllers also let the CPU out of a
        // HALT the moment their INTRQ/DRQ goes up; kept, since the drivers
        // written against it rely on it.
        if (fdcEdge && fdc)
        {
            cpu_m->continueRunning();
        }
    }
    else
    {
        cpu_m->lowerINT();
    }
}

BYTE
IntrController::readDataBus()
{
    for (int i = 0; i < SrcCount; ++i)
    {
        if (src_m[i].intrq)
        {
            return intrqOpcode[i];
        }
    }
    for (const SourceState& s : src_m)
    {
        if (s.drq)
        {
            return EI_c;
        }
    }
    return InterruptController::readDataBus();
}

void
IntrController::Port::raiseInterrupt(BYTE level)
{
    owner->raiseInterrupt(level);
}

void
IntrController::Port::lowerInterrupt(BYTE level)
{
    owner->lowerInterrupt(level);
}

void
IntrController::Port::setDrq(bool raise)
{
    owner->src_m[src].drq = raise;
    owner->update(true);
}

void
IntrController::Port::setIntrq(bool raise)
{
    owner->src_m[src].intrq = raise;
    owner->update(true);
}

void
IntrController::Port::blockInterrupts(bool block)
{
    owner->blocked_m = block;
    owner->update(false);
}

BYTE
IntrController::Port::readDataBus()
{
    return owner->readDataBus();
}
