/*
 * drives_internal -- the drive layer's hooks for the host (Drives.cpp).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_DRIVES_INTERNAL_H
#define H89_DRIVES_INTERNAL_H

#include <cstdint>
#include <string>

class Machine;

namespace drives_internal
{
/// The machine thread, between batches: writes back images whose writes
/// have stopped (about a second after the last one).
void service(Machine& m, uint64_t slices);
/// Writes every dirty image back; returns how many failed.
int flushAll(Machine& m, std::string& firstErr);
/// Whether the drive is selected with its motor on.
bool active(Machine& m, int id);
/// The write-protect tab of drive `id` changed: tell its drive.
void protectChanged(Machine& m, int id);
/// An empty ImageDisk file.
bool createBlankImd(const std::string& path, std::string& err);
}

#endif /* H89_DRIVES_INTERNAL_H */
