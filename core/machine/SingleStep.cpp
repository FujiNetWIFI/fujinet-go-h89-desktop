/*
 * SingleStep -- see the header.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "SingleStep.h"

#include "computer.h"

SingleStep::SingleStep(Computer* computer): GppListener(gppEnableBit_c),
                                            computer_m(computer),
                                            enabled_m(false),
                                            sawEI_m(false),
                                            next_m(false),
                                            asserted_m(false)
{
    GppListener::addListener(this);
}

SingleStep::~SingleStep()
{
}

void
SingleStep::reset()
{
    enabled_m = false;
    disarm();
}

void
SingleStep::disarm()
{
    sawEI_m = false;
    next_m  = false;
    if (asserted_m)
    {
        asserted_m = false;
        computer_m->lowerINT(level_c);
    }
}

void
SingleStep::fetched(BYTE opcode)
{
    if (!enabled_m || asserted_m)
    {
        return;
    }
    if (!sawEI_m)
    {
        sawEI_m = opcode == 0xfb;
    }
    else if (!next_m)
    {
        next_m = true;
    }
    else
    {
        asserted_m = true;
        computer_m->raiseINT(level_c);
    }
}

void
SingleStep::gppNewValue(BYTE gpo)
{
    bool on = (gpo & gppEnableBit_c) != 0;
    if (on == enabled_m)
    {
        return;
    }
    enabled_m = on;
    if (!on)
    {
        disarm();
    }
}
