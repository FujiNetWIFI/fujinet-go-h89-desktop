#!/usr/bin/env bash
# End to end: HDOS 2.0 on the emulated H89 runs fujinet-lib's FUJIDEV
# integration test against the in-process FujiNet, through the session
# (build/core/h89_session_run) -- the 82C55 card, SLIP-framed FujiBus, BoIP
# and libfujinet, with nothing faked.
#
# Needs: a build with the FujiNet runtime (build/), the Heath ROMs
# (tools/roms or --roms), the HDOS 2.0 OS disk (tools/disks/fetch-disks.sh),
# fujinet-lib's HDOS test programs (testing/r2r/h89-hdos/fujidev.abs, made
# by fujinet-lib's `make -C testing h89-hdos`) and z88dk's appmake, which
# puts FUJIDEV.ABS on a blank HDOS disk (on PATH, or $APPMAKE).
#
#   tools/e2e/hdos-fujidev.sh [--abs PATH] [--roms DIR] [--build DIR]
#
# Copyright (C) 2026 Thomas Cherryhomes
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ABS="${HOME}/Workspace/fujinet-lib/testing/r2r/h89-hdos/fujidev.abs"
ROMS="${ROOT}/tools/roms"
BUILD="${ROOT}/build"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --abs) ABS="$2"; shift 2 ;;
        --roms) ROMS="$2"; shift 2 ;;
        --build) BUILD="$2"; shift 2 ;;
        *) echo "usage: $0 [--abs PATH] [--roms DIR] [--build DIR]" >&2; exit 2 ;;
    esac
done

OS_DISK="${ROOT}/tools/disks/images/HDOS_2_0_Operating_System.h17disk"
RUN="${BUILD}/core/h89_session_run"
for f in "${OS_DISK}" "${ABS}" "${RUN}"; do
    [[ -e "$f" ]] || { echo "missing: $f" >&2; exit 77; }
done
APPMAKE="${APPMAKE:-$(command -v z88dk-appmake || echo "${HOME}/Workspace/z88dk/bin/z88dk-appmake")}"
[[ -x "${APPMAKE}" ]] || { echo "missing: z88dk-appmake (set APPMAKE)" >&2; exit 77; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/h89-e2e-XXXXXX")"
trap 'rm -rf "${WORK}"' EXIT

# The program's bytes after its 8-byte ABS header (FF 00, load, length,
# entry), on a fresh 100K HDOS disk at HDOS's program origin.
tail -c +9 "${ABS}" > "${WORK}/fujidev.bin"
(cd "${WORK}" && "${APPMAKE}" +hdos -b fujidev.bin --org 0x4480 -o FUJIDEV >/dev/null)
DISK="$(ls "${WORK}"/*.h8d | head -1)"
cp "${OS_DISK}" "${WORK}/os.h17disk"

"${RUN}" --roms "${ROMS}" --home "${WORK}/home" --timeout 300 \
    --disk "h17:1=${WORK}/os.h17disk" --disk "h17:2=${DISK}" \
    --until 'H:' --type 'B\r' --wait 3000 --type ' ' --wait 500 --type ' ' \
    --prompt 'ACTION? <BOOT>' --type '\r' \
    --prompt 'DATE (DD-MMM-YY)?' --type '11-OCT-80\r' \
    --prompt '>' --type 'MOUNT SY1:\r' --wait 3000 \
    --prompt '>' --type 'RUN SY1:FUJIDEV.ABS\r' \
    --until 'PRESS ANY KEY' --screen > "${WORK}/run.log" 2>&1 || true

sed -n '/---- screen ----/,/^\(\[fujinet\] \)\?----------------/p' "${WORK}/run.log" | sed 's/^\[fujinet\] //'
grep -q 'RESULTS:\|TESTS PASSED' "${WORK}/run.log" || { echo "FUJIDEV did not finish" >&2; exit 1; }
