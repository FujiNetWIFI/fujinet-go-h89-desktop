/*
 * DiskImage -- what every image this app mounts has in common, whatever
 * controller it is in: the file it came from, its format, whether it can be
 * written, and writing its changes back.
 *
 * Images live in memory; the emulator writes into that copy, and the host
 * writes the copy back to the file (flush) a moment after the writes stop,
 * on eject, at power cycle and at quit -- never on every sector write.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_DISKIMAGE_H
#define H89_DISKIMAGE_H

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

class DiskImage
{
  public:
    virtual ~DiskImage() = default;

    const std::string& path() const
    {
        return path_m;
    }
    /// "RAW", "H8D", "H17D", "IMD", "TD0", "SECTOR", ...
    virtual const char* formatName() const = 0;
    /// The format cannot be written at all (TD0).
    virtual bool readOnlyFormat() const
    {
        return false;
    }

    /// The write-protect tab (or a file the user cannot write).
    bool writeProtected() const
    {
        return protect_m;
    }
    void setWriteProtected(bool wp)
    {
        protect_m = wp || readOnlyFormat();
    }

    bool dirty() const
    {
        return dirty_m.load();
    }
    /// Bumped by every write (the host's debounce watches it).
    uint64_t writeCount() const
    {
        return writes_m.load();
    }

    /// Writes the in-memory image back to its file. Called with the machine
    /// stopped or under the run lock. Returns false with the reason.
    bool flush(std::string& err);

  protected:
    /// Serialise the whole image as the file should now hold it.
    virtual bool serialise(std::vector<uint8_t>& out, std::string& err) = 0;

    void touched()
    {
        writes_m.fetch_add(1);
        dirty_m.store(true);
    }

    std::string           path_m;
    bool                  protect_m = false;
    std::atomic<bool>     dirty_m{false};
    std::atomic<uint64_t> writes_m{0};
};

namespace diskio
{
/// The whole file; false with the reason.
bool readFile(const std::string& path, std::vector<uint8_t>& out, std::string& err);
/// Writes `data` as the file at `path`: to a temporary file beside it, then
/// renamed over it, so a crash never leaves half an image. Where the folder
/// is not writable (a file opened through the Flatpak document portal), the
/// file is written in place instead.
bool writeFile(const std::string& path, const std::vector<uint8_t>& data, std::string& err);
/// Whether the user may write the file.
bool writable(const std::string& path);
/// Lower-case extension without the dot ("h8d"), "" when none.
std::string extension(const std::string& path);
}

#endif /* H89_DISKIMAGE_H */
