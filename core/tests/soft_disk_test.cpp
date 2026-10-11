/*
 * soft_disk -- the soft-sectored media (core/machine/SoftDisk) and the
 * WD1797 that reads and writes it:
 *   - the Write Track parser on FM and MFM streams;
 *   - each image format read, written through the controller's interface,
 *     written back, and read again: IMD (maps, compressed, deleted, bad and
 *     data-less sectors; an untouched image writes back byte for byte),
 *     VirtualH89's sector and raw images, Teledisk (read-only);
 *   - the WD1797 itself on a blank ImageDisk: Write Track in MFM and FM,
 *     Read Sector, Write Sector, Read Address, the density check, and a
 *     write-protected disk refused at once.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "GenericFloppyDrive.h"
#include "h89drives.h"
#include "GenericFloppyFormat.h"
#include "MachineContext.h"
#include "SoftDisk.h"
#include "TrackFormatter.h"
#include "WD179xUserIf.h"
#include "WallClock.h"
#include "test_tmpdir.h"
#include "test_util.h"
#include "wd1797.h"

#include <cstring>
#include <string>
#include <vector>

namespace
{

std::string g_dir;

std::string
tmp(const char* name)
{
    return g_dir + "/" + name;
}

void
put(std::vector<uint8_t>& v, uint8_t b, size_t n = 1)
{
    v.insert(v.end(), n, b);
}

/// The bytes a CP/M FORMAT gives Write Track for one track.
std::vector<uint8_t>
formatStream(bool mfm, int cyl, int head, int nsec, int sizeCode, uint8_t fill)
{
    std::vector<uint8_t> s;
    uint8_t              gap = mfm ? 0x4e : 0xff;
    put(s, gap, mfm ? 80 : 40);
    put(s, 0x00, mfm ? 12 : 6);
    if (mfm)
    {
        put(s, 0xf6, 3);
    }
    put(s, 0xfc);
    put(s, gap, mfm ? 50 : 26);
    for (int i = 1; i <= nsec; ++i)
    {
        put(s, 0x00, mfm ? 12 : 6);
        if (mfm)
        {
            put(s, 0xf5, 3);
        }
        put(s, 0xfe);
        put(s, (uint8_t) cyl);
        put(s, (uint8_t) head);
        put(s, (uint8_t) i);
        put(s, (uint8_t) sizeCode);
        put(s, 0xf7);
        put(s, gap, mfm ? 22 : 11);
        put(s, 0x00, mfm ? 12 : 6);
        if (mfm)
        {
            put(s, 0xf5, 3);
        }
        put(s, 0xfb);
        put(s, fill, (size_t) 128 << sizeCode);
        put(s, 0xf7);
        put(s, gap, mfm ? 40 : 27);
    }
    return s;
}

/// Reads one sector through the GenericFloppyDisk interface.
bool
readSector(SoftDisk& d, int track, int side, int sec, std::vector<uint8_t>& out)
{
    int data;
    out.clear();
    if (!d.findSector((BYTE) side, (BYTE) track, (BYTE) sec))
    {
        return false;
    }
    d.readData((BYTE) track, (BYTE) side, (BYTE) sec, -1, data);
    if (data != GenericFloppyFormat::DATA_AM)
    {
        return false;
    }
    for (int i = 0;; ++i)
    {
        d.readData((BYTE) track, (BYTE) side, (BYTE) sec, i, data);
        if (data < 0)
        {
            return data == GenericFloppyFormat::CRC;
        }
        out.push_back((uint8_t) data);
    }
}

bool
writeSector(SoftDisk& d, int track, int side, int sec, const std::vector<uint8_t>& in)
{
    int result;
    d.writeData((BYTE) track, (BYTE) side, (BYTE) sec, -1, 0, false, result);
    if (result != GenericFloppyFormat::DATA_AM)
    {
        return false;
    }
    for (size_t i = 0;; ++i)
    {
        d.writeData((BYTE) track, (BYTE) side, (BYTE) sec, (int) i, i < in.size() ? in[i] : 0, true,
                    result);
        if (result < 0)
        {
            return result == GenericFloppyFormat::CRC && i == in.size();
        }
    }
}

std::vector<uint8_t>
pattern(size_t n, unsigned seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i)
    {
        v[i] = (uint8_t) (i * 7 + seed);
    }
    return v;
}

void
writeFile(const std::string& path, const std::vector<uint8_t>& data)
{
    std::string err;
    CHECK(diskio::writeFile(path, data, err));
}

// ---- the parser ------------------------------------------------------------

void
testParser()
{
    std::vector<FormattedSector> mfm = parseTrackStream(formatStream(true, 3, 1, 9, 2, 0xe5), true);
    CHECK_EQ_INT((int) mfm.size(), 9);
    if (mfm.size() == 9)
    {
        CHECK_EQ_INT(mfm[0].cyl, 3);
        CHECK_EQ_INT(mfm[0].head, 1);
        CHECK_EQ_INT(mfm[8].sec, 9);
        CHECK_EQ_INT(mfm[4].size, 2);
        CHECK(mfm[4].hasData && mfm[4].data.size() == 512 && mfm[4].data[511] == 0xe5);
    }
    std::vector<FormattedSector> fm = parseTrackStream(formatStream(false, 0, 0, 16, 0, 0xe5), false);
    CHECK_EQ_INT((int) fm.size(), 16);
    // an MFM stream read as FM has no marks after 00 sync bytes
    CHECK_EQ_INT((int) parseTrackStream(formatStream(true, 0, 0, 9, 2, 0xe5), false).size(), 0);

    // a deleted data mark, and an ID with no data field after it
    std::vector<uint8_t> s;
    put(s, 0x00, 6);
    put(s, 0xfe);
    put(s, 0);
    put(s, 0);
    put(s, 1);
    put(s, 0);
    put(s, 0xf7);
    put(s, 0xff, 11);
    put(s, 0x00, 6);
    put(s, 0xf8);
    put(s, 0x11, 128);
    put(s, 0xf7);
    put(s, 0xff, 20);
    put(s, 0x00, 6);
    put(s, 0xfe);
    put(s, 0);
    put(s, 0);
    put(s, 2);
    put(s, 0);
    put(s, 0xf7);
    put(s, 0xff, 100);
    std::vector<FormattedSector> odd = parseTrackStream(s, false);
    CHECK_EQ_INT((int) odd.size(), 2);
    if (odd.size() == 2)
    {
        CHECK(odd[0].deleted && odd[0].hasData);
        CHECK(!odd[1].hasData);
    }
}

// ---- ImageDisk -------------------------------------------------------------

std::vector<uint8_t>
sampleImd()
{
    std::string          h = "IMD 1.18: 11/10/2026 12:00:00\r\nsample\r\n";
    std::vector<uint8_t> f(h.begin(), h.end());
    put(f, 0x1a);
    // track 0 head 0: FM 250k, 4 x 128, sectors 1..4; a compressed one, a
    // deleted one, a bad one
    put(f, 2);
    put(f, 0);
    put(f, 0);
    put(f, 4);
    put(f, 0);
    for (int i = 1; i <= 4; ++i)
    {
        put(f, (uint8_t) i);
    }
    put(f, 1);
    std::vector<uint8_t> p = pattern(128, 1);
    f.insert(f.end(), p.begin(), p.end());
    put(f, 2);
    put(f, 0x5a);
    put(f, 3);
    p = pattern(128, 3);
    f.insert(f.end(), p.begin(), p.end());
    put(f, 5);
    p = pattern(128, 4);
    f.insert(f.end(), p.begin(), p.end());
    // track 1 head 1: MFM 250k, 2 x 256 with a cylinder map (IDs say
    // cylinder 9) and a sector with no data
    put(f, 5);
    put(f, 1);
    put(f, 1 | 0x80);
    put(f, 2);
    put(f, 1);
    put(f, 1);
    put(f, 2);
    put(f, 9);
    put(f, 9);
    put(f, 1);
    p = pattern(256, 5);
    f.insert(f.end(), p.begin(), p.end());
    put(f, 0);
    return f;
}

void
testImd()
{
    std::string path = tmp("sample.imd");
    writeFile(path, sampleImd());
    std::string err;

    SoftDisk::Format fmt;
    CHECK(SoftDisk::probe(path, fmt) && fmt == SoftDisk::FormatIMD);
    auto d = SoftDisk::load(path, 5, 40, false, err);
    CHECK(d != nullptr);
    if (!d)
    {
        printf("  %s\n", err.c_str());
        return;
    }
    CHECK_EQ_INT(d->cylinders(), 40);
    CHECK_EQ_INT(d->sides(), 2);
    CHECK(strcmp(d->formatName(), "IMD") == 0);

    // untouched, it writes back exactly as read
    CHECK(!d->dirty());
    std::vector<uint8_t> before = sampleImd();
    CHECK(d->flush(err));
    std::vector<uint8_t> after;
    CHECK(diskio::readFile(path, after, err));
    CHECK(after == before);

    std::vector<uint8_t> got;
    CHECK(d->densityMatches(false, 0, 0) && !d->densityMatches(true, 0, 0));
    CHECK(readSector(*d, 0, 0, 1, got) && got == pattern(128, 1));
    CHECK(readSector(*d, 0, 0, 2, got) && got == std::vector<uint8_t>(128, 0x5a));
    CHECK(!readSector(*d, 0, 0, 4, got));   // ends in a CRC error
    CHECK(d->densityMatches(true, 1, 1));
    CHECK(readSector(*d, 1, 1, 1, got) && got == pattern(256, 5));
    CHECK(!d->findSector(1, 1, 2));          // an ID without data
    CHECK(!d->findSector(0, 0, 9));
    CHECK(!d->trackFormatted(2, 0));

    // write two sectors, write back, read again
    CHECK(writeSector(*d, 0, 0, 2, pattern(128, 9)));
    CHECK(writeSector(*d, 0, 0, 4, pattern(128, 10)));
    CHECK(d->dirty());
    CHECK(d->flush(err));
    CHECK(!d->dirty());
    auto e = SoftDisk::load(path, 5, 40, false, err);
    CHECK(e != nullptr);
    if (e)
    {
        CHECK(readSector(*e, 0, 0, 2, got) && got == pattern(128, 9));
        CHECK(readSector(*e, 0, 0, 4, got) && got == pattern(128, 10));   // good CRC now
        CHECK(readSector(*e, 0, 0, 3, got) && got == pattern(128, 3));
        CHECK(readSector(*e, 1, 1, 1, got) && got == pattern(256, 5));
        CHECK(!e->findSector(1, 1, 2));
        const SoftDisk::Track* t = e->track(1, 1);
        CHECK(t && t->sectors.size() == 2 && t->sectors[0].cyl == 9);
    }

    // write protect: no writes
    d->setWriteProtected(true);
    CHECK(d->checkWriteProtect());
    CHECK(!writeSector(*d, 0, 0, 1, pattern(128, 2)));
}

// ---- VirtualH89's sector image -------------------------------------------

void
testSectorImage()
{
    // 5" DS 40 tracks, 9 x 512 MFM, side-major
    std::vector<uint8_t> f;
    for (int side = 0; side < 2; ++side)
    {
        for (int cyl = 0; cyl < 40; ++cyl)
        {
            for (int sec = 1; sec <= 9; ++sec)
            {
                std::vector<uint8_t> p = pattern(512, (unsigned) (side * 100 + cyl * 10 + sec));
                f.insert(f.end(), p.begin(), p.end());
            }
        }
    }
    std::string trailer = "5m512z9p2s40t1d0i\n";
    trailer.resize(128, '\0');
    f.insert(f.end(), trailer.begin(), trailer.end());
    std::string path = tmp("sample.mmsdisk");
    writeFile(path, f);

    std::string      err;
    SoftDisk::Format fmt;
    CHECK(SoftDisk::probe(path, fmt) && fmt == SoftDisk::FormatSector);
    auto d = SoftDisk::load(path, 5, 40, false, err);
    CHECK(d != nullptr);
    if (!d)
    {
        printf("  %s\n", err.c_str());
        return;
    }
    std::vector<uint8_t> got;
    CHECK(readSector(*d, 7, 1, 3, got) && got == pattern(512, 100 + 70 + 3));
    CHECK(writeSector(*d, 7, 1, 3, pattern(512, 77)));
    CHECK(d->flush(err));
    std::vector<uint8_t> after;
    CHECK(diskio::readFile(path, after, err));
    CHECK_EQ_INT((int) after.size(), (int) f.size());
    size_t at = ((size_t) (1 * 40 + 7) * 9 + 2) * 512;
    CHECK(std::equal(after.begin() + (std::ptrdiff_t) at, after.begin() + (std::ptrdiff_t) (at + 512),
                     pattern(512, 77).begin()));
    CHECK(std::equal(after.end() - 128, after.end(), trailer.begin()));

    // a format of the same geometry is taken; another is a write fault
    std::vector<uint8_t> s = formatStream(true, 2, 0, 9, 2, 0xe5);
    CHECK(d->formatTrack(2, 0, true, s));
    CHECK(readSector(*d, 2, 0, 9, got) && got == std::vector<uint8_t>(512, 0xe5));
    CHECK(!d->formatTrack(3, 0, true, formatStream(true, 3, 0, 5, 3, 0xe5)));
    CHECK(!d->formatTrack(3, 0, false, formatStream(false, 3, 0, 9, 2, 0xe5)));
}

// ---- VirtualH89's raw image -------------------------------------------------

std::vector<uint8_t>
rawTrack(int cyl, int side, int nsec, int secLen, long trackLen)
{
    std::vector<uint8_t> t;
    put(t, 0x00, 10);
    put(t, 0xfc);
    put(t, 0x00, 20);
    for (int sec = 1; sec <= nsec; ++sec)
    {
        put(t, 0xfe);
        put(t, (uint8_t) cyl);
        put(t, (uint8_t) side);
        put(t, (uint8_t) sec);
        put(t, (uint8_t) (secLen == 256 ? 1 : 0));
        put(t, 0x00, 2);
        put(t, 0x00, 11);
        put(t, 0xfb);
        std::vector<uint8_t> p = pattern((size_t) secLen, (unsigned) (cyl * 16 + sec + side * 64));
        t.insert(t.end(), p.begin(), p.end());
        put(t, 0x00, 2);
        put(t, 0x00, 20);
    }
    t.resize((size_t) trackLen, 0x00);
    return t;
}

void
testRawImage()
{
    // 5" SS 40 tracks FM (3200-byte tracks), 10 x 256
    std::vector<uint8_t> f;
    for (int cyl = 0; cyl < 40; ++cyl)
    {
        std::vector<uint8_t> t = rawTrack(cyl, 0, 10, 256, 3200);
        f.insert(f.end(), t.begin(), t.end());
    }
    std::string path = tmp("sample.rawfloppy");
    writeFile(path, f);

    std::string      err;
    SoftDisk::Format fmt;
    CHECK(SoftDisk::probe(path, fmt) && fmt == SoftDisk::FormatRaw);
    auto d = SoftDisk::load(path, 5, 40, false, err);
    CHECK(d != nullptr);
    if (!d)
    {
        printf("  %s\n", err.c_str());
        return;
    }
    CHECK_EQ_INT(d->cylinders(), 40);
    CHECK(d->densityMatches(false, 5, 0));
    std::vector<uint8_t> got;
    CHECK(readSector(*d, 5, 0, 10, got) && got == pattern(256, 5 * 16 + 10));
    CHECK(writeSector(*d, 5, 0, 10, pattern(256, 200)));
    CHECK(d->flush(err));
    std::vector<uint8_t> after, want = f;
    CHECK(diskio::readFile(path, after, err));
    // the raw track changed only in that sector's data
    std::vector<uint8_t> t5   = rawTrack(5, 0, 10, 256, 3200);
    size_t               data = 10 + 1 + 20 + 9 * (1 + 4 + 2 + 11 + 1 + 256 + 2 + 20) + 1 + 4 + 2 + 11 + 1;
    std::vector<uint8_t> p    = pattern(256, 200);
    std::copy(p.begin(), p.end(), want.begin() + (std::ptrdiff_t) (5 * 3200 + data));
    CHECK(after == want);
    auto e = SoftDisk::load(path, 5, 40, false, err);
    CHECK(e && readSector(*e, 5, 0, 10, got) && got == pattern(256, 200));
}

// ---- Teledisk ----------------------------------------------------------------

void
testTd0()
{
    std::vector<uint8_t> f = {'T', 'D', 0, 0, 0x15, 0x00, 0x01, 0x00, 0x00, 0x01, 0, 0};
    // one MFM track: two 256-byte sectors, raw-encoded
    put(f, 2);
    put(f, 0);
    put(f, 0);
    put(f, 0);
    for (int sec = 1; sec <= 2; ++sec)
    {
        put(f, 0);
        put(f, 0);
        put(f, (uint8_t) sec);
        put(f, 1);
        put(f, 0);
        put(f, 0);
        put(f, 1);   // block: 257 bytes, LE
        put(f, 1);
        put(f, 0);   // raw
        std::vector<uint8_t> p = pattern(256, (unsigned) sec);
        f.insert(f.end(), p.begin(), p.end());
    }
    put(f, 255);
    std::string path = tmp("sample.td0");
    writeFile(path, f);

    std::string err;
    auto        d = SoftDisk::load(path, 5, 40, false, err);
    CHECK(d != nullptr);
    if (!d)
    {
        printf("  %s\n", err.c_str());
        return;
    }
    CHECK(d->readOnlyFormat() && d->checkWriteProtect());
    std::vector<uint8_t> got;
    CHECK(readSector(*d, 0, 0, 2, got) && got == pattern(256, 2));
    CHECK(!writeSector(*d, 0, 0, 1, pattern(256, 9)));
    CHECK(!d->formatTrack(0, 0, true, formatStream(true, 0, 0, 2, 1, 0xe5)));
}

// ---- the WD1797 ----------------------------------------------------------------

struct FakeBus: public WD179xUserIf
{
    bool drq = false, intrq = false;
    void raiseIntrq() override
    {
        intrq = true;
    }
    void raiseDrq() override
    {
        drq = true;
    }
    void lowerIntrq() override
    {
        intrq = false;
    }
    void lowerDrq() override
    {
        drq = false;
    }
    int getClockPeriod() override
    {
        return 1000;
    }
    bool readReady() override
    {
        return true;
    }
};

struct Fdc
{
    FakeBus             bus;
    WD1797              wd{&bus};
    GenericFloppyDrive* drv;

    explicit Fdc(const char* type): drv(GenericFloppyDrive::getInstance(type))
    {
        drv->motor(true);
        wd.setCurrentDrive(drv);
    }
    ~Fdc()
    {
        delete drv;
    }

    void tick(unsigned n = 16)
    {
        WallClock::instance()->addTicks(n);
    }
    bool busy()
    {
        return wd.in(0) & 0x01;
    }
    /// Runs a command to its end, feeding `out` on each DRQ (Write) or
    /// collecting into `in` (Read); returns the final status.
    BYTE run(BYTE cmd, const std::vector<uint8_t>* out = nullptr, std::vector<uint8_t>* in = nullptr,
             uint8_t pad = 0x4e)
    {
        size_t at = 0;
        bus.intrq = false;
        wd.out(0, cmd);
        for (long i = 0; i < 400000 && busy(); ++i)
        {
            if (bus.drq)
            {
                if (in)
                {
                    in->push_back(wd.in(3));
                }
                else if (out)
                {
                    wd.out(3, at < out->size() ? (*out)[at] : pad);
                    ++at;
                }
            }
            tick();
        }
        return wd.in(0);
    }
};

void
testWd1797()
{
    std::string path = tmp("blank.imd");
    std::string err;
    CHECK(SoftDisk::createBlankImd(path, err));
    auto d = SoftDisk::load(path, 5, 40, false, err);
    CHECK(d != nullptr);
    if (!d)
    {
        return;
    }
    CHECK(!d->trackFormatted(0, 0));

    Fdc f("FDD_5_25_DS_ST");
    f.drv->insertDisk(d);

    // Restore, then format track 0 in MFM: 9 x 512
    BYTE st = f.run(0x00);
    CHECK((st & 0x05) == 0x04);   // track 0, not busy
    // idle after a Type I command, bit 1 is the index hole
    {
        bool bit1 = false;
        for (int i = 0; i < 30000; ++i)
        {
            f.tick();
            bit1 |= (f.wd.in(0) & 0x02) != 0;
        }
        CHECK(bit1);
    }
    f.wd.setDoubleDensity(true);
    std::vector<uint8_t> s = formatStream(true, 0, 0, 9, 2, 0xe5);
    st                     = f.run(0xf0, &s);
    CHECK_EQ_INT(st & 0xfc, 0);
    CHECK(d->trackFormatted(0, 0) && d->densityMatches(true, 0, 0));
    CHECK(d->dirty());

    // idle after a Type III command, bit 1 is DRQ, not the index hole: it
    // stays 0 through a whole revolution (CP/M's FORMAT checks it)
    {
        bool bit1 = false;
        for (int i = 0; i < 30000; ++i)
        {
            f.tick();
            bit1 |= (f.wd.in(0) & 0x02) != 0;
        }
        CHECK(!bit1);
    }

    // every sector reads back
    for (int sec = 1; sec <= 9; ++sec)
    {
        std::vector<uint8_t> got;
        f.wd.out(2, (BYTE) sec);
        st = f.run(0x80, nullptr, &got);
        CHECK_EQ_INT(st & 0xfc, 0);
        CHECK(got == std::vector<uint8_t>(512, 0xe5));
    }

    // Write Sector, Read Sector
    std::vector<uint8_t> p = pattern(512, 42), got;
    f.wd.out(2, 5);
    st = f.run(0xa0, &p);
    CHECK_EQ_INT(st & 0xfc, 0);
    f.wd.out(2, 5);
    st = f.run(0x80, nullptr, &got);
    CHECK(got == p);

    // Read Address: an ID of this track with a good CRC
    got.clear();
    st = f.run(0xc0, nullptr, &got);
    CHECK_EQ_INT(st & 0xfc, 0);
    CHECK_EQ_INT((int) got.size(), 6);
    if (got.size() == 6)
    {
        CHECK_EQ_INT(got[0], 0);
        CHECK_EQ_INT(got[3], 2);
    }

    // the other density finds nothing: Record Not Found
    f.wd.setDoubleDensity(false);
    f.wd.out(2, 1);
    got.clear();
    st = f.run(0x80, nullptr, &got);
    CHECK((st & 0x10) != 0);

    // step in to track 1 and format it FM: 16 x 128
    st = f.run(0x50);
    CHECK_EQ_INT(f.wd.in(1), 1);
    s  = formatStream(false, 1, 0, 16, 0, 0xe5);
    st = f.run(0xf0, &s, nullptr, 0xff);
    CHECK_EQ_INT(st & 0xfc, 0);
    f.wd.out(2, 16);
    got.clear();
    st = f.run(0x80, nullptr, &got);
    CHECK_EQ_INT(st & 0xfc, 0);
    CHECK(got == std::vector<uint8_t>(128, 0xe5));

    // write-protected: Write Sector and Write Track end at once
    d->setWriteProtected(true);
    st = f.run(0xa0, &p);
    CHECK((st & 0x41) == 0x40);
    st = f.run(0xf0, &s);
    CHECK((st & 0x41) == 0x40);
    f.bus.intrq = false;
    f.wd.out(0, 0xa0);
    CHECK(f.bus.intrq);   // (reading the status register clears it)
    d->setWriteProtected(false);

    // written back, and read again
    CHECK(d->flush(err));
    auto e = SoftDisk::load(path, 5, 40, false, err);
    CHECK(e != nullptr);
    if (e)
    {
        CHECK(e->densityMatches(true, 0, 0) && e->densityMatches(false, 1, 0));
        CHECK(readSector(*e, 0, 0, 5, got) && got == pattern(512, 42));
        CHECK(readSector(*e, 1, 0, 1, got) && got == std::vector<uint8_t>(128, 0xe5));
        CHECK_EQ_INT((int) e->track(0, 0)->sectors.size(), 9);
        CHECK_EQ_INT(e->track(0, 0)->imdMode, 5);
        CHECK_EQ_INT(e->track(1, 0)->imdMode, 2);
    }
}

}

int
main()
{
    char dir[1024];
    test_tmpdir(dir, sizeof dir, "softdisk");
    g_dir = dir;

    MachineContext        ctx;
    MachineContext::Scope scope(&ctx);

    testParser();
    testImd();
    testSectorImage();
    testRawImage();
    testTd0();
    testWd1797();

    // the media probe the session routes by: a raw floppy as long as an H17
    // raw image (128000 bytes) by its marks, the sector image by its trailer
    int inches = 0;
    CHECK_EQ_INT(h89image_soft_probe(tmp("sample.rawfloppy").c_str(), &inches), 3);
    CHECK_EQ_INT(inches, 5);
    CHECK_EQ_INT(h89image_soft_probe(tmp("sample.mmsdisk").c_str(), &inches), 2);
    CHECK_EQ_INT(h89image_soft_probe(tmp("sample.imd").c_str(), nullptr), 0);
    writeFile(tmp("h17.h17raw"), std::vector<uint8_t>(128000, 0));
    CHECK_EQ_INT(h89image_soft_probe(tmp("h17.h17raw").c_str(), nullptr), -1);
    return t_done("soft_disk");
}
