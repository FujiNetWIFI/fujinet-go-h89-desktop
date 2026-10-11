/*
 * IntrController -- the H89's interrupt logic with any combination of
 * soft-sectored controllers.
 *
 * VirtualH89 modelled the soft-sectored controllers' interrupt quirks by
 * replacing the machine's one InterruptController with a subclass
 * (H37InterruptController, MMS316IntrCtrlr), so whichever controller was
 * configured last won and the other's INTRQ/DRQ went nowhere. Here one
 * controller holds the eight motherboard levels plus a source per
 * soft-sectored controller; each controller is handed a Port that looks
 * like the InterruptController it was written against.
 *
 * On the data bus during an interrupt acknowledge (IM 0), in this order:
 *   - an INTRQ: RST 20H (0xE7) for the H37, RST 30H (0xF7) for the MMS 77316
 *   - a DRQ: EI (0xFB) -- both cards' trick for HALT-paced transfers
 *   - otherwise the highest motherboard level, RST level*8
 * The H37 can block the motherboard levels while its DRQ interrupts are
 * enabled (blockInterrupts), as on the card.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 * (behaviour from VirtualH89's InterruptController, H37InterruptController
 * and MMS316IntrCtrlr, BSD-3-Clause, Mark Garlanger and Douglas Miller)
 */

#ifndef H89_INTRCONTROLLER_H
#define H89_INTRCONTROLLER_H

#include "InterruptController.h"

class IntrController: public InterruptController
{
  public:
    enum Source
    {
        SrcH37 = 0,
        SrcMMS77316,
        SrcCount
    };

    explicit IntrController(CPU* cpu);
    ~IntrController() override;

    BYTE readDataBus() override;

    /// What a soft-sectored controller is constructed with.
    InterruptController* port(Source src)
    {
        return &ports_m[src];
    }

    /// The debugger's view.
    BYTE levels() const
    {
        return intLevel_m;
    }
    bool intrq(Source s) const
    {
        return src_m[s].intrq;
    }
    bool drq(Source s) const
    {
        return src_m[s].drq;
    }
    bool blocked() const
    {
        return blocked_m;
    }

  protected:
    void setINTLine() override;

  private:
    class Port: public InterruptController
    {
      public:
        Port(): InterruptController(nullptr)
        {
        }
        void raiseInterrupt(BYTE level) override;
        void lowerInterrupt(BYTE level) override;
        void setDrq(bool raise) override;
        void setIntrq(bool raise) override;
        void blockInterrupts(bool block) override;
        BYTE readDataBus() override;

        IntrController* owner = nullptr;
        Source          src   = SrcH37;
    };

    struct SourceState
    {
        bool intrq = false;
        bool drq   = false;
    };

    void update(bool fdcEdge);

    Port        ports_m[SrcCount];
    SourceState src_m[SrcCount];
    bool        blocked_m;
};

#endif /* H89_INTRCONTROLLER_H */
