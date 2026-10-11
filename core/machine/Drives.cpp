/*
 * Drives -- see h89drives.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "h89drives.h"

#include "DiskDrive.h"
#include "DiskFactory.h"
#include "DiskImage.h"
#include "GenericFloppyDrive.h"
#include "GenericSASIDrive.h"
#include "H17Image.h"
#include "Machine.h"
#include "drives_internal.h"
#include "host.h"
#include "h17.h"
#include "h89config.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

const char* const ctrlNames[H89_CTRL_COUNT] = {"H17", "H37", "MMS 77316", "MMS 77320"};

struct Lock
{
    Lock()
    {
        h89host_lock();
    }
    ~Lock()
    {
        h89host_unlock();
    }
};

void
setErr(char* err, int errsz, const std::string& msg)
{
    if (err && errsz > 0)
    {
        snprintf(err, (size_t) errsz, "%s", msg.c_str());
    }
}

/// The drive ids the machine has, in order.
std::vector<int>
driveIds(Machine& m)
{
    std::vector<int> ids;
    for (int u = 0; u < H89_H17_DRIVES; ++u)
    {
        if (m.h17() && m.h17Drive(u))
        {
            ids.push_back(H89_DRIVE_ID(H89_CTRL_H17, u));
        }
    }
    for (int u = 0; u < H89_H37_DRIVES; ++u)
    {
        if (m.h37() && m.h37Drive(u))
        {
            ids.push_back(H89_DRIVE_ID(H89_CTRL_H37, u));
        }
    }
    for (int u = 0; u < H89_MMS316_DRIVES; ++u)
    {
        if (m.mms316() && m.mms316Drive(u))
        {
            ids.push_back(H89_DRIVE_ID(H89_CTRL_MMS77316, u));
        }
    }
    for (int u = 0; u < H89_MMS320_DRIVES; ++u)
    {
        if (m.mms320() && m.mms320Drive(u))
        {
            ids.push_back(H89_DRIVE_ID(H89_CTRL_MMS77320, u));
        }
    }
    return ids;
}

bool
fitted(Machine& m, int id)
{
    for (int x : driveIds(m))
    {
        if (x == id)
        {
            return true;
        }
    }
    return false;
}

void
fillInfo(Machine& m, int id, h89_drive_info* out)
{
    const h89_config& cfg  = m.config();
    int               ctrl = H89_DRIVE_CTRL(id), unit = H89_DRIVE_UNIT(id);

    memset(out, 0, sizeof *out);
    out->id        = id;
    out->ctrl      = ctrl;
    out->unit      = unit;
    out->removable = ctrl != H89_CTRL_MMS77320;

    switch (ctrl)
    {
        case H89_CTRL_H17:
            snprintf(out->label, sizeof out->label, "H17 drive %d", unit + 1);
            snprintf(out->type, sizeof out->type, "%s", h89_h17_drive_name(cfg.h17_drive[unit]));
            break;
        case H89_CTRL_H37:
            snprintf(out->label, sizeof out->label, "H37 drive %d", unit + 1);
            snprintf(out->type, sizeof out->type, "%s", h89_fdd_name(cfg.h37_drive[unit]));
            break;
        case H89_CTRL_MMS77316:
            snprintf(out->label, sizeof out->label, "MMS 77316 drive %d (%s)", unit + 1,
                     unit < 4 ? "8\"" : "5.25\"");
            snprintf(out->type, sizeof out->type, "%s", h89_fdd_name(cfg.mms316_drive[unit]));
            break;
        case H89_CTRL_MMS77320:
            snprintf(out->label, sizeof out->label, "MMS 77320 unit %d", unit);
            snprintf(out->type, sizeof out->type, "%s", h89_sasi_name(cfg.mms320_drive[unit]));
            out->loaded = 1;
            snprintf(out->format, sizeof out->format, "SASI");
            break;
    }

    const std::shared_ptr<DiskImage>& img = m.mounted(id);
    if (img)
    {
        out->loaded           = 1;
        snprintf(out->path, sizeof out->path, "%s", img->path().c_str());
        snprintf(out->format, sizeof out->format, "%s", img->formatName());
        out->write_protected  = img->writeProtected();
        out->read_only_format = img->readOnlyFormat();
        out->dirty            = img->dirty();
    }
    out->active = drives_internal::active(m, id);
}

std::string
flushOne(const std::shared_ptr<DiskImage>& img)
{
    std::string err;
    if (img && img->dirty() && !img->flush(err))
    {
        return err;
    }
    return std::string();
}

} // namespace

namespace drives_internal
{

bool
active(Machine& m, int id)
{
    // the controllers do not say which drive is spinning in a common way;
    // the H17's is enough for an activity light today
    (void) m;
    (void) id;
    return false;
}

void
service(Machine& m, uint64_t slices)
{
    // write an image back once its writes have stopped for a second:
    // checked every ~1 s of emulated time, flushed if nothing was written
    // since the last check
    static uint64_t lastCheck = 0;
    static std::vector<std::pair<const DiskImage*, uint64_t>> seen;

    if (slices - lastCheck < 500 && slices >= lastCheck)
    {
        return;
    }
    lastCheck = slices;

    std::vector<std::pair<const DiskImage*, uint64_t>> now;
    for (int id : driveIds(m))
    {
        const std::shared_ptr<DiskImage>& img = m.mounted(id);
        if (!img || !img->dirty())
        {
            continue;
        }
        uint64_t writes = img->writeCount();
        bool     quiet  = false;
        for (auto& s : seen)
        {
            if (s.first == img.get() && s.second == writes)
            {
                quiet = true;
            }
        }
        if (quiet)
        {
            std::string err = flushOne(img);
            if (!err.empty())
            {
                fprintf(stderr, "h89: %s\n", err.c_str());
            }
        }
        else
        {
            now.emplace_back(img.get(), writes);
        }
    }
    seen.swap(now);
}

bool
createBlankImd(const std::string& path, std::string& err)
{
    // an ImageDisk with its header and no tracks: the H89 formats it
    std::string          hdr = "IMD 1.18: FujiNet Go H89 blank disk\r\n";
    std::vector<uint8_t> out(hdr.begin(), hdr.end());
    out.push_back(0x1a);
    return diskio::writeFile(path, out, err);
}

void
protectChanged(Machine& m, int id)
{
    (void) m;
    (void) id;
}

int
flushAll(Machine& m, std::string& firstErr)
{
    int failed = 0;
    for (int id : driveIds(m))
    {
        std::string err = flushOne(m.mounted(id));
        if (!err.empty())
        {
            if (firstErr.empty())
            {
                firstErr = err;
            }
            ++failed;
        }
    }
    return failed;
}

} // namespace drives_internal

extern "C" {

const char*
h89drive_ctrl_name(int ctrl)
{
    return (ctrl >= 0 && ctrl < H89_CTRL_COUNT) ? ctrlNames[ctrl] : "?";
}

int
h89drive_count(void)
{
    Lock     l;
    Machine* m = h89host_machine();
    return m ? (int) driveIds(*m).size() : 0;
}

int
h89drive_info_at(int index, h89_drive_info* out)
{
    Lock     l;
    Machine* m = h89host_machine();
    if (!m || !out)
    {
        return -1;
    }
    std::vector<int> ids = driveIds(*m);
    if (index < 0 || index >= (int) ids.size())
    {
        return -1;
    }
    fillInfo(*m, ids[(size_t) index], out);
    return 0;
}

int
h89drive_info(int id, h89_drive_info* out)
{
    Lock     l;
    Machine* m = h89host_machine();
    if (!m || !out || !fitted(*m, id))
    {
        return -1;
    }
    fillInfo(*m, id, out);
    return 0;
}

int
h89drive_eject(int id, char* err, int errsz)
{
    Lock     l;
    Machine* m = h89host_machine();
    if (!m)
    {
        setErr(err, errsz, "the machine is not running");
        return -1;
    }
    if (!fitted(*m, id))
    {
        setErr(err, errsz, "there is no such drive");
        return -1;
    }
    std::shared_ptr<DiskImage>& img = m->mounted(id);
    std::string                 e   = flushOne(img);

    switch (H89_DRIVE_CTRL(id))
    {
        case H89_CTRL_H17:
            m->h17Drive(H89_DRIVE_UNIT(id))->ejectDisk("");
            break;
        case H89_CTRL_H37:
            m->h37Drive(H89_DRIVE_UNIT(id))->insertDisk(nullptr);
            break;
        case H89_CTRL_MMS77316:
            m->mms316Drive(H89_DRIVE_UNIT(id))->insertDisk(nullptr);
            break;
        default:
            setErr(err, errsz, "the MMS 77320's disks are fixed");
            return -1;
    }
    img.reset();
    if (!e.empty())
    {
        setErr(err, errsz, "ejected, but the last changes could not be saved: " + e);
        return -1;
    }
    return 0;
}

int
h89drive_insert(int id, const char* path, int flags, char* err, int errsz)
{
    Lock     l;
    Machine* m = h89host_machine();
    if (!m)
    {
        setErr(err, errsz, "the machine is not running");
        return -1;
    }
    if (!path || !*path)
    {
        setErr(err, errsz, "no file");
        return -1;
    }
    if (!fitted(*m, id))
    {
        setErr(err, errsz, "there is no such drive in this machine");
        return -1;
    }
    bool        ro = (flags & H89DRIVE_READ_ONLY) != 0;
    std::string e;

    switch (H89_DRIVE_CTRL(id))
    {
        case H89_CTRL_H17:
        {
            std::shared_ptr<H17Image> img(H17Image::load(path, e).release());
            if (!img)
            {
                setErr(err, errsz, e);
                return -1;
            }
            if (ro)
            {
                img->setWriteProtected(true);
            }
            if (m->mounted(id))
            {
                h89drive_eject(id, nullptr, 0);
            }
            m->h17Drive(H89_DRIVE_UNIT(id))->insertDisk(img);
            m->mounted(id) = img;
            return 0;
        }

        case H89_CTRL_H37:
        case H89_CTRL_MMS77316:
        {
            GenericFloppyDrive* drv = H89_DRIVE_CTRL(id) == H89_CTRL_H37
                                          ? m->h37Drive(H89_DRIVE_UNIT(id))
                                          : m->mms316Drive(H89_DRIVE_UNIT(id));
            SoftMount sm;
            if (!loadSoftImage(path, drv, ro, sm, e))
            {
                setErr(err, errsz, e);
                return -1;
            }
            if (m->mounted(id))
            {
                h89drive_eject(id, nullptr, 0);
            }
            drv->insertDisk(sm.disk);
            m->mounted(id) = sm.image;
            return 0;
        }

        default:
            setErr(err, errsz, "the MMS 77320's disks are fixed");
            return -1;
    }
}

int
h89drive_set_write_protect(int id, int protect)
{
    Lock     l;
    Machine* m = h89host_machine();
    if (!m || !fitted(*m, id) || !m->mounted(id))
    {
        return -1;
    }
    m->mounted(id)->setWriteProtected(protect != 0);
    if (H89_DRIVE_CTRL(id) == H89_CTRL_H17)
    {
        auto* f = dynamic_cast<FloppyDisk*>(m->mounted(id).get());
        if (f)
        {
            f->setWriteProtect(m->mounted(id)->writeProtected());
        }
    }
    drives_internal::protectChanged(*m, id);
    return 0;
}

int
h89drive_flush_all(void)
{
    Lock     l;
    Machine* m = h89host_machine();
    if (!m)
    {
        return 0;
    }
    std::string e;
    return drives_internal::flushAll(*m, e) ? -1 : 0;
}

int
h89drive_insert_spec(const char* spec, char* err, int errsz)
{
    // ctrl:unit=path[:ro]
    std::string s(spec ? spec : "");
    size_t      colon = s.find(':'), eq = s.find('=');
    if (colon == std::string::npos || eq == std::string::npos || eq < colon)
    {
        setErr(err, errsz, "expected CONTROLLER:UNIT=PATH");
        return -1;
    }
    std::string name = s.substr(0, colon);
    int         unit = atoi(s.substr(colon + 1, eq - colon - 1).c_str());
    std::string path = s.substr(eq + 1);
    int         flags = 0;
    if (path.size() > 3 && path.compare(path.size() - 3, 3, ":ro") == 0)
    {
        flags |= H89DRIVE_READ_ONLY;
        path.resize(path.size() - 3);
    }
    int ctrl = -1;
    if (name == "h17")
    {
        ctrl = H89_CTRL_H17;
    }
    else if (name == "h37")
    {
        ctrl = H89_CTRL_H37;
    }
    else if (name == "mms77316" || name == "mms316")
    {
        ctrl = H89_CTRL_MMS77316;
    }
    if (ctrl < 0)
    {
        setErr(err, errsz, "unknown controller " + name);
        return -1;
    }
    return h89drive_insert(H89_DRIVE_ID(ctrl, unit - 1), path.c_str(), flags, err, errsz);
}

int
h89image_create_blank(const char* path, int kind, int sides, int tracks, char* err, int errsz)
{
    std::string e;
    bool        ok = false;
    switch (kind)
    {
        case 0: // H89_BLANK_H17_H8D
            ok = H17Image::create(path, H17Image::FormatH8D, (unsigned) sides, (unsigned) tracks, e);
            break;
        case 1: // H89_BLANK_H17_H17D
            ok = H17Image::create(path, H17Image::FormatH17D, (unsigned) sides, (unsigned) tracks, e);
            break;
        case 2: // H89_BLANK_H17_RAW
            ok = H17Image::create(path, H17Image::FormatRaw, (unsigned) sides, (unsigned) tracks, e);
            break;
        case 3: // H89_BLANK_IMD
            ok = drives_internal::createBlankImd(path, e);
            break;
        default:
            e = "unknown kind of disk";
            break;
    }
    if (!ok)
    {
        setErr(err, errsz, e);
        return -1;
    }
    return 0;
}

} // extern "C"
