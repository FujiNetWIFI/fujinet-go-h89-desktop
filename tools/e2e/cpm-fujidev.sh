#!/usr/bin/env bash
# End to end: CP/M 2.2.04 on the emulated H89's H17 runs fujinet-lib's
# FUJIDEV against the in-process FujiNet, through the session
# (build/core/h89_session_run). The distribution disk is a 32K system, too
# small for FUJIDEV, so the first run builds a 64K one the way a user would
# (Heath's CONFIGUR on first boot, then MOVCPM17 64 * and SYSGEN to the boot
# tracks) -- which also exercises H17 writes.
#
# Needs: a build with the FujiNet runtime (build/), the Heath ROMs
# (tools/roms or --roms), CP/M 2.2.04 (tools/disks/fetch-disks.sh),
# fujinet-lib's CP/M test program (testing/r2r/h89-cpm/FUJIDEV.COM) and
# z88dk's appmake, which puts it on a CP/M data disk (on PATH, or $APPMAKE).
#
#   tools/e2e/cpm-fujidev.sh [--com PATH] [--roms DIR] [--build DIR]
#
# Copyright (C) 2026 Thomas Cherryhomes
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COM="${HOME}/Workspace/fujinet-lib/testing/r2r/h89-cpm/FUJIDEV.COM"
ROMS="${ROOT}/tools/roms"
BUILD="${ROOT}/build"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --com) COM="$2"; shift 2 ;;
        --roms) ROMS="$2"; shift 2 ;;
        --build) BUILD="$2"; shift 2 ;;
        *) echo "usage: $0 [--com PATH] [--roms DIR] [--build DIR]" >&2; exit 2 ;;
    esac
done

OS_DISK="${ROOT}/tools/disks/images/CPM_2_2_update_1.h8d"
RUN="${BUILD}/core/h89_session_run"
for f in "${OS_DISK}" "${COM}" "${RUN}"; do
    [[ -e "$f" ]] || { echo "missing: $f" >&2; exit 77; }
done
APPMAKE="${APPMAKE:-$(command -v z88dk-appmake || echo "${HOME}/Workspace/z88dk/bin/z88dk-appmake")}"
[[ -x "${APPMAKE}" ]] || { echo "missing: z88dk-appmake (set APPMAKE)" >&2; exit 77; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/h89-e2e-XXXXXX")"
trap 'rm -rf "${WORK}"' EXIT

# FUJIDEV.COM on a fresh H17 CP/M disk (hz89: 256-byte sectors, 10 a track)
cp "${COM}" "${WORK}/FUJIDEV.COM"
(cd "${WORK}" && "${APPMAKE}" +cpmdisk -f hz89 --container raw -b FUJIDEV.COM -o data >/dev/null)
mv "${WORK}/data" "${WORK}/data.h8d"
cp "${OS_DISK}" "${WORK}/cpm.h8d"

# the 64K system
"${RUN}" --roms "${ROMS}" --home "${WORK}/home1" --timeout 120 \
    --disk "h17:1=${WORK}/cpm.h8d" \
    --until 'H:' --type 'B\r' \
    --prompt 'STANDARD SYSTEM (Y OR N)? <Y>:' --type '\r' \
    --prompt 'A>' --type 'MOVCPM17 64 *\r' --wait 15000 \
    --prompt 'A>' --type 'SYSGEN\r' \
    --prompt 'SOURCE DRIVE NAME (OR RETURN TO SKIP):' --type '\r' \
    --prompt 'DESTINATION DRIVE NAME (OR RETURN TO REBOOT):' --type 'A' \
    --prompt 'DESTINATION ON A, THEN TYPE RETURN' --type '\r' \
    --until 'FUNCTION COMPLETE' > "${WORK}/sysgen.log" 2>&1 \
    || { cat "${WORK}/sysgen.log" >&2; echo "SYSGEN failed" >&2; exit 1; }

# a fresh boot of it, and FUJIDEV
"${RUN}" --roms "${ROMS}" --home "${WORK}/home2" --timeout 300 \
    --disk "h17:1=${WORK}/cpm.h8d" --disk "h17:2=${WORK}/data.h8d" \
    --until 'H:' --type 'B\r' --until 'A>' --wait 1000 \
    --prompt 'A>' --type 'B:FUJIDEV\r' \
    --until 'Press any key' --screen > "${WORK}/run.log" 2>&1 || true

sed -n '/---- screen ----/,/^\(\[fujinet\] \)\?----------------/p' "${WORK}/run.log" | sed 's/^\[fujinet\] //'
grep -q 'Results:' "${WORK}/run.log" || { echo "FUJIDEV did not finish" >&2; exit 1; }
