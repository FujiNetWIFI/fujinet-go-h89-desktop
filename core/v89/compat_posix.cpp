/// \file compat_posix.cpp
///
/// See compat_posix.h.
///
/// Copyright (C) 2026 Thomas Cherryhomes
/// SPDX-License-Identifier: GPL-3.0-or-later
///

#include "compat_posix.h"

namespace
{

/// Matches a [...] class at p against c; sets *end past the class.
bool
classMatch(const char* p, char c, const char** end)
{
    bool negate = (*p == '!' || *p == '^');
    bool hit    = false;
    if (negate)
    {
        ++p;
    }
    const char* start = p;
    while (*p && (*p != ']' || p == start))
    {
        char lo = *p, hi = *p;
        if (p[1] == '-' && p[2] && p[2] != ']')
        {
            hi = p[2];
            p += 2;
        }
        if (c >= lo && c <= hi)
        {
            hit = true;
        }
        ++p;
    }
    *end = *p == ']' ? p + 1 : p;
    return hit != negate;
}

bool
match(const char* p, const char* s)
{
    while (*p)
    {
        switch (*p)
        {
            case '*':
                while (*p == '*')
                {
                    ++p;
                }
                if (!*p)
                {
                    return true;
                }
                for (; *s; ++s)
                {
                    if (match(p, s))
                    {
                        return true;
                    }
                }
                return false;
            case '?':
                if (!*s)
                {
                    return false;
                }
                ++p;
                ++s;
                break;
            case '[':
            {
                const char* end;
                if (!*s || !classMatch(p + 1, *s, &end))
                {
                    return false;
                }
                p = end;
                ++s;
                break;
            }
            default:
                if (*p != *s)
                {
                    return false;
                }
                ++p;
                ++s;
                break;
        }
    }
    return *s == '\0';
}

}

int
v89_fnmatch(const char* pattern, const char* string)
{
    return match(pattern, string) ? 0 : 1;
}

long
v89_gmtoff(time_t t)
{
#if defined(_WIN32)
    struct tm local, utc;
    localtime_s(&local, &t);
    gmtime_s(&utc, &t);
    // the difference of the two broken-down times, read back as local
    utc.tm_isdst = local.tm_isdst;
    return (long) difftime(mktime(&local), mktime(&utc));
#else
    struct tm local;
    localtime_r(&t, &local);
    return local.tm_gmtoff;
#endif
}
