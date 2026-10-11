/// \file GenericFloppyDisk.h
///
/// \brief virtual Floppy Disk interface.
///
/// \date Feb 2, 2016
///
/// \author Douglas Miller
///
/// Modified for FujiNet Go H89 (modifications GPL-3.0-or-later):
///   checkWriteProtect is virtual (an image's tab can change while it is
///   mounted); densityMatches, trackFormatted and formatTrack let the media
///   answer per track and take a WD1797 Write Track. The image loader
///   (loadDiskImage) went with the IMD, sector and raw image classes it
///   chose between: core/machine/SoftDisk replaces them.
///

#ifndef GENERICFLOPPYDISK_H_
#define GENERICFLOPPYDISK_H_


#include "h89Types.h"

/// \cond
#include <string>
#include <memory>
#include <vector>
/// \endcond

class Sector;
class Track;

class GenericFloppyDisk;
typedef std::shared_ptr<GenericFloppyDisk> GenericFloppyDisk_ptr;

/// \class GenericFloppyDisk
///
/// \brief A virtual floppy disk
///
class GenericFloppyDisk
{
  public:
    GenericFloppyDisk();
    virtual ~GenericFloppyDisk();

    void setWriteProtect(bool value);

    virtual bool checkWriteProtect()
    {
        return writeProtect_m;
    }

    /// Whether the track under the head (physical track, side) is recorded
    /// in this density; the controller finds no ID field otherwise.
    virtual bool densityMatches(bool doubleDensity, BYTE track, BYTE side)
    {
        (void) track;
        (void) side;
        return doubleDensity == doubleDensity_m;
    }
    /// Whether the track has any ID fields at all.
    virtual bool trackFormatted(BYTE track, BYTE side)
    {
        (void) track;
        (void) side;
        return true;
    }
    /// A WD1797 Write Track: `stream` is every byte the host wrote from index
    /// to index (F5-F7 still the controller's codes). False: a write fault.
    virtual bool formatTrack(BYTE track, BYTE side, bool doubleDensity,
                             const std::vector<BYTE>& stream)
    {
        (void) track;
        (void) side;
        (void) doubleDensity;
        (void) stream;
        return false;
    }
    bool doubleDensity()
    {
        return doubleDensity_m;
    }

    virtual bool readData(BYTE track,
                          BYTE side,
                          BYTE sector,
                          int  inSector,
                          int& data) = 0;
    virtual bool writeData(BYTE track,
                           BYTE side,
                           BYTE sector,
                           int  inSector,
                           BYTE data,
                           bool dataReady,
                           int& result) = 0;

    virtual BYTE getRealTrackNumber(BYTE track);
    virtual bool findSector(BYTE sideNum,
                            BYTE trackNum,
                            BYTE sectorNum)    = 0;

    virtual bool isReady()                     = 0;
    virtual void eject(const std::string name) = 0;
    virtual void dump(void)                    = 0;
    virtual std::string getMediaName();

    enum DiskSize_t
    {
        FiveAndQuarterInch,
        EightInch
    };
    void setDriveType(BYTE numTracks);

  private:

  protected:

    std::string imageName_m;
    bool        writeProtect_m;
    bool        doubleDensity_m;
    unsigned    trackLen_m;
    int         numTracks_m;
    int         numSectors_m;
    int         numSides_m;
    int         secSize_m;
    int         mediaSize_m;

    ///

    bool        hypoTrack_m;  // ST media in DT drive
    bool        hyperTrack_m; // DT media in ST drive

};


#endif // GENERICFLOPPYDISK_H_
