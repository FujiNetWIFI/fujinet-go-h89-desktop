/// \file compat_posix.h
///
/// The few POSIX calls the vendored VirtualH89 sources make that Windows
/// (MinGW-w64) does not have, and O_BINARY for every open(): without it a
/// Windows build would translate line endings inside disk images.
///
/// New code for FujiNet Go H89.
///
/// Copyright (C) 2026 Thomas Cherryhomes
/// SPDX-License-Identifier: GPL-3.0-or-later
///

#ifndef COMPAT_POSIX_H_
#define COMPAT_POSIX_H_

#include <cstring>
#include <fcntl.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#if defined(_WIN32)
#include <io.h>

static inline int
ffs(int i)
{
    return __builtin_ffs(i);
}

static inline void
bzero(void* p, size_t n)
{
    memset(p, 0, n);
}
#else
#include <strings.h>
#endif

#include <ctime>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
/// mkdir(path, mode): Windows has no mode
#define v89_mkdir(path, mode) _mkdir(path)
static inline struct tm*
localtime_r(const time_t* t, struct tm* out)
{
    return localtime_s(out, t) == 0 ? out : nullptr;
}
#else
#define v89_mkdir(path, mode) mkdir(path, mode)
#endif

/// Seconds east of UTC for local time at `t` (struct tm's tm_gmtoff, which
/// Windows does not have).
long v89_gmtoff(time_t t);

/// fnmatch(pattern, string, 0): '*', '?' and '[...]' (with '!' or '^' to
/// negate, and ranges), no escapes -- what HostFileBdos builds from CP/M
/// file names. Returns 0 on a match, as fnmatch does.
int v89_fnmatch(const char* pattern, const char* string);

#endif // COMPAT_POSIX_H_
