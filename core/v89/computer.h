///
/// \name computer.h
/// \brief Base computer class.
///
/// \date Jul 16, 2012
/// \author Mark Garlanger
///
/// Modified for FujiNet Go H89 (modifications GPL-3.0-or-later):
///   Only what the devices call remains. run()/init()/keypress()/display()
///   and the system mutex went with VirtualH89's main loop; the H-17 RAM
///   write-protect calls (the global `h89` in VirtualH89) moved here.
///

#ifndef COMPUTER_H_
#define COMPUTER_H_

#include "h89Types.h"

class AddressBus;

/// \class Computer
///
/// The machine as its devices see it (core/machine/Machine implements it).
///
class Computer
{
  public:
    Computer();
    virtual ~Computer();

    virtual void raiseINT(int level)    = 0;
    virtual void lowerINT(int level)    = 0;
    virtual void raiseNMI(void)         = 0;
    virtual void continueCPU(void)      = 0;
    virtual void waitCPU()              = 0;

    /// H-17 controller control port bit 7: the scratch RAM at 0x1400.
    virtual void writeProtectH17RAM()   = 0;
    virtual void writeEnableH17RAM()    = 0;

    virtual AddressBus& getAddressBus() = 0;
};

#endif // COMPUTER_H_
