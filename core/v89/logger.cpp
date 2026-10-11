/// \file logger.cpp
///
/// The VirtualH89 logging back end, rewritten for FujiNet Go H89.
///
/// The original wrote to a FILE* opened by main() and printed the emulated
/// wall-clock time on every line. Here there is no main() of ours and many
/// of the messages the vendored devices emit at ERROR fire in normal
/// operation, so the default is silence: every subsystem logs FATAL only.
///
/// H89_LOG turns subsystems up, e.g.
///     H89_LOG=all               everything at INFO
///     H89_LOG=H17:VERBOSE,Z80   H17 at VERBOSE, Z80 at INFO
/// Names are the subSystems enumerators without their "ss" prefix (case
/// insensitive); levels are the logLevel names. Lines go to stderr.
///
/// Copyright (C) 2026 Thomas Cherryhomes
/// SPDX-License-Identifier: GPL-3.0-or-later
/// (the logger.h interface it implements is VirtualH89's, BSD-3-Clause,
/// Copyright (c) 2009-2016 Mark Garlanger)
///

#include "logger.h"

#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

unsigned debugLevel[ssMax];

namespace
{

const char* const s_names[ssMax] = {
    "H89", "MEM", "RAM", "ROM", "Z80", "InterruptController",
    "H37InterruptController", "AddressBus", "IO", "H17", "H37", "H47", "H67",
    "DiskDrive", "H17_1", "H17_4", "H47Drive", "Console", "H19", "8250",
    "Serial", "Timer", "WallClock", "FloppyDisk", "Gpp", "Parallel",
    "StdioConsole", "MMS77316", "WD1797", "GenericFloppyDrive",
    "RawFloppyImage", "MMS77320", "GenericSASIDrive", "HostFileBdos",
    "CPNetDevice", "SectorFloppyImage",
};

std::mutex s_lock;

bool
ieq(const std::string& a, const char* b)
{
    if (a.size() != strlen(b))
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (tolower((unsigned char) a[i]) != tolower((unsigned char) b[i]))
        {
            return false;
        }
    }
    return true;
}

unsigned
parseLevel(const std::string& s)
{
    static const struct { const char* name; logLevel level; } levels[] = {
        {"FATAL", FATAL}, {"ERROR", ERROR}, {"WARNING", WARNING},
        {"INFO", INFO}, {"VERBOSE", VERBOSE}, {"ALL", ALL},
    };
    for (const auto& l : levels)
    {
        if (ieq(s, l.name))
        {
            return l.level;
        }
    }
    return INFO;
}

struct Init
{
    Init()
    {
        setDebugLevel();
    }
} s_init;

} // namespace

void
setDebugLevel()
{
    for (unsigned& l : debugLevel)
    {
        l = FATAL;
    }

    const char* env = getenv("H89_LOG");
    if (!env || !*env)
    {
        return;
    }

    std::string spec(env);
    size_t      start = 0;
    while (start <= spec.size())
    {
        size_t      end  = spec.find(',', start);
        std::string item = spec.substr(start, end == std::string::npos ? std::string::npos
                                                                         : end - start);
        std::string name = item, level;
        size_t      colon = item.find(':');
        if (colon != std::string::npos)
        {
            name  = item.substr(0, colon);
            level = item.substr(colon + 1);
        }
        unsigned lv = level.empty() ? (unsigned) INFO : parseLevel(level);
        for (int ss = 0; ss < ssMax; ++ss)
        {
            if (ieq(name, "all") || ieq(name, s_names[ss]))
            {
                debugLevel[ss] = lv;
            }
        }
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
}

void
setDebug(subSystems ss,
         logLevel   level)
{
    debugLevel[ss] = level;
}

void
__debugss(enum logLevel level, const char* functionName, const char* fmt, ...)
{
    std::lock_guard<std::mutex> g(s_lock);
    va_list                     vl;

    fprintf(stderr, "v89 %s %s: ",
            level <= FATAL ? "FATAL" : level <= ERROR ? "ERROR" : level <= WARNING ? "WARN" : "INFO",
            functionName);
    va_start(vl, fmt);
    vfprintf(stderr, fmt, vl);
    va_end(vl);
}

void
__debugss_nts(const char* fmt, ...)
{
    std::lock_guard<std::mutex> g(s_lock);
    va_list                     vl;

    va_start(vl, fmt);
    vfprintf(stderr, fmt, vl);
    va_end(vl);
}
