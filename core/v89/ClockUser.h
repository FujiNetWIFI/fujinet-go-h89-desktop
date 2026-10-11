///
/// \name ClockUser.h
///
///  \date Aug 11, 2012
///  \author Mark Garlanger
///
/// Modified for FujiNet Go H89 (modifications GPL-3.0-or-later):
///   Remembers the clock it registered with, so it unregisters from that one.
///

#ifndef CLOCKUSER_H_
#define CLOCKUSER_H_



class WallClock;

class ClockUser
{
  public:
    ClockUser();
    virtual ~ClockUser();

    virtual void notification(unsigned int cycleCount) = 0;

  protected:
    /// The clock this user registered with (its machine's).
    WallClock* wallClock_m;

  private:

};



#endif // CLOCKUSER_H_
