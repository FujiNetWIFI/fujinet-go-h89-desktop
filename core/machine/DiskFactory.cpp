/*
 * DiskFactory -- see DiskFactory.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "DiskFactory.h"

#include "DiskImage.h"
#include "GenericDiskDrive.h"
#include "GenericFloppyDisk.h"
#include "SectorFloppyImage.h"
#include "propertyutil.h"

#include <filesystem>

namespace
{

/// VirtualH89's soft-sectored images as they come: not written back by the
/// host (SectorFloppyImage and RawFloppyImage write their file directly;
/// IMD and TD0 keep changes in memory only).
class LegacySoftImage: public DiskImage
{
  public:
    LegacySoftImage(const std::string& path, const char* format, bool ro): format_m(format)
    {
        path_m    = path;
        protect_m = ro;
    }
    const char* formatName() const override
    {
        return format_m;
    }

  protected:
    bool serialise(std::vector<uint8_t>&, std::string& err) override
    {
        err = "this image format is not written back yet";
        return false;
    }

  private:
    const char* format_m;
};

}

bool
loadSoftImage(const std::string& path, GenericDiskDrive* drive, bool readOnly, SoftMount& out,
              std::string& err)
{
    if (!std::filesystem::exists(std::filesystem::u8path(path)))
    {
        err = "cannot open " + path;
        return false;
    }
    std::string ext = diskio::extension(path);
    std::vector<std::string> args{path};
    if (!readOnly && diskio::writable(path))
    {
        args.push_back("rw");
    }

    const char* fmt = "SECTOR";
    if (ext == "imd" || ext == "td0")
    {
        out.disk = GenericFloppyDisk::loadDiskImage(args);
        fmt      = ext == "imd" ? "IMD" : "TD0";
    }
    else
    {
        out.disk = SectorFloppyImage::getDiskette(drive, args);
    }
    if (!out.disk)
    {
        err = path + " is not a soft-sectored image this app can read";
        return false;
    }
    out.disk->setWriteProtect(readOnly || !diskio::writable(path) || ext == "td0");
    out.image = std::make_shared<LegacySoftImage>(path, fmt, out.disk->checkWriteProtect());
    return true;
}
