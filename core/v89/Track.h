///
/// \name Track.h
///
///
/// \date Jun 21, 2013
/// \author Mark Garlanger
///
/// Modified for FujiNet Go H89 (modifications GPL-3.0-or-later):
///   read-only accessors for the sectors, density and data rate (the TD0
///   decoder's tracks become core/machine/SoftDisk's).
///

#ifndef TRACK_H_
#define TRACK_H_

#include "h89Types.h"

/// \cond
#include <vector>
#include <memory>
/// \endcond

class Sector;

class Track
{
  public:
    enum DataRate
    {
        dr_Unknown,
        dr_250kbps,
        dr_300kbps,
        dr_500kbps
    };

    enum Density
    {
        density_Unknown,
        singleDensity,
        FM  = singleDensity,
        doubleDensity,
        MFM = doubleDensity
    };

  private:
    BYTE                                   sideNum_m;
    BYTE                                   trackNum_m;

    std::vector <std::shared_ptr<Sector> > sectors_m;

    Density                                density_m;
    DataRate                               dataRate_m;

    enum FormattingState
    {
        fs_none,
        fs_waitingForIndex,
        fs_writingGap,
        fs_writingIdRecord,
        fs_writingUserData
    };

    FormattingState formattingMode_m;


  public:
    Track(BYTE sideNum,
          BYTE trackNum);
    virtual ~Track();

    BYTE getTrackNumber();
    BYTE getSideNumber();

    bool addSector(std::shared_ptr<Sector> sector);

    void setDensity(Density density);
    void setDataRate(DataRate datarate);

    void dump();

    std::shared_ptr<Sector> findSector(BYTE sector);

    const std::vector<std::shared_ptr<Sector> >& sectors() const
    {
        return sectors_m;
    }
    Density density() const
    {
        return density_m;
    }
    DataRate dataRate() const
    {
        return dataRate_m;
    }

    void startFormat();

};

#endif // TRACK_H_
