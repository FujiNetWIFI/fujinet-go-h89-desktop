/*
 * H17Image -- see H17Image.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "H17Image.h"

#include <cstring>

namespace
{

// Where the H17 ROM puts a sector inside its 320-byte window, measured from
// disks captured with an FC5025 (the H17Disk draft images): the header's
// sync 9-12 bytes after the hole, the data's 30-33.
const unsigned headerSync_c = 10;
const unsigned dataSync_c   = 32;
const uint8_t  sync_c       = 0xfd;

uint32_t
be32(const uint8_t* p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

uint16_t
be16(const uint8_t* p)
{
    return (uint16_t) ((p[0] << 8) | p[1]);
}

void
putBe32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

/// The volume serial of an HDOS disk, from its label (track 0, sector 9),
/// or -1 when sector 9 does not look like an HDOS label (a CP/M disk).
int
hdosVolume(const uint8_t* label)
{
    unsigned siz = label[12] | (label[13] << 8);
    unsigned dis = label[3] | (label[4] << 8);
    unsigned grt = label[5] | (label[6] << 8);
    unsigned spg = label[7];

    if (siz != 400 && siz != 800 && siz != 1600)
    {
        return -1;
    }
    if (spg != 1 && spg != 2 && spg != 4 && spg != 8)
    {
        return -1;
    }
    if (!grt || grt >= siz || !dis || dis >= siz)
    {
        return -1;
    }
    return label[0];
}

}

uint8_t
H17Image::checksum(const uint8_t* p, size_t n)
{
    // the H17 ROM's: exclusive-or, then rotate left
    uint8_t d = 0;
    for (size_t i = 0; i < n; ++i)
    {
        d ^= p[i];
        d = (uint8_t) ((d << 1) | (d >> 7));
    }
    return d;
}

void
H17Image::encodeSector(uint8_t* w, uint8_t vol, uint8_t trk, uint8_t sec, const uint8_t* data)
{
    memset(w, 0, sectorWindow_c);
    uint8_t hdr[3] = {vol, trk, sec};
    w[headerSync_c]     = sync_c;
    w[headerSync_c + 1] = vol;
    w[headerSync_c + 2] = trk;
    w[headerSync_c + 3] = sec;
    w[headerSync_c + 4] = checksum(hdr, 3);
    w[dataSync_c]       = sync_c;
    memcpy(&w[dataSync_c + 1], data, sectorSize_c);
    w[dataSync_c + 1 + sectorSize_c] = checksum(data, sectorSize_c);
}

bool
H17Image::decodeSector(const uint8_t* w, unsigned len, uint8_t& vol, uint8_t& trk, uint8_t& sec,
                       uint8_t* data)
{
    // the header: the first sync with a good checksum behind it
    for (unsigned i = 0; i + 5 <= len; ++i)
    {
        if (w[i] != sync_c || checksum(&w[i + 1], 3) != w[i + 4])
        {
            continue;
        }
        // the data: the next sync with a good checksum 256 bytes on
        for (unsigned j = i + 5; j + 1 + sectorSize_c + 1 <= len; ++j)
        {
            if (w[j] == sync_c && checksum(&w[j + 1], sectorSize_c) == w[j + 1 + sectorSize_c])
            {
                vol = w[i + 1];
                trk = w[i + 2];
                sec = w[i + 3];
                memcpy(data, &w[j + 1], sectorSize_c);
                return true;
            }
        }
        return false;
    }
    return false;
}

H17Image::H17Image(Format format, unsigned sides, unsigned tracks):
    format_m(format),
    sides_m(sides),
    tracks_m(tracks),
    rawSideMajor_m(true),
    raw_m((size_t) sides * tracks * bytesPerTrack_c, 0)
{
}

std::unique_ptr<H17Image>
H17Image::load(const std::string& path, std::string& err, Hint hint)
{
    std::vector<uint8_t> file;
    if (!diskio::readFile(path, file, err))
    {
        return nullptr;
    }

    std::unique_ptr<H17Image> img;
    bool                      ok = false;

    if (file.size() >= 8 && memcmp(file.data(), "H17D", 4) == 0)
    {
        bool v2 = file[7] == 0xff && file[4] >= '0' && file[4] <= '9';
        img.reset(new H17Image(v2 ? FormatH17D : FormatH17DDraft, 1, 40));
        ok = v2 ? img->loadH17D(file, err) : img->loadH17DDraft(file, err);
    }
    else if (file.size() % (sectorsPerTrack_c * sectorSize_c) == 0 &&
             (file.size() == 102400 || file.size() == 204800 || file.size() == 409600))
    {
        img.reset(new H17Image(FormatH8D, 1, 40));
        ok = img->loadH8D(file, hint, err);
    }
    else if (file.size() && file.size() % bytesPerTrack_c == 0 && file.size() <= 512000)
    {
        img.reset(new H17Image(FormatRaw, 1, 40));
        ok = img->loadRaw(file, err);
    }
    else
    {
        err = "not an H17 disk image (" + std::to_string(file.size()) +
              " bytes; expected a raw, H8D or H17Disk image)";
        return nullptr;
    }

    if (!ok)
    {
        return nullptr;
    }
    img->path_m = path;
    img->file_m.swap(file);
    if (img->format_m == FormatRaw || img->format_m == FormatH8D)
    {
        img->file_m.clear();   // rebuilt whole on write-back
    }
    if (!diskio::writable(path))
    {
        img->protect_m = true;
    }
    img->FloppyDisk::setWriteProtect(img->protect_m);
    return img;
}

bool
H17Image::loadRaw(const std::vector<uint8_t>& file, std::string& err)
{
    unsigned nTracks = (unsigned) (file.size() / bytesPerTrack_c);

    // VirtualH89's loader reads up to 80 tracks of side 0, then side 1
    if (nTracks == 40 || nTracks == 80)
    {
        sides_m  = 1;
        tracks_m = nTracks;
    }
    else if (nTracks == 160)
    {
        sides_m  = 2;
        tracks_m = 80;
    }
    else if (nTracks < 80)
    {
        sides_m  = 1;
        tracks_m = 40;
    }
    else
    {
        err = "a raw H17 image must be 40, 80 or 160 tracks of 3200 bytes";
        return false;
    }
    raw_m.assign((size_t) sides_m * tracks_m * bytesPerTrack_c, 0);

    // Which order are the sides in? The second track of the file is side 0
    // track 1 in VirtualH89's order, side 1 track 0 in z88dk's.
    rawSideMajor_m = true;
    if (sides_m == 2)
    {
        uint8_t vol, trk, sec, data[sectorSize_c];
        for (unsigned w = 0; w < sectorsPerTrack_c; ++w)
        {
            if (decodeSector(&file[bytesPerTrack_c + w * sectorWindow_c], sectorWindow_c, vol, trk,
                             sec, data))
            {
                rawSideMajor_m = (trk != 0);
                break;
            }
        }
    }

    for (unsigned t = 0; t < nTracks; ++t)
    {
        unsigned side, track;
        if (rawSideMajor_m)
        {
            side  = t / tracks_m;
            track = t % tracks_m;
        }
        else
        {
            side  = t % sides_m;
            track = t / sides_m;
        }
        if (side < sides_m && track < tracks_m)
        {
            memcpy(raw(side, track), &file[(size_t) t * bytesPerTrack_c], bytesPerTrack_c);
        }
    }
    return true;
}

void
H17Image::buildFromSectors(const std::vector<uint8_t>& data, const std::vector<uint8_t>* secm,
                           int volume)
{
    // data: 256-byte sectors, tracks in order, sides interleaved per track
    for (unsigned t = 0; t < tracks_m; ++t)
    {
        for (unsigned s = 0; s < sides_m; ++s)
        {
            uint8_t* track = raw(s, t);
            for (unsigned sec = 0; sec < sectorsPerTrack_c; ++sec)
            {
                size_t   idx = ((size_t) t * sides_m + s) * sectorsPerTrack_c + sec;
                uint8_t  vol = (t == 0 && s == 0) ? 0 : (uint8_t) (volume > 0 ? volume : 0);
                uint8_t  hs  = (uint8_t) sec;
                const uint8_t* src = &data[idx * sectorSize_c];
                if (secm)
                {
                    // the header as recorded, and the data where SecM says
                    const uint8_t* m = &(*secm)[idx * 16];
                    vol              = m[6];
                    hs               = m[8];
                    (void) src;
                    src = nullptr;
                    size_t off = be32(m);
                    if (off + sectorSize_c <= file_m.size())
                    {
                        src = &file_m[off];
                    }
                }
                static const uint8_t zeros[sectorSize_c] = {0};
                encodeSector(&track[sec * sectorWindow_c], vol, (uint8_t) t, hs, src ? src : zeros);
            }
        }
    }
}

bool
H17Image::loadH8D(const std::vector<uint8_t>& file, Hint hint, std::string& err)
{
    (void) err;
    switch (file.size())
    {
        case 102400:
            sides_m  = 1;
            tracks_m = 40;
            break;
        case 204800:
            sides_m  = hint == HintSS80 ? 1 : 2;
            tracks_m = hint == HintSS80 ? 80 : 40;
            break;
        default:
            sides_m  = 2;
            tracks_m = 80;
            break;
    }
    raw_m.assign((size_t) sides_m * tracks_m * bytesPerTrack_c, 0);
    buildFromSectors(file, nullptr, hdosVolume(&file[9 * sectorSize_c]));
    return true;
}

bool
H17Image::loadH17D(const std::vector<uint8_t>& file, std::string& err)
{
    // version 2: "H17D" "200" 0xFF, then blocks of 4-character id + BE32 length
    size_t   p = 8;
    size_t   h8db = 0, h8dbLen = 0, secm = 0, secmLen = 0;
    bool     haveFmt = false;
    bool     ro      = false;

    while (p + 8 <= file.size())
    {
        uint32_t len = be32(&file[p + 4]);
        if (p + 8 + len > file.size())
        {
            break;
        }
        const uint8_t* id   = &file[p];
        const uint8_t* body = &file[p + 8];
        if (!memcmp(id, "DskF", 4) && len >= 2)
        {
            sides_m  = body[0];
            tracks_m = body[1];
            ro       = len >= 3 && body[2];
            haveFmt  = true;
        }
        else if (!memcmp(id, "H8DB", 4))
        {
            h8db    = p + 8;
            h8dbLen = len;
        }
        else if (!memcmp(id, "SecM", 4))
        {
            secm    = p + 8;
            secmLen = len;
        }
        p += 8 + len;
        if (len == 0 && !memcmp(id, "\0\0\0\0", 4) && p < 248)
        {
            // zero padding written without a Padd block: the H8DB block
            // is required to start at 248
            p = 248;
        }
    }
    if (!haveFmt || (sides_m != 1 && sides_m != 2) || (tracks_m != 40 && tracks_m != 80))
    {
        err = "the H17Disk image has no usable DskF block";
        return false;
    }
    size_t sectors = (size_t) sides_m * tracks_m * sectorsPerTrack_c;
    if (!h8db || h8dbLen < sectors * sectorSize_c)
    {
        err = "the H17Disk image's H8DB block is missing or short";
        return false;
    }
    raw_m.assign((size_t) sides_m * tracks_m * bytesPerTrack_c, 0);
    file_m = file; // buildFromSectors reads sector data through SecM offsets
    std::vector<uint8_t> data(file.begin() + (long) h8db,
                              file.begin() + (long) (h8db + sectors * sectorSize_c));
    if (secm && secmLen >= sectors * 16)
    {
        std::vector<uint8_t> meta(file.begin() + (long) secm,
                                  file.begin() + (long) (secm + sectors * 16));
        buildFromSectors(data, &meta, 0);
        dataOff_m.resize(sectors);
        metaOff_m.resize(sectors);
        for (size_t i = 0; i < sectors; ++i)
        {
            metaOff_m[i] = secm + i * 16;
            dataOff_m[i] = be32(&file[metaOff_m[i]]);
            if (dataOff_m[i] + sectorSize_c > file.size())
            {
                dataOff_m[i] = h8db + i * sectorSize_c;
            }
        }
    }
    else
    {
        buildFromSectors(data, nullptr, hdosVolume(&data[9 * sectorSize_c]));
        dataOff_m.resize(sectors);
        metaOff_m.assign(sectors, 0);
        for (size_t i = 0; i < sectors; ++i)
        {
            dataOff_m[i] = h8db + i * sectorSize_c;
        }
    }
    protect_m = ro;
    return true;
}

bool
H17Image::loadH17DDraft(const std::vector<uint8_t>& file, std::string& err)
{
    // the draft: "H17D" + 3 version bytes, then blocks of a 1-byte id,
    // 1-byte flags and BE32 length; the data block (0x10) holds every
    // track (0x11 head track length16) and in it every sector as read
    // (0x12 sector status length16 bytes...). The draft's text gives the
    // status 4 bytes; the published images use 1. Both are accepted.
    size_t p    = 7;
    bool   ro   = false;
    size_t data = 0, dataLen = 0;

    sides_m  = 1;
    tracks_m = 40;
    while (p + 6 <= file.size())
    {
        uint8_t  id  = file[p];
        uint8_t  flg = file[p + 1];
        uint32_t len = be32(&file[p + 2]);
        if (p + 6 + len > file.size())
        {
            break;
        }
        const uint8_t* body = &file[p + 6];
        if (id == 0x00 && len >= 1)
        {
            sides_m  = body[0];
            tracks_m = len >= 2 ? body[1] : 40;
        }
        else if (id == 0x01 && len >= 1)
        {
            ro = body[0] & 1;
        }
        else if (id == 0x10)
        {
            data    = p + 6;
            dataLen = len;
        }
        else if ((flg & 0x80) && id != 0x20)
        {
            err = "the H17Disk image needs a block this app does not know";
            return false;
        }
        p += 6 + len;
    }
    if ((sides_m != 1 && sides_m != 2) || (tracks_m != 40 && tracks_m != 80) || !data)
    {
        err = "the H17Disk image's format or data block is missing";
        return false;
    }

    raw_m.assign((size_t) sides_m * tracks_m * bytesPerTrack_c, 0);
    captureOff_m.assign((size_t) sides_m * tracks_m * sectorsPerTrack_c, 0);
    captureLen_m.assign(captureOff_m.size(), 0);

    size_t q = data, end = data + dataLen;
    while (q + 5 <= end)
    {
        if (file[q] != 0x11)
        {
            err = "the H17Disk image's data block is damaged";
            return false;
        }
        unsigned head = file[q + 1], trk = file[q + 2];
        size_t   tEnd = q + 5 + be16(&file[q + 3]);
        q += 5;
        if (tEnd > end)
        {
            tEnd = end;
        }
        while (q + 5 <= tEnd && file[q] == 0x12)
        {
            unsigned sec  = file[q + 1];
            size_t   hdr  = 5;
            size_t   slen = be16(&file[q + 3]);
            if (q + 5 + slen > tEnd || (q + 5 + slen < tEnd && file[q + 5 + slen] != 0x12))
            {
                hdr  = 8;
                slen = be16(&file[q + 6]);
            }
            if (q + hdr + slen > tEnd)
            {
                err = "the H17Disk image's data block is damaged";
                return false;
            }
            if (head < sides_m && trk < tracks_m && sec < sectorsPerTrack_c)
            {
                size_t n = slen < sectorWindow_c ? slen : sectorWindow_c;
                memcpy(raw(head, trk) + sec * sectorWindow_c, &file[q + hdr], n);
                size_t idx        = ((size_t) trk * sides_m + head) * sectorsPerTrack_c + sec;
                captureOff_m[idx] = q + hdr;
                captureLen_m[idx] = n;
            }
            q += hdr + slen;
        }
        q = tEnd;
    }
    protect_m = ro;
    return true;
}

std::unique_ptr<H17Image>
H17Image::blank(Format format, unsigned sides, unsigned tracks)
{
    std::unique_ptr<H17Image> img(new H17Image(format, sides, tracks));
    if (format != FormatRaw)
    {
        std::vector<uint8_t> zeros((size_t) sides * tracks * sectorsPerTrack_c * sectorSize_c, 0);
        img->buildFromSectors(zeros, nullptr, 0);
    }
    return img;
}

bool
H17Image::create(const std::string& path, Format format, unsigned sides, unsigned tracks,
                 std::string& err)
{
    if ((sides != 1 && sides != 2) || (tracks != 40 && tracks != 80))
    {
        err = "an H17 disk is 1 or 2 sides of 40 or 80 tracks";
        return false;
    }
    if (format == FormatH17DDraft)
    {
        format = FormatH17D;
    }
    std::unique_ptr<H17Image> img = blank(format, sides, tracks);
    img->path_m = path;
    std::vector<uint8_t> out;
    img->dirty_m.store(true);
    if (!img->serialise(out, err))
    {
        return false;
    }
    return diskio::writeFile(path, out, err);
}

// ---- FloppyDisk ------------------------------------------------------------

bool
H17Image::mapTrack(BYTE side, BYTE& track) const
{
    // a 40-track disk in an 80-track drive steps twice per track, and the
    // other way round (VirtualH89's HardSectoredDisk)
    if (maxTrack_m == 80 && tracks_m == 40)
    {
        track /= 2;
    }
    else if (maxTrack_m == 40 && tracks_m == 80)
    {
        track *= 2;
    }
    return side < sides_m && track < tracks_m;
}

bool
H17Image::readData(BYTE side, BYTE track, unsigned long pos, BYTE& data)
{
    if (pos >= bytesPerTrack_c || !mapTrack(side, track))
    {
        data = 0;
        return false;
    }
    data = raw(side, track)[pos];
    return true;
}

bool
H17Image::writeData(BYTE side, BYTE track, unsigned long pos, BYTE data)
{
    if (protect_m || pos >= bytesPerTrack_c || !mapTrack(side, track))
    {
        return false;
    }
    uint8_t& b = raw(side, track)[pos];
    if (b != data)
    {
        b = data;
        touched();
    }
    return true;
}

void
H17Image::getControlInfo(unsigned long pos, bool& hole, bool& writeProtect)
{
    // ten sector holes, each the first 64 bytes of its sector, and the
    // index hole half way through the last sector (VirtualH89's timing)
    hole         = (pos % sectorWindow_c) < 64 || (pos > 3040 && pos < 3104);
    writeProtect = protect_m;
}

bool
H17Image::readSectorData(BYTE, BYTE, BYTE, WORD, BYTE&)
{
    return false;
}

void
H17Image::eject(const char*)
{
    // the drive's eject: written back by the host, not here
}

void
H17Image::dump(void)
{
}

const char*
H17Image::formatName() const
{
    switch (format_m)
    {
        case FormatRaw:
            return "RAW";
        case FormatH8D:
            return "H8D";
        case FormatH17D:
            return "H17D";
        case FormatH17DDraft:
            return "H17D (draft)";
    }
    return "?";
}

const uint8_t*
H17Image::rawTrack(unsigned side, unsigned track) const
{
    if (side >= sides_m || track >= tracks_m)
    {
        return nullptr;
    }
    return &raw_m[((size_t) side * tracks_m + track) * bytesPerTrack_c];
}

// ---- writing back -----------------------------------------------------------

bool
H17Image::serialiseSectors(std::vector<uint8_t>& sectors)
{
    // decode every sector; one that does not decode keeps its old contents
    // (sectors holds them on entry), in the H8D order
    for (unsigned t = 0; t < tracks_m; ++t)
    {
        for (unsigned s = 0; s < sides_m; ++s)
        {
            const uint8_t* track = raw(s, t);
            for (unsigned w = 0; w < sectorsPerTrack_c; ++w)
            {
                uint8_t vol, trk, sec, data[sectorSize_c];
                if (!decodeSector(track + w * sectorWindow_c, sectorWindow_c, vol, trk, sec, data) ||
                    sec >= sectorsPerTrack_c)
                {
                    continue;
                }
                size_t idx = ((size_t) t * sides_m + s) * sectorsPerTrack_c + sec;
                memcpy(&sectors[idx * sectorSize_c], data, sectorSize_c);
            }
        }
    }
    return true;
}

bool
H17Image::serialise(std::vector<uint8_t>& out, std::string& err)
{
    (void) err;
    size_t sectors = (size_t) sides_m * tracks_m * sectorsPerTrack_c;

    switch (format_m)
    {
        case FormatRaw:
        {
            // in the order the file had
            out.resize(raw_m.size());
            unsigned n = sides_m * tracks_m;
            for (unsigned t = 0; t < n; ++t)
            {
                unsigned side  = rawSideMajor_m ? t / tracks_m : t % sides_m;
                unsigned track = rawSideMajor_m ? t % tracks_m : t / sides_m;
                memcpy(&out[(size_t) t * bytesPerTrack_c], raw(side, track), bytesPerTrack_c);
            }
            return true;
        }

        case FormatH8D:
        {
            // start from what the file holds (or zeros), so a sector that no
            // longer decodes keeps its last good contents
            std::vector<uint8_t> prev;
            std::string          e;
            if (!diskio::readFile(path_m, prev, e) || prev.size() != sectors * sectorSize_c)
            {
                prev.assign(sectors * sectorSize_c, 0);
            }
            serialiseSectors(prev);
            out.swap(prev);
            return true;
        }

        case FormatH17D:
        {
            if (file_m.empty())
            {
                // a new file: header, DskF, Labl, padding to 248, H8DB, SecM
                out.clear();
                const uint8_t hdr[8] = {'H', '1', '7', 'D', '2', '0', '0', 0xff};
                out.insert(out.end(), hdr, hdr + 8);
                const uint8_t dskf[11] = {'D', 's', 'k', 'F', 0, 0, 0, 3, (uint8_t) sides_m,
                                          (uint8_t) tracks_m, 0};
                out.insert(out.end(), dskf, dskf + 11);
                const char*   label   = "FujiNet Go H89";
                size_t        lablen  = strlen(label);
                const uint8_t labl[8] = {'L', 'a', 'b', 'l', 0, 0, 0, (uint8_t) lablen};
                out.insert(out.end(), labl, labl + 8);
                out.insert(out.end(), label, label + lablen);
                // a Padd block up to 248, so the H8DB data starts at 256
                // as the format requires (bare zero padding only works
                // when it happens to be a multiple of 8)
                uint8_t padd[8] = {'P', 'a', 'd', 'd'};
                putBe32(&padd[4], (uint32_t) (248 - out.size() - 8));
                out.insert(out.end(), padd, padd + 8);
                out.resize(248, 0);
                uint8_t h8db[8] = {'H', '8', 'D', 'B'};
                putBe32(&h8db[4], (uint32_t) (sectors * sectorSize_c));
                out.insert(out.end(), h8db, h8db + 8);
                out.resize(256 + sectors * sectorSize_c, 0);
                uint8_t secm[8] = {'S', 'e', 'c', 'M'};
                putBe32(&secm[4], (uint32_t) (sectors * 16));
                out.insert(out.end(), secm, secm + 8);
                size_t metaBase = out.size();
                out.resize(metaBase + sectors * 16, 0);
                dataOff_m.resize(sectors);
                metaOff_m.resize(sectors);
                for (size_t i = 0; i < sectors; ++i)
                {
                    dataOff_m[i] = 256 + i * sectorSize_c;
                    metaOff_m[i] = metaBase + i * 16;
                    putBe32(&out[metaOff_m[i]], (uint32_t) dataOff_m[i]);
                    out[metaOff_m[i] + 12] = 0x00;   // valid bytes, 256
                    out[metaOff_m[i] + 13] = 0x01;
                }
                file_m = out;
            }
            out = file_m;
            // patch each physical sector's data and metadata in place
            for (unsigned t = 0; t < tracks_m; ++t)
            {
                for (unsigned s = 0; s < sides_m; ++s)
                {
                    const uint8_t* track = raw(s, t);
                    for (unsigned w = 0; w < sectorsPerTrack_c; ++w)
                    {
                        uint8_t vol, trk, sec, data[sectorSize_c];
                        if (!decodeSector(track + w * sectorWindow_c, sectorWindow_c, vol, trk, sec,
                                          data))
                        {
                            continue;
                        }
                        size_t idx = ((size_t) t * sides_m + s) * sectorsPerTrack_c + w;
                        if (idx >= dataOff_m.size())
                        {
                            continue;
                        }
                        memcpy(&out[dataOff_m[idx]], data, sectorSize_c);
                        if (metaOff_m[idx])
                        {
                            uint8_t* m = &out[metaOff_m[idx]];
                            uint8_t  h[3] = {vol, trk, sec};
                            m[4]  = 0;
                            m[5]  = sync_c;
                            m[6]  = vol;
                            m[7]  = trk;
                            m[8]  = sec;
                            m[9]  = checksum(h, 3);
                            m[10] = sync_c;
                            m[11] = checksum(data, sectorSize_c);
                        }
                    }
                }
            }
            file_m = out;
            return true;
        }

        case FormatH17DDraft:
        {
            // the captured sectors are raw bytes: put each window back
            out = file_m;
            for (unsigned t = 0; t < tracks_m; ++t)
            {
                for (unsigned s = 0; s < sides_m; ++s)
                {
                    for (unsigned w = 0; w < sectorsPerTrack_c; ++w)
                    {
                        size_t idx = ((size_t) t * sides_m + s) * sectorsPerTrack_c + w;
                        if (captureOff_m[idx])
                        {
                            memcpy(&out[captureOff_m[idx]], raw(s, t) + w * sectorWindow_c,
                                   captureLen_m[idx]);
                        }
                    }
                }
            }
            file_m = out;
            return true;
        }
    }
    return false;
}
