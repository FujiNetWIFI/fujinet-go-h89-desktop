///
///  \file MemoryLayout.cpp
///
///  \author Mark Garlanger
///
/// Modified for FujiNet Go H89 (modifications GPL-3.0-or-later):
///   Keeps a raw-pointer copy of the page table (see MemoryLayout.h).
///  \date   May 8, 2016
///

#include "MemoryLayout.h"

#include "NilMemory8K.h"

using namespace std;


MemoryLayout::MemoryLayout()
{
    for (int x = 0; x < 8; ++x)
    {
        memPage_m[x] = make_shared<NilMemory8K>(pageToAddress(x));
        rawPage_m[x] = memPage_m[x].get();
    }
}

void
MemoryLayout::addPage(shared_ptr<Memory8K> mem)
{
    // error if not NULL?
    memPage_m[addressToPage(mem->getBase())] = mem;
    rawPage_m[addressToPage(mem->getBase())] = mem.get();
}

void
MemoryLayout::addPageAt(shared_ptr<Memory8K> mem, WORD address)
{
    // error if not NULL?
    memPage_m[addressToPage(address)] = mem;
    rawPage_m[addressToPage(address)] = mem.get();
}
