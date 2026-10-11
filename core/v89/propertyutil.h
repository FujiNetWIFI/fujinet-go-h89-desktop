/// \file propertyutil.h
///
/// A small helper class with the interface the vendored VirtualH89 devices
/// expect from "PropertyUtil" (the property map type, argument splitting and
/// a std::string sprintf).
///
/// New code for FujiNet Go H89: VirtualH89's own propertyutil (a Java-style
/// properties reader by Paul D. Senzee) carries no licence, so it is not
/// vendored. Machine configuration comes from the session's settings store
/// instead (core/include/h89config.h); only these helpers remain.
///
/// Copyright (C) 2026 Thomas Cherryhomes
/// SPDX-License-Identifier: GPL-3.0-or-later
///

#ifndef PROPERTYUTIL_H_
#define PROPERTYUTIL_H_

#include <map>
#include <string>
#include <vector>

class PropertyUtil
{
  public:
    typedef std::map<std::string, std::string> PropertyMapT;

    /// Whitespace-separated words of a property value.
    static std::vector<std::string> splitArgs(std::string prop);
    /// args[start..] joined with single spaces.
    static std::string combineArgs(std::vector<std::string> args,
                                   int                      start = 0);
    /// args[start..] as a new vector.
    static std::vector<std::string> shiftArgs(std::vector<std::string> args,
                                              int                      start);
    /// printf into a std::string.
    static std::string sprintf(const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
};

#endif // PROPERTYUTIL_H_
