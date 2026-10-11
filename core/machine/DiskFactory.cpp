/*
 * DiskFactory -- see DiskFactory.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "DiskFactory.h"

#include "GenericFloppyDrive.h"
#include "SoftDisk.h"

bool
loadSoftImage(const std::string& path, GenericFloppyDrive* drive, bool readOnly, SoftMount& out,
              std::string& err)
{
    int inches = drive && drive->getRawBytesPerTrack() > 3200 ? 8 : 5;
    int tracks = drive ? drive->getNumTracks() : 40;
    std::shared_ptr<SoftDisk> d = SoftDisk::load(path, inches, tracks, readOnly, err);
    if (!d)
    {
        return false;
    }
    out.disk  = d;
    out.image = d;
    return true;
}
