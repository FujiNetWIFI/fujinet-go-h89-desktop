#!/usr/bin/env python3
#
# Check the disks tools/disks/make-fujinet-disks.sh made: read each data
# disk's directory the way its operating system does and make sure that,
# between them, the HDOS disks hold exactly fujinet-lib's .abs programs and
# the CP/M disks exactly the .COM programs make-fujinet-disks.sh staged for
# them, once each.
#
# CP/M directories also get the checks that matter on Heath's CP/M 2.2.04:
# no zeroed entry (DIR shows it as a nameless file) and no never-used entry
# (all E5) before the last file, where its directory search stops.
#
#   check-fujinet-disks.py DISKS_DIR HDOS_PROGRAMS_DIR CPM_FILES_DIR
#
# Copyright (C) 2026 Thomas Cherryhomes
# SPDX-License-Identifier: GPL-3.0-or-later

import pathlib
import sys

SECTOR = 256

# CP/M hz89: 3 boot tracks of 10 sectors, the directory in the first 2K
# (64 entries of 32 bytes), sectors in the image in skewed order
CPM_SKEW = [0, 4, 8, 2, 6, 1, 5, 9, 3, 7]
CPM_BOOT_TRACKS = 3
CPM_DIR_SECTORS = 8

# HDOS: the label is sector 9; its bytes 3-4 are the directory's first
# sector. A directory block is 512 bytes of 23-byte entries and a 6-byte
# trailer; an entry starting FE or FF is empty.
HDOS_LABEL = 9
HDOS_BLOCK = 512
HDOS_ENTRY = 23


def cpm_names(path, errors):
    image = path.read_bytes()
    entries = []
    for ls in range(CPM_DIR_SECTORS):
        off = ((CPM_BOOT_TRACKS * 10) + CPM_SKEW[ls]) * SECTOR
        sec = image[off:off + SECTOR]
        entries += [sec[e:e + 32] for e in range(0, SECTOR, 32)]
    used = [i for i, e in enumerate(entries) if e[0] != 0xE5]
    last = used[-1] if used else -1
    names = set()
    for i, e in enumerate(entries):
        if not any(e):
            errors.append(f"{path.name}: directory entry {i} is zeroed")
        elif e == b'\xe5' * 32 and i < last:
            errors.append(f"{path.name}: directory entry {i} is never-used "
                          "before the last file; CP/M stops there")
        elif e[0] <= 15:
            name = bytes(c & 0x7F for c in e[1:9]).decode().rstrip()
            ext = bytes(c & 0x7F for c in e[9:12]).decode().rstrip()
            names.add(f"{name}.{ext}")
    return names


def hdos_names(path, errors):
    image = path.read_bytes()
    label = image[HDOS_LABEL * SECTOR:(HDOS_LABEL + 1) * SECTOR]
    first = int.from_bytes(label[3:5], 'little')
    block = image[first * SECTOR:first * SECTOR + HDOS_BLOCK]
    names = set()
    slots = 0
    for e in range(0, HDOS_BLOCK - 6 - HDOS_ENTRY + 1, HDOS_ENTRY):
        slots += 1
        entry = block[e:e + HDOS_ENTRY]
        if entry[0] in (0xFE, 0xFF):
            continue
        name = entry[0:8].rstrip(b'\0').decode('ascii', 'replace')
        ext = entry[8:11].rstrip(b'\0').decode('ascii', 'replace')
        names.add(f"{name}.{ext}")
    if len(names) == slots:
        errors.append(f"{path.name}: the first directory block is full; "
                      "this check does not follow the chain")
    return names


def check(kind, disks, programs, reader, errors):
    expected = {p.name.upper() for p in programs}
    seen = []
    for disk in disks:
        names = reader(disk, errors)
        print(f"{disk.name}: {' '.join(sorted(names))}")
        seen += sorted(names)
    if not disks:
        errors.append(f"no {kind} disks")
    for name in sorted(set(seen)):
        if seen.count(name) > 1:
            errors.append(f"{name} is on more than one {kind} disk")
    missing = expected - set(seen)
    extra = set(seen) - expected
    if missing:
        errors.append(f"{kind}: missing {' '.join(sorted(missing))}")
    if extra:
        errors.append(f"{kind}: unexpected {' '.join(sorted(extra))}")


def main():
    if len(sys.argv) != 4:
        sys.exit(f"usage: {sys.argv[0]} DISKS_DIR HDOS_PROGRAMS_DIR CPM_FILES_DIR")
    out, hdos_src, cpm_src = (pathlib.Path(a) for a in sys.argv[1:])
    errors = []
    check("HDOS", sorted(out.glob("fujinet-hdos-*.h8d")),
          sorted(hdos_src.glob("*.abs")), hdos_names, errors)
    check("CP/M", sorted(out.glob("fujinet-cpm-*.h8d")),
          sorted(p for p in cpm_src.iterdir() if p.is_file()), cpm_names, errors)
    for e in errors:
        print(f"error: {e}", file=sys.stderr)
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
