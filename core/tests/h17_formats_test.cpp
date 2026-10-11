/*
 * h17_formats -- the H17's disk images (core/machine/H17Image): every format
 * reads, writes back and reads again the same; the headers rebuilt for an
 * H8D carry the HDOS volume; the two raw side orders; the H17Disk draft's
 * captured sectors. With tools/disks/images present, the HDOS 2.0
 * distribution disks decode completely too.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "H17Image.h"
#include "test_tmpdir.h"
#include "test_util.h"

#include <cstring>
#include <filesystem>
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
writeAll(const std::string& path, const std::vector<uint8_t>& data)
{
    std::string err;
    CHECK(diskio::writeFile(path, data, err));
}

std::vector<uint8_t>
readAll(const std::string& path)
{
    std::vector<uint8_t> d;
    std::string          err;
    diskio::readFile(path, d, err);
    return d;
}

/// Writes a sector through the drive interface, byte by byte, as the H17
/// ROM's write routine does (re-encoding the whole window).
void
writeSector(H17Image& img, unsigned side, unsigned track, unsigned window, uint8_t vol,
            uint8_t sec, const uint8_t* data)
{
    uint8_t w[H17Image::sectorWindow_c];
    H17Image::encodeSector(w, vol, (uint8_t) track, sec, data);
    for (unsigned i = 0; i < H17Image::sectorWindow_c; ++i)
    {
        img.writeData((BYTE) side, (BYTE) track, window * H17Image::sectorWindow_c + i, w[i]);
    }
}

/// The sector a window holds.
bool
readSector(H17Image& img, unsigned side, unsigned track, unsigned window, uint8_t& vol,
           uint8_t& sec, uint8_t* data)
{
    uint8_t w[H17Image::sectorWindow_c], trk;
    for (unsigned i = 0; i < H17Image::sectorWindow_c; ++i)
    {
        img.readData((BYTE) side, (BYTE) track, window * H17Image::sectorWindow_c + i, w[i]);
    }
    return H17Image::decodeSector(w, H17Image::sectorWindow_c, vol, trk, sec, data) &&
           trk == track;
}

void
fill(uint8_t* d, uint8_t seed)
{
    for (unsigned i = 0; i < 256; ++i)
    {
        d[i] = (uint8_t) (seed * 7 + i * 13);
    }
}

void
testCodec()
{
    uint8_t w[320], data[256], out[256], vol, trk, sec;
    fill(data, 3);
    H17Image::encodeSector(w, 0x42, 7, 5, data);
    CHECK(H17Image::decodeSector(w, 320, vol, trk, sec, out));
    CHECK(vol == 0x42 && trk == 7 && sec == 5);
    CHECK(memcmp(data, out, 256) == 0);
    w[100] ^= 1;
    CHECK(!H17Image::decodeSector(w, 320, vol, trk, sec, out));
    // the ROM's checksum: XOR, then rotate left
    const uint8_t h[3] = {1, 2, 3};   // 1 -> 02, ^2 -> 00, ^3 -> 06
    CHECK_EQ_INT(H17Image::checksum(h, 3), 0x06);
}

void
testH8D()
{
    // an HDOS-like disk: a label with serial 0x42 in track 0 sector 9
    std::vector<uint8_t> h8d(102400, 0);
    for (size_t i = 0; i < h8d.size(); ++i)
    {
        h8d[i] = (uint8_t) (i * 31 >> 3);
    }
    uint8_t* label = &h8d[9 * 256];
    label[0]       = 0x42;  // serial
    label[3]       = 10;    // directory sector
    label[4]       = 0;
    label[5]       = 20;    // GRT sector
    label[6]       = 0;
    label[7]       = 2;     // sectors per group
    label[12]      = 400 & 0xff;
    label[13]      = 400 >> 8;
    writeAll(tmp("hdos.h8d"), h8d);

    std::string err;
    auto img = H17Image::load(tmp("hdos.h8d"), err);
    CHECK(img != nullptr);
    if (!img)
    {
        return;
    }
    img->setMaxTrack(40);
    CHECK(img->format() == H17Image::FormatH8D);
    CHECK(img->sides() == 1 && img->tracks() == 40);

    uint8_t vol, sec, data[256];
    CHECK(readSector(*img, 0, 0, 3, vol, sec, data));
    CHECK(vol == 0 && sec == 3);
    CHECK(memcmp(data, &h8d[3 * 256], 256) == 0);
    CHECK(readSector(*img, 0, 12, 4, vol, sec, data));
    CHECK(vol == 0x42 && sec == 4);
    CHECK(memcmp(data, &h8d[(12 * 10 + 4) * 256], 256) == 0);

    // write a sector, flush, the file holds it; nothing else changed
    uint8_t mine[256];
    fill(mine, 9);
    writeSector(*img, 0, 12, 4, 0x42, 4, mine);
    CHECK(img->dirty());
    CHECK(img->flush(err));
    CHECK(!img->dirty());
    std::vector<uint8_t> back = readAll(tmp("hdos.h8d"));
    CHECK(back.size() == h8d.size());
    CHECK(memcmp(&back[(12 * 10 + 4) * 256], mine, 256) == 0);
    memcpy(&h8d[(12 * 10 + 4) * 256], mine, 256);
    CHECK(back == h8d);

    // write-protected: writes are refused
    img->setWriteProtected(true);
    CHECK(!img->writeData(0, 1, 50, 0x99));
    bool hole, wp;
    img->getControlInfo(10, hole, wp);
    CHECK(hole && wp);
    img->getControlInfo(100, hole, wp);
    CHECK(!hole);
    img->getControlInfo(3050, hole, wp);   // the index hole
    CHECK(hole);

    // a 40-track disk in an 80-track drive steps twice per track
    img->setMaxTrack(80);
    CHECK(readSector(*img, 0, 24, 4, vol, sec, data) == false); // decodes, but as track 12
    uint8_t w[320], trk;
    for (unsigned i = 0; i < 320; ++i)
    {
        img->readData(0, 24, 4 * 320 + i, w[i]);
    }
    CHECK(H17Image::decodeSector(w, 320, vol, trk, sec, data) && trk == 12);
}

void
testH17D()
{
    std::string err;
    CHECK(H17Image::create(tmp("new.h17disk"), H17Image::FormatH17D, 2, 40, err));
    auto img = H17Image::load(tmp("new.h17disk"), err);
    CHECK(img != nullptr);
    if (!img)
    {
        return;
    }
    img->setMaxTrack(40);
    CHECK(img->format() == H17Image::FormatH17D);
    CHECK(img->sides() == 2 && img->tracks() == 40);

    uint8_t a[256], b[256], vol, sec, data[256];
    fill(a, 1);
    fill(b, 2);
    writeSector(*img, 0, 3, 7, 0x10, 7, a);
    writeSector(*img, 1, 3, 2, 0x10, 2, b);
    CHECK(img->flush(err));

    auto again = H17Image::load(tmp("new.h17disk"), err);
    CHECK(again != nullptr);
    if (!again)
    {
        return;
    }
    again->setMaxTrack(40);
    CHECK(readSector(*again, 0, 3, 7, vol, sec, data) && vol == 0x10);
    CHECK(memcmp(data, a, 256) == 0);
    CHECK(readSector(*again, 1, 3, 2, vol, sec, data));
    CHECK(memcmp(data, b, 256) == 0);
    CHECK(readSector(*again, 1, 39, 9, vol, sec, data) && vol == 0);

    // the H8DB block holds the data in the documented order (track, side, sector)
    std::vector<uint8_t> f = readAll(tmp("new.h17disk"));
    CHECK(memcmp(&f[256 + ((3 * 2 + 1) * 10 + 2) * 256], b, 256) == 0);
    CHECK(memcmp(f.data(), "H17D200\xff", 8) == 0);
}

void
testRawOrders()
{
    // a two-sided 80-track raw image in each order; side 1 track 0 sector 0
    // holds a marker
    for (int interleaved = 0; interleaved < 2; ++interleaved)
    {
        std::vector<uint8_t> raw(512000, 0);
        for (unsigned side = 0; side < 2; ++side)
        {
            for (unsigned t = 0; t < 80; ++t)
            {
                unsigned slot = interleaved ? t * 2 + side : side * 80 + t;
                for (unsigned s = 0; s < 10; ++s)
                {
                    uint8_t d[256];
                    fill(d, (uint8_t) (side * 100 + t + s));
                    H17Image::encodeSector(&raw[(size_t) slot * 3200 + s * 320], 0, (uint8_t) t,
                                           (uint8_t) s, d);
                }
            }
        }
        std::string path = tmp(interleaved ? "z88dk.h17raw" : "v89.h17raw");
        writeAll(path, raw);
        std::string err;
        auto img = H17Image::load(path, err);
        CHECK(img != nullptr);
        if (!img)
        {
            continue;
        }
        img->setMaxTrack(80);
        CHECK(img->sides() == 2 && img->tracks() == 80);
        uint8_t vol, sec, data[256], want[256];
        fill(want, 100 + 5 + 3);
        CHECK(readSector(*img, 1, 5, 3, vol, sec, data));
        CHECK(memcmp(data, want, 256) == 0);

        // written back in the order it came in
        uint8_t mine[256];
        fill(mine, 77);
        writeSector(*img, 1, 5, 3, 0, 3, mine);
        CHECK(img->flush(err));
        std::vector<uint8_t> back = readAll(path);
        unsigned slot = interleaved ? 5 * 2 + 1 : 80 + 5;
        uint8_t  v2, t2, s2, d2[256];
        CHECK(H17Image::decodeSector(&back[(size_t) slot * 3200 + 3 * 320], 320, v2, t2, s2, d2));
        CHECK(memcmp(d2, mine, 256) == 0);
    }
}

void
testDraft()
{
    // a minimal draft H17Disk: 1 side, 40 tracks, sectors captured as 350
    // bytes (the frame, then the start of the next)
    std::vector<uint8_t> f = {'H', '1', '7', 'D', 0x01, 0x00, 0x00};
    auto block = [&](uint8_t id, uint8_t flags, const std::vector<uint8_t>& body) {
        f.push_back(id);
        f.push_back(flags);
        uint32_t n = (uint32_t) body.size();
        f.push_back((uint8_t) (n >> 24));
        f.push_back((uint8_t) (n >> 16));
        f.push_back((uint8_t) (n >> 8));
        f.push_back((uint8_t) n);
        f.insert(f.end(), body.begin(), body.end());
    };
    block(0x00, 0x80, {1, 40});
    block(0x01, 0x80, {0, 1, 3});
    std::vector<uint8_t> data;
    for (unsigned t = 0; t < 40; ++t)
    {
        unsigned tl = 10 * (5 + 350);
        data.insert(data.end(), {0x11, 0, (uint8_t) t, (uint8_t) (tl >> 8), (uint8_t) tl});
        for (unsigned s = 0; s < 10; ++s)
        {
            data.insert(data.end(), {0x12, (uint8_t) s, 0, 350 >> 8, 350 & 0xff});
            uint8_t w[350] = {0}, d[256];
            fill(d, (uint8_t) (t + s));
            H17Image::encodeSector(w, 0, (uint8_t) t, (uint8_t) s, d);
            data.insert(data.end(), w, w + 350);
        }
    }
    block(0x10, 0x80, data);
    writeAll(tmp("draft.h17disk"), f);

    std::string err;
    auto img = H17Image::load(tmp("draft.h17disk"), err);
    CHECK(img != nullptr);
    if (!img)
    {
        printf("  (%s)\n", err.c_str());
        return;
    }
    img->setMaxTrack(40);
    CHECK(img->format() == H17Image::FormatH17DDraft);
    uint8_t vol, sec, d[256], want[256];
    fill(want, 17 + 6);
    CHECK(readSector(*img, 0, 17, 6, vol, sec, d));
    CHECK(memcmp(d, want, 256) == 0);

    uint8_t mine[256];
    fill(mine, 200);
    writeSector(*img, 0, 17, 6, 0, 6, mine);
    CHECK(img->flush(err));
    auto again = H17Image::load(tmp("draft.h17disk"), err);
    CHECK(again != nullptr);
    if (again)
    {
        again->setMaxTrack(40);
        CHECK(readSector(*again, 0, 17, 6, vol, sec, d));
        CHECK(memcmp(d, mine, 256) == 0);
    }
}

void
testBlankAndRejects()
{
    std::string err;
    CHECK(H17Image::create(tmp("blank.h8d"), H17Image::FormatH8D, 1, 40, err));
    CHECK(readAll(tmp("blank.h8d")).size() == 102400);
    CHECK(H17Image::create(tmp("blank.h17raw"), H17Image::FormatRaw, 2, 80, err));
    CHECK(readAll(tmp("blank.h17raw")).size() == 512000);
    CHECK(!H17Image::create(tmp("bad.h8d"), H17Image::FormatH8D, 3, 40, err));

    writeAll(tmp("junk.bin"), std::vector<uint8_t>(1234, 0));
    CHECK(H17Image::load(tmp("junk.bin"), err) == nullptr);
    CHECK(!err.empty());
}

void
testDistributionDisks()
{
    const char* names[] = {"HDOS_2_0_Operating_System.h17disk", "HDOS_2_0_Software_Tools.h17disk",
                           "HDOS_2_0_Device_Drivers.h17disk"};
    for (const char* n : names)
    {
        std::string path = std::string(H89_SOURCE_DIR) + "/tools/disks/images/" + n;
        if (!std::filesystem::exists(path))
        {
            printf("  (%s not present; skipped)\n", n);
            continue;
        }
        std::string err;
        auto img = H17Image::load(path, err);
        CHECK(img != nullptr);
        if (!img)
        {
            continue;
        }
        img->setMaxTrack(40);
        CHECK(img->writeProtected());   // the R/O flag in the image
        int good = 0;
        for (unsigned t = 0; t < 40; ++t)
        {
            for (unsigned w = 0; w < 10; ++w)
            {
                uint8_t vol, sec, d[256];
                good += readSector(*img, 0, t, w, vol, sec, d);
            }
        }
        printf("  %s: %d/400 sectors decode\n", n, good);
        CHECK(good >= 390);
    }
}

}

int
main()
{
    char dir[1024];
    test_tmpdir(dir, sizeof dir, "h17");
    g_dir = dir;

    testCodec();
    testH8D();
    testH17D();
    testRawOrders();
    testDraft();
    testBlankAndRejects();
    testDistributionDisks();

    std::error_code ec;
    std::filesystem::remove_all(g_dir, ec);
    return t_done("h17_formats");
}
