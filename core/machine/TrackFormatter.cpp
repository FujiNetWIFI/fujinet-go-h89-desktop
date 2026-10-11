/*
 * TrackFormatter -- see TrackFormatter.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "TrackFormatter.h"

#include <cstddef>

namespace
{

const uint8_t CRC_CODE   = 0xf7;
const uint8_t A1_CODE    = 0xf5;
const uint8_t IDAM       = 0xfe;
// how far past an ID field its data mark may be: the standard gap 2 is 17
// bytes in FM and 37 in MFM (with its syncs); some formats stretch it
const size_t  DAM_WINDOW = 64;

bool
isDataMark(uint8_t b)
{
    return b >= 0xf8 && b <= 0xfb;
}

/// A mark is a mark only where the controller would write it with the
/// missing clock: after the F5 syncs in MFM, after a 00 sync byte in FM.
bool
marked(const std::vector<uint8_t>& s, size_t i, bool mfm)
{
    if (i == 0)
    {
        return !mfm;
    }
    return mfm ? s[i - 1] == A1_CODE : s[i - 1] == 0x00;
}

}

std::vector<FormattedSector>
parseTrackStream(const std::vector<uint8_t>& s, bool mfm)
{
    std::vector<FormattedSector> out;
    size_t                       i = 0;

    while (i + 6 <= s.size())
    {
        if (s[i] != IDAM || !marked(s, i, mfm))
        {
            ++i;
            continue;
        }

        FormattedSector f;
        f.cyl  = s[i + 1];
        f.head = s[i + 2];
        f.sec  = s[i + 3];
        f.size = s[i + 4];
        if (s[i + 5] != CRC_CODE)
        {
            // not an ID field the controller wrote whole
            ++i;
            continue;
        }
        i += 6;

        // its data field: the next data mark, unless another ID comes first
        size_t len = (size_t) 128 << (f.size & 3);
        for (size_t j = i; j < s.size() && j < i + DAM_WINDOW; ++j)
        {
            if (s[j] == IDAM && marked(s, j, mfm))
            {
                break;
            }
            if (isDataMark(s[j]) && marked(s, j, mfm))
            {
                if (j + 1 + len < s.size() && s[j + 1 + len] == CRC_CODE)
                {
                    f.hasData = true;
                    f.deleted = s[j] == 0xf8;
                    f.data.assign(s.begin() + (std::ptrdiff_t) (j + 1),
                                  s.begin() + (std::ptrdiff_t) (j + 1 + len));
                    i = j + 2 + len;
                }
                break;
            }
        }
        out.push_back(std::move(f));
    }
    return out;
}
