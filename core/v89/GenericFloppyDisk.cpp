/// \file GenericFloppyDisk.cpp
///
/// \brief virtual Floppy Disk interface.
///
/// \date Feb 2, 2016
/// \author Douglas Miller
///
/// Modified for FujiNet Go H89 (modifications GPL-3.0-or-later):
///   loadDiskImage and determineFileType removed (core/machine/SoftDisk).
///

#include <algorithm>

#include "GenericFloppyDisk.h"

#include "logger.h"

using namespace std;


GenericFloppyDisk::GenericFloppyDisk(): writeProtect_m(false),
                                        doubleDensity_m(false),
                                        numTracks_m(0),
                                        numSectors_m(0),
                                        numSides_m(0),
                                        secSize_m(0),
                                        mediaSize_m(0),
                                        hypoTrack_m(false),
                                        hyperTrack_m(false)
{
}

GenericFloppyDisk::~GenericFloppyDisk()
{
}

void
GenericFloppyDisk::setDriveType(BYTE numTracks)
{
    // Check if drive/disk match
    if (numTracks == numTracks_m)
    {
        // they match
        hypoTrack_m  = false;
        hyperTrack_m = false;
    }
    else if ((numTracks == 40) && (numTracks_m == 80))
    {
        // drive is only 40 tracks but disk is 80.
        hyperTrack_m = true;
        hypoTrack_m  = false;
    }
    else if ((numTracks == 80) && (numTracks_m == 40))
    {
        // drive is 80 tracks and disk is 40.
        hyperTrack_m = false;
        hypoTrack_m  = true;
    }
    else
    {
        debugss(ssFloppyDisk, ERROR, "unknown drive/disk tracks: %d/%d\n", numTracks, numTracks_m);
    }
}

void
GenericFloppyDisk::setWriteProtect(bool value)
{
    writeProtect_m = value;
}

BYTE
GenericFloppyDisk::getRealTrackNumber(BYTE track)
{
    // possibly update track if format mismatch between drive and disk
    if (hyperTrack_m)
    {
        // skip every other track
        return track * 2;
    }
    else if (hypoTrack_m)
    {
        // read each track twice
        return track / 2;
    }

    return track;
}


string
GenericFloppyDisk::getMediaName()
{
    if (imageName_m.empty())
    {
        return "NONE";
    }

    return imageName_m;
}
