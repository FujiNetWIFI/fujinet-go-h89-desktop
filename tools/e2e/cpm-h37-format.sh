#!/usr/bin/env bash
# End to end: soft-sectored CP/M on the Z-89-37. CP/M 2.2.04 with BIOS-80
# boots from its ImageDisk distribution disk, FORMATs an empty ImageDisk in
# drive 2 (the WD1797's Write Track, 80 tracks of 16 x 256 MFM), PIPs a file
# onto it, and a fresh session reads it back -- the disk and its write-back
# through the session.
#
# Needs: a build (build/), the Heath ROMs (tools/roms or --roms) and the
# CP/M 2.2.0X Z-89-37 disks (tools/disks/fetch-disks.sh).
#
#   tools/e2e/cpm-h37-format.sh [--roms DIR] [--build DIR]
#
# Copyright (C) 2026 Thomas Cherryhomes
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ROMS="${ROOT}/tools/roms"
BUILD="${ROOT}/build"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --roms) ROMS="$2"; shift 2 ;;
        --build) BUILD="$2"; shift 2 ;;
        *) echo "usage: $0 [--roms DIR] [--build DIR]" >&2; exit 2 ;;
    esac
done

OS_DISK="${ROOT}/tools/disks/images/HOS8917A.IMD"
RUN="${BUILD}/core/h89_session_run"
for f in "${OS_DISK}" "${RUN}"; do
    [[ -e "$f" ]] || { echo "missing: $f" >&2; exit 77; }
done

WORK="$(mktemp -d "${TMPDIR:-/tmp}/h89-e2e-XXXXXX")"
trap 'rm -rf "${WORK}"' EXIT
cp "${OS_DISK}" "${WORK}/cpm.imd"
# an empty ImageDisk, as New Blank Disk makes it
printf 'IMD 1.18: 01/01/2026 00:00:00\r\nblank\r\n\x1a' > "${WORK}/blank.imd"
H37=(--set slot.p505=h37 --set slot.p506=empty
     --disk "h37:1=${WORK}/cpm.imd" --disk "h37:2=${WORK}/blank.imd")

"${RUN}" --roms "${ROMS}" --home "${WORK}/home1" --timeout 400 "${H37[@]}" \
    --until 'H:' --type 'B\r' --until 'A>' --wait 500 \
    --prompt 'A>' --type 'FORMAT\r' \
    --prompt 'Is that what you want? (y/n):' --type 'y' \
    --prompt 'for this operation?:' --type 'B' \
    --prompt 'E=extended double):' --type 'D' \
    --prompt '(1=single, 2=double):' --type '1' \
    --until 'Press RETURN to begin.' --wait 500 --type '\r' \
    --until 'more disks to format' --type 'n' \
    --until 'press any character' --type ' ' --wait 2000 \
    --prompt 'A>' --type 'PIP B:=A:STAT.COM\r' --wait 6000 \
    --prompt 'A>' --screen > "${WORK}/format.log" 2>&1 \
    || { cat "${WORK}/format.log" >&2; echo "FORMAT/PIP failed" >&2; exit 1; }
if grep -q 'Unable to format' "${WORK}/format.log"; then
    echo "FORMAT failed" >&2; exit 1
fi

"${RUN}" --roms "${ROMS}" --home "${WORK}/home2" --timeout 120 "${H37[@]}" \
    --until 'H:' --type 'B\r' --until 'A>' --wait 500 \
    --prompt 'A>' --type 'B:STAT B:*.*\r' --wait 6000 \
    --screen > "${WORK}/reread.log" 2>&1 || true
sed -n '/---- screen ----/,/^\(\[fujinet\] \)\?----------------/p' "${WORK}/reread.log" | sed 's/^\[fujinet\] //'
grep -q 'R/W B:STAT.COM' "${WORK}/reread.log" || { echo "the copy did not read back" >&2; exit 1; }
