/*
 * TrackFormatter -- what a WD1797 Write Track put on the disk: the byte
 * stream the host wrote from index to index, with the controller's codes
 * still in it, turned into the sectors a later Read Sector finds.
 *
 * The codes (WD1797 datasheet, "Formatting the disk"):
 *   FM   F7 two CRC bytes; F8-FB a data address mark (F8 deleted); FC the
 *        index mark; FE an ID address mark. Marks follow 00 sync bytes.
 *   MFM  F5 an A1 sync byte (missing clock, CRC preset); F6 a C2 (before
 *        the index mark); F7 two CRC bytes; the marks follow the F5s.
 * An ID field is the mark, then cylinder, head, sector and size code, then
 * F7. Its data field is the next data mark (before another ID mark), then
 * 128 << (size & 3) bytes, then F7.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_TRACKFORMATTER_H
#define H89_TRACKFORMATTER_H

#include <cstdint>
#include <vector>

struct FormattedSector
{
    uint8_t              cyl = 0, head = 0, sec = 0, size = 0;   // the ID field
    bool                 deleted = false;   // F8 data mark
    bool                 hasData = false;   // no data field: an ID alone
    std::vector<uint8_t> data;
};

/// The sectors `stream` records, in the order they pass the head.
std::vector<FormattedSector> parseTrackStream(const std::vector<uint8_t>& stream, bool mfm);

#endif /* H89_TRACKFORMATTER_H */
