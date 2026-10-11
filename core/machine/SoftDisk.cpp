/*
 * SoftDisk -- see SoftDisk.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "SoftDisk.h"

#include "GenericFloppyFormat.h"
#include "Sector.h"
#include "TD0FloppyDisk.h"
#include "Track.h"
#include "TrackFormatter.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>

namespace
{

uint16_t
crc16(const uint8_t* p, size_t n, uint16_t crc = 0xffff)
{
    while (n--)
    {
        crc ^= (uint16_t) (*p++ << 8);
        for (int i = 0; i < 8; ++i)
        {
            crc = (crc & 0x8000) ? (uint16_t) ((crc << 1) ^ 0x1021) : (uint16_t) (crc << 1);
        }
    }
    return crc;
}

/// The size code (N) for a data length; -1 if it is not 128 << n.
int
sizeCode(size_t len)
{
    for (int n = 0; n < 7; ++n)
    {
        if (((size_t) 128 << n) == len)
        {
            return n;
        }
    }
    return -1;
}

size_t
sizeOf(uint8_t n)
{
    return (size_t) 128 << (n & 7);
}

/// Whole-track lengths: what a Read Track returns, index to index.
size_t
trackBytes(int inches, bool mfm)
{
    return (inches == 8 ? 5208 : 3125) * (mfm ? 2 : 1);
}

int
defaultImdMode(int inches, bool mfm)
{
    // ImageDisk: 0-2 FM at 500/300/250 kbps, 3-5 MFM at the same
    return inches == 8 ? (mfm ? 3 : 0) : (mfm ? 5 : 2);
}

/// "5m512z9p2s40t1d0i": VirtualH89's sector image geometry; false unless
/// every required field is there.
struct SectorGeometry
{
    int  media = 0, secSize = 0, spt = 0, sides = 0, tracks = 0;
    bool dd = false, interlaced = false;
};

bool
parseGeometry(const uint8_t* buf, size_t n, SectorGeometry& g)
{
    const char* b    = (const char*) buf;
    const char* end  = b + n;
    int         seen = 0;
    while (b < end && *b != '\n' && *b != '\0')
    {
        char*         e;
        unsigned long p = strtoul(b, &e, 0);
        if (e >= end || e == b)
        {
            return false;
        }
        switch (tolower((unsigned char) *e))
        {
            case 'm':
                if (p == 5 || p == 8)
                {
                    g.media = (int) p;
                    seen |= 0x01;
                }
                break;
            case 'z':
                if (p == 128 || p == 256 || p == 512 || p == 1024)
                {
                    g.secSize = (int) p;
                    seen |= 0x02;
                }
                break;
            case 'p':
                if (p > 0 && p < 256)
                {
                    g.spt = (int) p;
                    seen |= 0x04;
                }
                break;
            case 's':
                if (p == 1 || p == 2)
                {
                    g.sides = (int) p;
                    seen |= 0x08;
                }
                break;
            case 't':
                if (p > 0 && p < 256)
                {
                    g.tracks = (int) p;
                    seen |= 0x10;
                }
                break;
            case 'd':
                g.dd = p != 0;
                seen |= 0x20;
                break;
            case 'i':
                g.interlaced = p != 0;
                seen |= 0x40;
                break;
            case 'l':
                break;
            default:
                return false;
        }
        b = e + 1;
    }
    return seen == 0x7f;
}

bool
readGeometryTrailer(const std::vector<uint8_t>& f, SectorGeometry& g)
{
    if (f.size() <= 128)
    {
        return false;
    }
    if (!parseGeometry(&f[f.size() - 128], 128, g))
    {
        return false;
    }
    size_t want = (size_t) g.sides * g.tracks * g.spt * g.secSize;
    return want + 128 <= f.size();
}

/// A VirtualH89 raw track image: a track's FC index mark, ID fields (FE C
/// H R N crc crc) and data fields (FB data crc crc), zero gaps. Finds the
/// track length (the distance between the first two index marks).
long
rawTrackLength(const std::vector<uint8_t>& f)
{
    long   first = -1;
    size_t secLen = 0;
    size_t limit = std::min(f.size(), (size_t) 64 * 1024);
    for (size_t i = 0; i < limit; ++i)
    {
        uint8_t b = f[i];
        if (b == GenericFloppyFormat::INDEX_AM_BYTE)
        {
            if (first < 0)
            {
                first = (long) i;
            }
            else
            {
                return secLen ? (long) i - first : -1;
            }
        }
        else if (b == GenericFloppyFormat::ID_AM_BYTE && i + 6 < f.size())
        {
            secLen = sizeOf(f[i + 4] & 3);
            i += 6;
        }
        else if (b == GenericFloppyFormat::DATA_AM_BYTE && secLen)
        {
            i += secLen + 2;
        }
        else if (b != 0 && first < 0)
        {
            // something before the first mark that is not a gap
            return -1;
        }
    }
    return -1;
}

}

// ---- loading -------------------------------------------------------------

SoftDisk::SoftDisk(Format fmt): format_m(fmt)
{
    numSides_m  = 1;
    numTracks_m = 0;
}

bool
SoftDisk::probe(const std::string& path, Format& fmt)
{
    std::vector<uint8_t> f;
    std::string          err;
    if (!diskio::readFile(path, f, err) || f.size() < 4)
    {
        return false;
    }
    if (!memcmp(f.data(), "IMD ", 4))
    {
        fmt = FormatIMD;
        return true;
    }
    if ((f[0] == 'T' && f[1] == 'D') || (f[0] == 't' && f[1] == 'd'))
    {
        fmt = FormatTD0;
        return true;
    }
    SectorGeometry g;
    if (readGeometryTrailer(f, g))
    {
        fmt = FormatSector;
        return true;
    }
    if (rawTrackLength(f) > 0)
    {
        fmt = FormatRaw;
        return true;
    }
    return false;
}

std::shared_ptr<SoftDisk>
SoftDisk::load(const std::string& path, int mediaInches, int driveTracks, bool readOnly,
               std::string& err)
{
    Format fmt;
    if (!probe(path, fmt))
    {
        err = path + " is not a soft-sectored image this app can read";
        return nullptr;
    }

    std::shared_ptr<SoftDisk> d(new SoftDisk(fmt));
    d->path_m        = path;
    d->imageName_m   = path;
    d->mediaInches_m = mediaInches == 8 ? 8 : 5;

    std::vector<uint8_t> f;
    if (fmt != FormatTD0 && !diskio::readFile(path, f, err))
    {
        return nullptr;
    }
    bool ok = false;
    switch (fmt)
    {
        case FormatIMD:
            ok = d->loadIMD(f, err);
            break;
        case FormatTD0:
            ok = d->loadTD0(err);
            break;
        case FormatSector:
            ok = d->loadSector(f, err);
            break;
        case FormatRaw:
            ok = d->loadRaw(f, d->mediaInches_m == 8 ? 6400 : 3200, err);
            break;
    }
    if (!ok)
    {
        if (err.empty())
        {
            err = path + " is damaged";
        }
        return nullptr;
    }

    // the cylinder count the 40/80-track rule compares with the drive's
    int maxCyl = -1;
    for (size_t i = 0; i < d->tracks_m.size(); ++i)
    {
        if (d->tracks_m[i].present)
        {
            maxCyl = std::max(maxCyl, (int) (i / 2));
            if (i % 2)
            {
                d->numSides_m = 2;
            }
        }
    }
    if (fmt == FormatIMD || fmt == FormatTD0)
    {
        if (maxCyl < 0)
        {
            // a blank disk takes the drive's geometry when it is formatted
            d->numTracks_m = driveTracks;
            d->numSides_m  = 2;
        }
        else if (d->mediaInches_m == 8)
        {
            d->numTracks_m = 77;
        }
        else
        {
            d->numTracks_m = maxCyl < 42 ? 40 : 80;
        }
    }
    d->tracks_m.resize((size_t) std::max(d->numTracks_m, maxCyl + 1) * 2);
    d->setWriteProtected(readOnly || !diskio::writable(path));
    return d;
}

bool
SoftDisk::createBlankImd(const std::string& path, std::string& err)
{
    char      date[64];
    time_t    now = time(nullptr);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    strftime(date, sizeof date, "%d/%m/%Y %H:%M:%S", &tmv);
    std::string          hdr = std::string("IMD 1.18: ") + date + "\r\nFujiNet Go H89 blank disk\r\n";
    std::vector<uint8_t> out(hdr.begin(), hdr.end());
    out.push_back(0x1a);
    return diskio::writeFile(path, out, err);
}

bool
SoftDisk::loadIMD(const std::vector<uint8_t>& f, std::string& err)
{
    size_t pos = 0;
    while (pos < f.size() && f[pos] != 0x1a)
    {
        ++pos;
    }
    if (pos == f.size())
    {
        err = "the ImageDisk header has no end";
        return false;
    }
    ++pos;
    imdHeader_m.assign(f.begin(), f.begin() + (std::ptrdiff_t) pos);

    auto need = [&](size_t n) { return pos + n <= f.size(); };
    while (pos < f.size())
    {
        if (!need(5))
        {
            err = "an ImageDisk track header is cut short";
            return false;
        }
        uint8_t mode = f[pos], cyl = f[pos + 1], head = f[pos + 2], nsec = f[pos + 3],
                scode = f[pos + 4];
        pos += 5;
        if (mode > 5)
        {
            err = "an ImageDisk track has an unknown mode";
            return false;
        }
        bool hasCylMap = head & 0x80, hasHeadMap = head & 0x40;
        head &= 0x01;
        size_t mapLen = nsec * (1 + hasCylMap + hasHeadMap) + (scode == 0xff ? nsec * 2 : 0);
        if (!need(mapLen))
        {
            err = "an ImageDisk sector map is cut short";
            return false;
        }
        const uint8_t* smap = &f[pos];
        const uint8_t* cmap = hasCylMap ? smap + nsec : nullptr;
        const uint8_t* hmap = hasHeadMap ? smap + nsec * (1 + hasCylMap) : nullptr;
        const uint8_t* zmap = scode == 0xff ? smap + nsec * (1 + hasCylMap + hasHeadMap) : nullptr;
        pos += mapLen;

        Track t;
        t.present = true;
        t.imdMode = mode;
        t.mfm     = mode >= 3;
        for (unsigned i = 0; i < nsec; ++i)
        {
            Sector s;
            size_t len = zmap ? (size_t) (zmap[i * 2] | (zmap[i * 2 + 1] << 8)) : sizeOf(scode);
            int    n   = sizeCode(len);
            if (scode != 0xff && scode > 6)
            {
                err = "an ImageDisk track has an unknown sector size";
                return false;
            }
            s.cyl  = cmap ? cmap[i] : cyl;
            s.head = hmap ? hmap[i] : head;
            s.sec  = smap[i];
            s.size = (uint8_t) (n < 0 ? 0 : n);
            if (!need(1))
            {
                err = "an ImageDisk sector is cut short";
                return false;
            }
            uint8_t type = f[pos++];
            if (type > 8)
            {
                err = "an ImageDisk sector has an unknown type";
                return false;
            }
            if (type == 0)
            {
                s.noData = true;
            }
            else
            {
                unsigned k = type - 1u;
                s.deleted  = k & 2;
                s.crcError = k & 4;
                if (k & 1)
                {
                    if (!need(1))
                    {
                        err = "an ImageDisk sector is cut short";
                        return false;
                    }
                    s.data.assign(len, f[pos++]);
                }
                else
                {
                    if (!need(len))
                    {
                        err = "an ImageDisk sector is cut short";
                        return false;
                    }
                    s.data.assign(f.begin() + (std::ptrdiff_t) pos,
                                  f.begin() + (std::ptrdiff_t) (pos + len));
                    pos += len;
                }
            }
            t.sectors.push_back(std::move(s));
        }
        size_t idx = (size_t) cyl * 2 + head;
        if (tracks_m.size() <= idx)
        {
            tracks_m.resize(idx + 1);
        }
        tracks_m[idx] = std::move(t);
    }
    return true;
}

bool
SoftDisk::loadTD0(std::string& err)
{
    TD0FloppyDisk td({path_m});
    if (!td.isReady())
    {
        err = path_m + " is not a Teledisk image this app can read";
        return false;
    }
    for (const auto& tr : td.decodedTracks())
    {
        Track t;
        t.present = true;
        t.mfm     = tr->density() == ::Track::doubleDensity;
        int head  = tr->getSideNumber() & 1;
        int cyl   = tr->getTrackNumber();
        for (const auto& sp : tr->sectors())
        {
            Sector s;
            s.cyl      = sp->getTrackNum();
            s.head     = sp->getHeadNum();
            s.sec      = sp->getSectorNum();
            int n      = sizeCode(sp->getSectorLength());
            s.size     = (uint8_t) (n < 0 ? 0 : n);
            s.crcError = sp->getReadError();
            s.deleted  = sp->getDeletedDataAddressMark();
            s.data.resize(sp->getSectorLength());
            for (WORD i = 0; i < sp->getSectorLength(); ++i)
            {
                BYTE b = 0;
                sp->readData(i, b);
                s.data[i] = b;
            }
            t.sectors.push_back(std::move(s));
        }
        size_t idx = (size_t) cyl * 2 + head;
        if (tracks_m.size() <= idx)
        {
            tracks_m.resize(idx + 1);
        }
        tracks_m[idx] = std::move(t);
    }
    return true;
}

bool
SoftDisk::loadSector(const std::vector<uint8_t>& f, std::string& err)
{
    SectorGeometry g;
    if (!readGeometryTrailer(f, g))
    {
        err = "the sector image's geometry is missing";
        return false;
    }
    mediaInches_m   = g.media;
    numSides_m      = g.sides;
    numTracks_m     = g.tracks;
    numSectors_m    = g.spt;
    secSize_m       = g.secSize;
    doubleDensity_m = g.dd;
    interlaced_m    = g.interlaced;
    trailer_m.assign(f.end() - 128, f.end());

    tracks_m.assign((size_t) g.tracks * 2, Track());
    for (int side = 0; side < g.sides; ++side)
    {
        for (int cyl = 0; cyl < g.tracks; ++cyl)
        {
            Track& t = tracks_m[(size_t) cyl * 2 + side];
            t.present = true;
            t.mfm     = g.dd;
            for (int sec = 1; sec <= g.spt; ++sec)
            {
                size_t slot = interlaced_m ? ((size_t) (cyl * g.sides + side) * g.spt + sec - 1)
                                           : ((size_t) (side * g.tracks + cyl) * g.spt + sec - 1);
                size_t off  = slot * g.secSize;
                Sector s;
                s.cyl  = (uint8_t) cyl;
                s.head = (uint8_t) side;
                s.sec  = (uint8_t) sec;
                s.size = (uint8_t) sizeCode((size_t) g.secSize);
                s.data.assign(f.begin() + (std::ptrdiff_t) off,
                              f.begin() + (std::ptrdiff_t) (off + g.secSize));
                t.sectors.push_back(std::move(s));
            }
        }
    }
    return true;
}

bool
SoftDisk::loadRaw(const std::vector<uint8_t>& f, int driveSdBytes, std::string& err)
{
    long trk = rawTrackLength(f);
    if (trk <= 0)
    {
        err = "no track layout found in the raw image";
        return false;
    }
    rawTrackLen_m = trk;
    long count    = (long) (f.size() / (size_t) trk);

    // the second track's first ID says how the tracks are laid out
    auto firstId = [&](long t, int& c, int& h) {
        c = h = -1;
        size_t base = (size_t) t * (size_t) trk;
        for (size_t i = base; i < base + (size_t) trk && i + 4 < f.size(); ++i)
        {
            if (f[i] == GenericFloppyFormat::ID_AM_BYTE)
            {
                c = f[i + 1];
                h = f[i + 2];
                return;
            }
        }
    };
    int c, h;
    firstId(1, c, h);
    interlaced_m = c == 0 && h == 1;
    numSides_m   = interlaced_m ? 2 : 1;

    if (count == 77 || count == 154)
    {
        mediaInches_m = 8;
        numTracks_m   = 77;
        numSides_m    = count > 77 ? 2 : 1;
    }
    else if (count == 40 || count == 80 || count == 160)
    {
        mediaInches_m = 5;
        if (count == 160)
        {
            numSides_m  = 2;
            numTracks_m = 80;
        }
        else if (count == 40)
        {
            numSides_m  = 1;
            numTracks_m = 40;
        }
        else if (interlaced_m)
        {
            numTracks_m = 40;
        }
        else
        {
            firstId(40, c, h);
            numSides_m  = (c == 0 && h == 1) ? 2 : 1;
            numTracks_m = numSides_m == 2 ? 40 : 80;
        }
    }
    else
    {
        err = "the raw image's size is not a whole disk";
        return false;
    }
    int sd = mediaInches_m == 8 ? 6400 : driveSdBytes;
    if (trk == sd)
    {
        doubleDensity_m = false;
    }
    else if (trk == sd * 2)
    {
        doubleDensity_m = true;
    }
    else
    {
        err = "the raw image's track length is not a drive's";
        return false;
    }

    tracks_m.assign((size_t) numTracks_m * 2, Track());
    for (int side = 0; side < numSides_m; ++side)
    {
        for (int cyl = 0; cyl < numTracks_m; ++cyl)
        {
            Track& t = tracks_m[(size_t) cyl * 2 + side];
            size_t at = interlaced_m ? (size_t) (cyl * numSides_m + side) * trk
                                     : (size_t) (side * numTracks_m + cyl) * trk;
            t.raw.assign(f.begin() + (std::ptrdiff_t) at, f.begin() + (std::ptrdiff_t) (at + trk));
            t.present = true;
            t.mfm     = doubleDensity_m;
            parseRawTrack(t, cyl, side);
        }
    }
    return true;
}

bool
SoftDisk::parseRawTrack(Track& t, int cyl, int head)
{
    (void) cyl;
    (void) head;
    t.sectors.clear();
    const std::vector<uint8_t>& r = t.raw;
    for (size_t i = 0; i + 6 < r.size(); ++i)
    {
        if (r[i] != GenericFloppyFormat::ID_AM_BYTE)
        {
            continue;
        }
        Sector s;
        s.cyl  = r[i + 1];
        s.head = r[i + 2];
        s.sec  = r[i + 3];
        s.size = r[i + 4] & 3;
        s.rawIdOffset = (long) i;
        size_t len    = sizeOf(s.size);
        size_t j      = i + 7;
        while (j < r.size() && r[j] != GenericFloppyFormat::DATA_AM_BYTE &&
               r[j] != GenericFloppyFormat::ID_AM_BYTE)
        {
            ++j;
        }
        if (j < r.size() && r[j] == GenericFloppyFormat::DATA_AM_BYTE && j + 1 + len <= r.size())
        {
            s.rawOffset = (long) (j + 1);
            s.data.assign(r.begin() + (std::ptrdiff_t) (j + 1),
                          r.begin() + (std::ptrdiff_t) (j + 1 + len));
            i = j + len + 2;
        }
        else
        {
            s.noData = true;
            i += 6;
        }
        t.sectors.push_back(std::move(s));
    }
    return !t.sectors.empty();
}

// ---- where the head is ---------------------------------------------------

const SoftDisk::Track*
SoftDisk::track(int cyl, int head) const
{
    size_t idx = (size_t) cyl * 2 + (head & 1);
    return idx < tracks_m.size() && tracks_m[idx].present ? &tracks_m[idx] : nullptr;
}

SoftDisk::Track*
SoftDisk::trackAt(int cyl, int head)
{
    size_t idx = (size_t) cyl * 2 + (head & 1);
    return idx < tracks_m.size() && tracks_m[idx].present ? &tracks_m[idx] : nullptr;
}

int
SoftDisk::mediaCylinder(BYTE track)
{
    return getRealTrackNumber(track);
}

SoftDisk::Track*
SoftDisk::physTrack(BYTE track, BYTE side)
{
    return trackAt(mediaCylinder(track), side);
}

SoftDisk::Sector*
SoftDisk::locate(Track* t, BYTE cyl, BYTE sector)
{
    if (!t)
    {
        return nullptr;
    }
    Sector* any = nullptr;
    for (Sector& s : t->sectors)
    {
        if (s.sec == sector)
        {
            if (s.cyl == cyl)
            {
                return &s;
            }
            if (!any)
            {
                any = &s;
            }
        }
    }
    return any;
}

bool
SoftDisk::densityMatches(bool doubleDensity, BYTE track, BYTE side)
{
    Track* t = physTrack(track, side);
    return t && t->mfm == doubleDensity;
}

bool
SoftDisk::trackFormatted(BYTE track, BYTE side)
{
    Track* t = physTrack(track, side);
    return t && !t->sectors.empty();
}

bool
SoftDisk::findSector(BYTE side, BYTE track, BYTE sector)
{
    Sector* s = locate(physTrack(track, side), (BYTE) mediaCylinder(track), sector);
    return s && !s->noData;
}

// ---- the controller's reads and writes -----------------------------------

bool
SoftDisk::readData(BYTE track, BYTE side, BYTE sector, int inSector, int& data)
{
    if (inSector < 0)
    {
        Track* t = physTrack(track, side);
        xferPos_m = 0;
        xferBad_m = false;
        if (sector == 0xfd)
        {
            // Read Address: the next ID field round the track
            if (!t || t->sectors.empty())
            {
                data = GenericFloppyFormat::ERROR;
                return true;
            }
            const Sector& s = t->sectors[nextId_m++ % t->sectors.size()];
            uint8_t       id[8] = {0xa1, 0xa1, 0xa1, 0xfe, s.cyl, s.head, s.sec, s.size};
            uint16_t      crc   = t->mfm ? crc16(id, 8) : crc16(id + 3, 5);
            memcpy(idField_m, id + 4, 4);
            idField_m[4] = (uint8_t) (crc >> 8);
            idField_m[5] = (uint8_t) crc;
            xfer_m       = idField_m;
            xferLen_m    = 6;
            data         = GenericFloppyFormat::ID_AM;
            return true;
        }
        if (sector == 0xff)
        {
            // Read Track: the track as the controller would see it
            readTrack_m.clear();
            bool mfm = t ? t->mfm : false;
            auto put = [&](uint8_t b, size_t n) { readTrack_m.insert(readTrack_m.end(), n, b); };
            uint8_t gap = mfm ? 0x4e : 0xff;
            put(gap, mfm ? 80 : 40);
            put(0x00, mfm ? 12 : 6);
            if (mfm)
            {
                put(0xc2, 3);
            }
            put(0xfc, 1);
            put(gap, mfm ? 50 : 26);
            for (const Sector& s : t ? t->sectors : std::vector<Sector>())
            {
                put(0x00, mfm ? 12 : 6);
                size_t   at    = readTrack_m.size();
                if (mfm)
                {
                    put(0xa1, 3);
                }
                uint8_t  id[5] = {0xfe, s.cyl, s.head, s.sec, s.size};
                readTrack_m.insert(readTrack_m.end(), id, id + 5);
                uint16_t crc = crc16(&readTrack_m[at], readTrack_m.size() - at);
                put((uint8_t) (crc >> 8), 1);
                put((uint8_t) crc, 1);
                put(gap, mfm ? 22 : 11);
                if (s.noData)
                {
                    continue;
                }
                put(0x00, mfm ? 12 : 6);
                at = readTrack_m.size();
                if (mfm)
                {
                    put(0xa1, 3);
                }
                put(s.deleted ? 0xf8 : 0xfb, 1);
                readTrack_m.insert(readTrack_m.end(), s.data.begin(), s.data.end());
                crc = crc16(&readTrack_m[at], readTrack_m.size() - at);
                put((uint8_t) (crc >> 8), 1);
                put((uint8_t) crc, 1);
                put(gap, mfm ? 54 : 27);
            }
            readTrack_m.resize(trackBytes(mediaInches_m, mfm), gap);
            xfer_m    = readTrack_m.data();
            xferLen_m = readTrack_m.size();
            data      = GenericFloppyFormat::INDEX_AM;
            return true;
        }
        Sector* s = locate(t, (BYTE) mediaCylinder(track), sector);
        if (!s || s->noData)
        {
            data = GenericFloppyFormat::NO_DATA;
            return true;
        }
        xfer_m    = s->data.data();
        xferLen_m = s->data.size();
        xferBad_m = s->crcError;
        data      = GenericFloppyFormat::DATA_AM;
        return true;
    }

    if (xfer_m && xferPos_m < xferLen_m)
    {
        data = xfer_m[xferPos_m++];
    }
    else
    {
        data = xferBad_m ? GenericFloppyFormat::ERROR : GenericFloppyFormat::CRC;
    }
    return true;
}

bool
SoftDisk::writeData(BYTE track, BYTE side, BYTE sector, int inSector, BYTE data, bool dataReady,
                    int& result)
{
    if (writeProtected())
    {
        result = GenericFloppyFormat::ERROR;
        return false;
    }
    if (inSector < 0)
    {
        writing_m = nullptr;
        if (sector == 0xff || sector == 0xfe)
        {
            // Write Track goes through formatTrack
            result = GenericFloppyFormat::ERROR;
            return true;
        }
        Track*  t = physTrack(track, side);
        Sector* s = locate(t, (BYTE) mediaCylinder(track), sector);
        if (!s || s->noData)
        {
            result = GenericFloppyFormat::NO_DATA;
            return true;
        }
        writing_m      = s;
        writingTrack_m = t;
        xferPos_m      = 0;
        result         = GenericFloppyFormat::DATA_AM;
        return true;
    }

    if (!writing_m)
    {
        result = GenericFloppyFormat::ERROR;
        return true;
    }
    if (xferPos_m < writing_m->data.size())
    {
        if (!dataReady)
        {
            result = GenericFloppyFormat::NO_DATA;
            return true;
        }
        writing_m->data[xferPos_m] = data;
        if (writing_m->rawOffset >= 0)
        {
            writingTrack_m->raw[(size_t) writing_m->rawOffset + xferPos_m] = data;
        }
        ++xferPos_m;
        touched();
        result = data;
    }
    else
    {
        // a sector written whole has a good CRC and a normal data mark
        writing_m->crcError = false;
        writing_m->deleted  = false;
        writing_m           = nullptr;
        touched();
        result = GenericFloppyFormat::CRC;
    }
    return true;
}

bool
SoftDisk::formatTrack(BYTE track, BYTE side, bool doubleDensity, const std::vector<BYTE>& stream)
{
    if (writeProtected() || format_m == FormatTD0)
    {
        return false;
    }
    int    cyl = mediaCylinder(track);
    size_t idx = (size_t) cyl * 2 + (side & 1);
    if (tracks_m.size() <= idx)
    {
        tracks_m.resize(idx + 1);
    }
    Track& old = tracks_m[idx];

    Track nt;
    nt.present = true;
    nt.mfm     = doubleDensity;
    nt.imdMode = old.present && old.mfm == doubleDensity ? old.imdMode : -1;
    for (FormattedSector& f : parseTrackStream(stream, doubleDensity))
    {
        Sector s;
        s.cyl     = f.cyl;
        s.head    = f.head;
        s.sec     = f.sec;
        s.size    = f.size & 3;
        s.deleted = f.deleted;
        s.noData  = !f.hasData;
        s.data    = std::move(f.data);
        nt.sectors.push_back(std::move(s));
    }

    if (format_m == FormatSector || format_m == FormatRaw)
    {
        // the file's fixed geometry, or nothing
        if (doubleDensity != doubleDensity_m || (int) nt.sectors.size() != numSectors_m ||
            (format_m == FormatRaw && nt.sectors.size() != old.sectors.size()))
        {
            return false;
        }
        std::vector<bool> seen((size_t) numSectors_m + 1, false);
        for (const Sector& s : nt.sectors)
        {
            if (s.noData || sizeOf(s.size) != (size_t) secSize_m || s.sec < 1 ||
                s.sec > numSectors_m || seen[s.sec])
            {
                return false;
            }
            seen[s.sec] = true;
        }
        if (format_m == FormatRaw)
        {
            nt.raw = old.raw;
            for (size_t i = 0; i < nt.sectors.size(); ++i)
            {
                Sector&       s    = nt.sectors[i];
                const Sector& slot = old.sectors[i];
                if (slot.rawOffset < 0 || slot.rawIdOffset < 0)
                {
                    return false;
                }
                s.rawIdOffset = slot.rawIdOffset;
                s.rawOffset   = slot.rawOffset;
                uint8_t* id   = &nt.raw[(size_t) slot.rawIdOffset];
                id[1]         = s.cyl;
                id[2]         = s.head;
                id[3]         = s.sec;
                id[4]         = s.size;
                std::copy(s.data.begin(), s.data.end(), nt.raw.begin() + slot.rawOffset);
            }
        }
    }

    writing_m = nullptr;
    old       = std::move(nt);
    if (cyl >= numTracks_m)
    {
        numTracks_m = cyl + 1;
    }
    if (side & 1)
    {
        numSides_m = 2;
    }
    touched();
    return true;
}

// ---- writing back ----------------------------------------------------------

const char*
SoftDisk::formatName() const
{
    switch (format_m)
    {
        case FormatIMD:
            return "IMD";
        case FormatTD0:
            return "TD0";
        case FormatSector:
            return "SECTOR";
        case FormatRaw:
            return "RAW";
    }
    return "?";
}

bool
SoftDisk::serialise(std::vector<uint8_t>& out, std::string& err)
{
    switch (format_m)
    {
        case FormatIMD:
            return saveIMD(out, err);
        case FormatSector:
            return saveSector(out, err);
        case FormatRaw:
            return saveRaw(out, err);
        case FormatTD0:
            break;
    }
    err = "Teledisk images are read-only";
    return false;
}

bool
SoftDisk::saveIMD(std::vector<uint8_t>& out, std::string& err)
{
    (void) err;
    out.clear();
    if (imdHeader_m.empty())
    {
        std::string h = "IMD 1.18: FujiNet Go H89\r\n";
        out.assign(h.begin(), h.end());
        out.push_back(0x1a);
    }
    else
    {
        out = imdHeader_m;
    }

    for (size_t idx = 0; idx < tracks_m.size(); ++idx)
    {
        const Track& t = tracks_m[idx];
        if (!t.present)
        {
            continue;
        }
        uint8_t cyl = (uint8_t) (idx / 2), head = (uint8_t) (idx % 2);
        int     mode = t.imdMode;
        if (mode < 0 || (mode >= 3) != t.mfm)
        {
            mode = defaultImdMode(mediaInches_m, t.mfm);
        }

        bool   cylMap = false, headMap = false, mixed = false;
        size_t len0   = 0;
        for (size_t i = 0; i < t.sectors.size(); ++i)
        {
            const Sector& s = t.sectors[i];
            cylMap  = cylMap || s.cyl != cyl;
            headMap = headMap || s.head != head;
            size_t len = s.noData ? sizeOf(s.size) : s.data.size();
            if (i == 0)
            {
                len0 = len;
            }
            else if (len != len0)
            {
                mixed = true;
            }
        }
        int code = t.sectors.empty() ? 0 : sizeCode(len0);
        if (code < 0)
        {
            mixed = true;
        }

        out.push_back((uint8_t) mode);
        out.push_back(cyl);
        out.push_back((uint8_t) (head | (cylMap ? 0x80 : 0) | (headMap ? 0x40 : 0)));
        out.push_back((uint8_t) t.sectors.size());
        out.push_back(mixed ? 0xff : (uint8_t) code);
        for (const Sector& s : t.sectors)
        {
            out.push_back(s.sec);
        }
        if (cylMap)
        {
            for (const Sector& s : t.sectors)
            {
                out.push_back(s.cyl);
            }
        }
        if (headMap)
        {
            for (const Sector& s : t.sectors)
            {
                out.push_back(s.head);
            }
        }
        if (mixed)
        {
            for (const Sector& s : t.sectors)
            {
                size_t len = s.noData ? sizeOf(s.size) : s.data.size();
                out.push_back((uint8_t) len);
                out.push_back((uint8_t) (len >> 8));
            }
        }
        for (const Sector& s : t.sectors)
        {
            if (s.noData)
            {
                out.push_back(0);
                continue;
            }
            bool same = std::all_of(s.data.begin(), s.data.end(),
                                    [&](uint8_t b) { return b == s.data[0]; });
            uint8_t type = (uint8_t) (1 + (same ? 1 : 0) + (s.deleted ? 2 : 0) + (s.crcError ? 4 : 0));
            out.push_back(type);
            if (same)
            {
                out.push_back(s.data.empty() ? 0 : s.data[0]);
            }
            else
            {
                out.insert(out.end(), s.data.begin(), s.data.end());
            }
        }
    }
    return true;
}

bool
SoftDisk::saveSector(std::vector<uint8_t>& out, std::string& err)
{
    (void) err;
    size_t secSize = (size_t) secSize_m;
    out.assign((size_t) numSides_m * numTracks_m * numSectors_m * secSize, 0xe5);
    for (int side = 0; side < numSides_m; ++side)
    {
        for (int cyl = 0; cyl < numTracks_m; ++cyl)
        {
            const Track* t = track(cyl, side);
            if (!t)
            {
                continue;
            }
            for (const Sector& s : t->sectors)
            {
                if (s.noData || s.sec < 1 || s.sec > numSectors_m || s.data.size() != secSize)
                {
                    continue;
                }
                size_t slot = interlaced_m
                                  ? ((size_t) (cyl * numSides_m + side) * numSectors_m + s.sec - 1)
                                  : ((size_t) (side * numTracks_m + cyl) * numSectors_m + s.sec - 1);
                std::copy(s.data.begin(), s.data.end(), out.begin() + (std::ptrdiff_t) (slot * secSize));
            }
        }
    }
    out.insert(out.end(), trailer_m.begin(), trailer_m.end());
    return true;
}

bool
SoftDisk::saveRaw(std::vector<uint8_t>& out, std::string& err)
{
    (void) err;
    size_t trk = (size_t) rawTrackLen_m;
    out.assign((size_t) numSides_m * numTracks_m * trk, 0x00);
    for (int side = 0; side < numSides_m; ++side)
    {
        for (int cyl = 0; cyl < numTracks_m; ++cyl)
        {
            const Track* t = track(cyl, side);
            if (!t || t->raw.size() != trk)
            {
                continue;
            }
            size_t at = interlaced_m ? (size_t) (cyl * numSides_m + side) * trk
                                     : (size_t) (side * numTracks_m + cyl) * trk;
            std::copy(t->raw.begin(), t->raw.end(), out.begin() + (std::ptrdiff_t) at);
        }
    }
    return true;
}
