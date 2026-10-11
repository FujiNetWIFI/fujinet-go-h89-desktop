/// \file propertyutil.cpp
///
/// See propertyutil.h.
///
/// Copyright (C) 2026 Thomas Cherryhomes
/// SPDX-License-Identifier: GPL-3.0-or-later
///

#include "propertyutil.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>

std::vector<std::string>
PropertyUtil::splitArgs(std::string prop)
{
    std::vector<std::string> out;
    std::string              word;

    for (char c : prop)
    {
        if (isspace((unsigned char) c))
        {
            if (!word.empty())
            {
                out.push_back(word);
                word.clear();
            }
        }
        else
        {
            word += c;
        }
    }
    if (!word.empty())
    {
        out.push_back(word);
    }
    return out;
}

std::string
PropertyUtil::combineArgs(std::vector<std::string> args,
                          int                      start)
{
    std::string out;

    for (size_t i = start < 0 ? 0 : (size_t) start; i < args.size(); ++i)
    {
        if (!out.empty())
        {
            out += ' ';
        }
        out += args[i];
    }
    return out;
}

std::vector<std::string>
PropertyUtil::shiftArgs(std::vector<std::string> args,
                        int                      start)
{
    if (start <= 0)
    {
        return args;
    }
    if ((size_t) start >= args.size())
    {
        return {};
    }
    return std::vector<std::string>(args.begin() + start, args.end());
}

std::string
PropertyUtil::sprintf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    if (n <= 0)
    {
        return std::string();
    }
    std::string out((size_t) n + 1, '\0');
    va_start(ap, fmt);
    vsnprintf(&out[0], out.size(), fmt, ap);
    va_end(ap);
    out.resize((size_t) n);
    return out;
}
