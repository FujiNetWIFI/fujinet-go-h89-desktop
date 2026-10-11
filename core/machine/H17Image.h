/*
 * H17Image -- a hard-sectored 5.25" disk for the H17 controller, kept in
 * memory as raw tracks (3200 bytes each, exactly what VirtualH89's H17
 * reads and writes at 128 CPU cycles a byte), loaded from and written back
 * to whichever format the file is in:
 *
 *   RAW     VirtualH89's raw tracks (.h17raw, .rawdisk, .tmpdisk), and
 *           z88dk's "H89js RAW" .h17raw: 128000 / 256000 / 512000 bytes.
 *           Two-sided images come in two orders -- VirtualH89 puts side 1
 *           after all of side 0, z88dk interleaves sides per track -- told
 *           apart by the track number in the second track's headers.
 *   H8D     the popular sector dump: 256-byte sectors, 10 a track, tracks in
 *           order and the sides of a two-sided disk interleaved per track
 *           (102400 SS40 / 204800 DS40 or SS80 / 409600 DS80). The headers
 *           it does not keep are rebuilt: volume 0 on track 0 and on CP/M
 *           disks, the HDOS label's serial on the other tracks of an HDOS
 *           disk.
 *   H17D    Mark Garlanger's H17Disk (.h17disk, .h17), version 2 -- the
 *           H8D data plus each sector's header in SecM (z88dk and MAME
 *           write and read it) -- and the earlier draft, whose data block is
 *           every sector as it was read off the disk, header and gaps and
 *           all (the HDOS distribution images on heathkit.garlanger.com).
 *
 * Every sector the image synthesises is laid out as the real H17 ROM
 * writes it -- measured from those captured disks: the header's sync byte
 * 10 bytes after the sector hole, the data's at byte 32, zeros between.
 *
 * Writing back decodes each changed track's sectors from the raw bytes
 * (sync, header, checksum, data, checksum); a sector that does not decode
 * keeps what the file had. RAW and the draft H17D are written as raw bytes
 * and need no decoding.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 * (the hole timing is VirtualH89's HardSectoredDisk, BSD-3-Clause,
 * Mark Garlanger)
 */

#ifndef H89_H17IMAGE_H
#define H89_H17IMAGE_H

#include "DiskImage.h"
#include "FloppyDisk.h"

#include <memory>
#include <string>
#include <vector>

class H17Image: public FloppyDisk, public DiskImage
{
  public:
    static const unsigned bytesPerTrack_c  = 3200;
    static const unsigned sectorWindow_c   = 320;
    static const unsigned sectorsPerTrack_c = 10;
    static const unsigned sectorSize_c     = 256;

    enum Format
    {
        FormatRaw,
        FormatH8D,
        FormatH17D,       // version 2
        FormatH17DDraft,  // the draft, with captured sectors
    };

    /// geometry hints for the H8D sizes that are ambiguous
    enum Hint
    {
        HintNone = 0,
        HintSS80,         // a 204800-byte H8D is 80 tracks, one side
        HintDS40,         // ... is 40 tracks, two sides
    };

    /// Loads `path`; null with the reason in err.
    static std::unique_ptr<H17Image> load(const std::string& path, std::string& err,
                                          Hint hint = HintNone);
    /// A new, empty image: H8D/H17D formatted (every sector present, zero
    /// data, volume 0), RAW unformatted.
    static std::unique_ptr<H17Image> blank(Format format, unsigned sides, unsigned tracks);
    /// Writes a new blank image file.
    static bool create(const std::string& path, Format format, unsigned sides,
                       unsigned tracks, std::string& err);

    // ---- FloppyDisk (VirtualH89's H17 drives call these) ----
    bool readData(BYTE side, BYTE track, unsigned long pos, BYTE& data) override;
    bool writeData(BYTE side, BYTE track, unsigned long pos, BYTE data) override;
    void getControlInfo(unsigned long pos, bool& hole, bool& writeProtect) override;
    bool readSectorData(BYTE side, BYTE track, BYTE sector, WORD pos, BYTE& data) override;
    void eject(const char* name) override;
    void dump(void) override;

    // ---- DiskImage ----
    const char* formatName() const override;

    unsigned sides() const
    {
        return sides_m;
    }
    unsigned tracks() const
    {
        return tracks_m;
    }
    Format format() const
    {
        return format_m;
    }

    /// The raw track (for the tests and the debugger).
    const uint8_t* rawTrack(unsigned side, unsigned track) const;

    /// Decodes one sector from a raw track: false when it does not decode.
    static bool decodeSector(const uint8_t* window, unsigned window_len, uint8_t& vol,
                             uint8_t& trk, uint8_t& sec, uint8_t* data256);
    /// Lays down one sector as the H17 ROM formats it.
    static void encodeSector(uint8_t* window, uint8_t vol, uint8_t trk, uint8_t sec,
                             const uint8_t* data256);
    static uint8_t checksum(const uint8_t* p, size_t n);

  protected:
    bool serialise(std::vector<uint8_t>& out, std::string& err) override;

  private:
    H17Image(Format format, unsigned sides, unsigned tracks);

    uint8_t* raw(unsigned side, unsigned track)
    {
        return &raw_m[((size_t) side * tracks_m + track) * bytesPerTrack_c];
    }
    bool mapTrack(BYTE side, BYTE& track) const;

    bool loadRaw(const std::vector<uint8_t>& file, std::string& err);
    bool loadH8D(const std::vector<uint8_t>& file, Hint hint, std::string& err);
    bool loadH17D(const std::vector<uint8_t>& file, std::string& err);
    bool loadH17DDraft(const std::vector<uint8_t>& file, std::string& err);
    void buildFromSectors(const std::vector<uint8_t>& data, const std::vector<uint8_t>* secm,
                          int volume);

    bool serialiseSectors(std::vector<uint8_t>& sectors);

    Format               format_m;
    unsigned             sides_m;
    unsigned             tracks_m;
    bool                 rawSideMajor_m;   // RAW: VirtualH89's order
    std::vector<uint8_t> raw_m;
    std::vector<uint8_t> file_m;           // the file as loaded (H8D/H17D are patched)
    // H17D: where each sector's 256 bytes and metadata are in file_m
    std::vector<size_t>  dataOff_m, metaOff_m;
    // draft H17D: where each captured sector is in file_m, and its length
    std::vector<size_t>  captureOff_m, captureLen_m;
};

#endif /* H89_H17IMAGE_H */
