/*
 * DiskFactory -- opening a soft-sectored image (for the H37 and the
 * MMS 77316, which both put a WD1797 in front of VirtualH89's
 * GenericFloppyDrive) as the object the drive takes plus the DiskImage the
 * host writes back.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_DISKFACTORY_H
#define H89_DISKFACTORY_H

#include <memory>
#include <string>

class DiskImage;
class GenericDiskDrive;
class GenericFloppyDisk;

struct SoftMount
{
    std::shared_ptr<GenericFloppyDisk> disk;    // what the drive reads and writes
    std::shared_ptr<DiskImage>         image;   // the same object, as an image
};

/// Opens `path` for `drive`; false with the reason.
bool loadSoftImage(const std::string& path, GenericDiskDrive* drive, bool readOnly,
                   SoftMount& out, std::string& err);

#endif /* H89_DISKFACTORY_H */
