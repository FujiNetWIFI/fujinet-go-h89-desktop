/*
 * DiskImage -- see DiskImage.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "DiskImage.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

bool
DiskImage::flush(std::string& err)
{
    if (!dirty_m.load())
    {
        return true;
    }
    std::vector<uint8_t> out;
    if (!serialise(out, err))
    {
        return false;
    }
    // what is written is what was dirty when serialised; a write landing
    // after this point sets it again
    dirty_m.store(false);
    if (!diskio::writeFile(path_m, out, err))
    {
        dirty_m.store(true);
        return false;
    }
    return true;
}

namespace diskio
{

bool
readFile(const std::string& path, std::vector<uint8_t>& out, std::string& err)
{
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f)
    {
        err = "cannot open " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size < 0 || size > 64LL * 1024 * 1024)
    {
        err = path + " is too large to be a disk image";
        return false;
    }
    f.seekg(0, std::ios::beg);
    out.resize((size_t) size);
    if (size && !f.read(reinterpret_cast<char*>(out.data()), size))
    {
        err = "cannot read " + path;
        return false;
    }
    return true;
}

static bool
writeAt(const fs::path& p, const std::vector<uint8_t>& data)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        return false;
    }
    if (!data.empty())
    {
        f.write(reinterpret_cast<const char*>(data.data()), (std::streamsize) data.size());
    }
    f.flush();
    return (bool) f;
}

bool
writeFile(const std::string& path, const std::vector<uint8_t>& data, std::string& err)
{
    fs::path        target = fs::u8path(path);
    fs::path        tmp    = target;
    std::error_code ec;

    tmp += ".h89tmp";
    if (writeAt(tmp, data))
    {
        fs::rename(tmp, target, ec);
        if (!ec)
        {
            return true;
        }
        fs::remove(tmp, ec);
    }
    // the folder is not ours to write in: write the file itself
    if (writeAt(target, data))
    {
        return true;
    }
    err = "cannot write " + path + ": " + strerror(errno);
    return false;
}

bool
writable(const std::string& path)
{
    std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::in | std::ios::out);
    return (bool) f;
}

std::string
extension(const std::string& path)
{
    std::string ext = fs::u8path(path).extension().u8string();
    if (!ext.empty() && ext[0] == '.')
    {
        ext.erase(0, 1);
    }
    for (char& c : ext)
    {
        c = (char) tolower((unsigned char) c);
    }
    return ext;
}

}
