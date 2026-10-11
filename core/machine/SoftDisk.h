/*
 * SoftDisk -- a soft-sectored diskette for the WD1797 controllers (H37,
 * MMS 77316): every track's ID fields and data in memory, read and written
 * by the controller through VirtualH89's GenericFloppyDisk interface, and
 * written back to the file it came from by the host (DiskImage).
 *
 * Files it reads:
 *   IMD     ImageDisk (any geometry; mixed densities, sector size tables,
 *           cylinder and head maps, compressed, deleted and bad sectors)
 *   TD0     Teledisk, through VirtualH89's decoder -- read-only
 *   SECTOR  VirtualH89's sector image (.mmsdisk/.logdisk): the sectors in
 *           order and a 128-byte geometry trailer ("5m512z9p2s40t1d0i")
 *   RAW     VirtualH89's raw track image: each track's bytes with FC/FE/FB
 *           marks and zero gaps, every track laid out alike
 * Every format but TD0 is written back as itself. A track the H89 formats
 * (Write Track) is kept whatever its layout in an IMD; a sector or raw image
 * takes only the geometry its file already has, and anything else is a
 * write fault (an IMD is the format for disks the H89 formats freely).
 *
 * Tracks are named by cylinder and head as recorded; the drive's physical
 * track maps to a cylinder through GenericFloppyDisk's 40/80-track rule.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_SOFTDISK_H
#define H89_SOFTDISK_H

#include "DiskImage.h"
#include "GenericFloppyDisk.h"

#include <memory>
#include <string>
#include <vector>

class SoftDisk: public GenericFloppyDisk, public DiskImage
{
  public:
    enum Format
    {
        FormatIMD,
        FormatTD0,
        FormatSector,
        FormatRaw,
    };

    struct Sector
    {
        uint8_t              cyl = 0, head = 0, sec = 0, size = 0;   // the ID field
        bool                 deleted  = false;
        bool                 crcError = false;
        bool                 noData   = false;   // the ID only (IMD type 0)
        std::vector<uint8_t> data;
        long                 rawOffset   = -1;   // RAW: the data's place in the track
        long                 rawIdOffset = -1;   // RAW: the ID mark's
    };
    struct Track
    {
        bool                 present = false;    // formatted (has a record in the file)
        bool                 mfm     = false;
        int                  imdMode = -1;       // IMD's mode byte, kept on write-back
        std::vector<Sector>  sectors;            // in the order they pass the head
        std::vector<uint8_t> raw;                // RAW: the track's bytes
    };

    /// Reads `path`. `mediaInches` (5 or 8) and the drive's track count say
    /// what a raw image's track length means and what geometry a blank IMD
    /// takes. Null with the reason when it is not an image this reads.
    static std::shared_ptr<SoftDisk> load(const std::string& path, int mediaInches,
                                          int driveTracks, bool readOnly, std::string& err);
    /// Recognises the format from the name and the contents; false if none.
    static bool probe(const std::string& path, Format& fmt);
    /// An ImageDisk with no tracks: the H89 formats it.
    static bool createBlankImd(const std::string& path, std::string& err);

    Format format() const
    {
        return format_m;
    }
    int cylinders() const
    {
        return numTracks_m;
    }
    int sides() const
    {
        return numSides_m;
    }
    const Track* track(int cyl, int head) const;

    // ---- GenericFloppyDisk -------------------------------------------------
    bool readData(BYTE track, BYTE side, BYTE sector, int inSector, int& data) override;
    bool writeData(BYTE track, BYTE side, BYTE sector, int inSector, BYTE data, bool dataReady,
                   int& result) override;
    bool findSector(BYTE side, BYTE track, BYTE sector) override;
    bool isReady() override
    {
        return true;
    }
    void eject(const std::string name) override
    {
        (void) name;
    }
    void dump() override
    {
    }
    std::string getMediaName() override
    {
        return path_m;
    }
    bool checkWriteProtect() override
    {
        return writeProtected();
    }
    bool densityMatches(bool doubleDensity, BYTE track, BYTE side) override;
    bool trackFormatted(BYTE track, BYTE side) override;
    bool formatTrack(BYTE track, BYTE side, bool doubleDensity,
                     const std::vector<BYTE>& stream) override;

    // ---- DiskImage ---------------------------------------------------------
    const char* formatName() const override;
    bool        readOnlyFormat() const override
    {
        return format_m == FormatTD0;
    }

  protected:
    bool serialise(std::vector<uint8_t>& out, std::string& err) override;

  private:
    explicit SoftDisk(Format fmt);

    Track* trackAt(int cyl, int head);
    Track* physTrack(BYTE track, BYTE side);
    Sector* locate(Track* t, BYTE cyl, BYTE sector);
    int    mediaCylinder(BYTE track);

    bool loadIMD(const std::vector<uint8_t>& f, std::string& err);
    bool loadTD0(std::string& err);
    bool loadSector(const std::vector<uint8_t>& f, std::string& err);
    bool loadRaw(const std::vector<uint8_t>& f, int driveSdBytes, std::string& err);
    bool parseRawTrack(Track& t, int cyl, int head);

    bool saveIMD(std::vector<uint8_t>& out, std::string& err);
    bool saveSector(std::vector<uint8_t>& out, std::string& err);
    bool saveRaw(std::vector<uint8_t>& out, std::string& err);

    Format                     format_m;
    int                        mediaInches_m = 5;
    std::vector<Track>         tracks_m;      // [cyl * 2 + head]
    std::vector<uint8_t>       imdHeader_m;   // the comment, up to and with its 0x1A

    // SECTOR and RAW: the file's geometry
    bool                       interlaced_m = false;
    std::vector<uint8_t>       trailer_m;     // SECTOR: the 128-byte geometry text
    long                       rawTrackLen_m = 0;

    // the transfer the controller is in the middle of
    const uint8_t*             xfer_m     = nullptr;
    size_t                     xferLen_m  = 0;
    size_t                     xferPos_m  = 0;
    bool                       xferBad_m  = false;   // ends in a CRC error
    Sector*                    writing_m  = nullptr;
    Track*                     writingTrack_m = nullptr;
    uint8_t                    idField_m[6] = {};
    std::vector<uint8_t>       readTrack_m;
    unsigned                   nextId_m   = 0;       // Read Address goes round the IDs
};

#endif /* H89_SOFTDISK_H */
