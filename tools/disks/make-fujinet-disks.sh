#!/usr/bin/env bash
#
# Build disks for trying FujiNet on the H89: fujinet-lib's H89 test programs
# (testing/r2r/h89-hdos/*.abs and testing/r2r/h89-cpm/*.COM, made by
# fujinet-lib's `make -C testing h89-hdos h89-cpm`) packed onto 100K H17
# disks, which the default machine's H-17-1 drives take, and the boot disks
# to run them from:
#
#   fujinet-hdos-N.h8d     HDOS data disks: MOUNT SY1: then RUN SY1:FUJIDEV
#   fujinet-cpm-N.h8d      CP/M data disks: B:FUJIDEV
#   hdos-2.0.h17disk       a copy of the HDOS 2.0 system disk
#   cpm-2.2.04-64k.h8d     CP/M 2.2.04 rebuilt as a 64K system; the
#                          distribution disk is a 32K one, too small for the
#                          bigger programs ("BAD LOAD")
#   CONTENTS.txt           which program is on which disk
#
# Needs z88dk's appmake (on PATH, or $APPMAKE) and python3. The boot disks
# come from tools/disks/fetch-disks.sh. The 64K CP/M disk is
# made the way a user would (CONFIGUR, MOVCPM17 64 *, SYSGEN), so it also
# needs a build (build/core/h89_session_run) and the Heath ROMs; without them
# it is skipped. Files already in --out are kept unless --force.
#
#   tools/disks/make-fujinet-disks.sh [--out DIR] [--lib DIR] [--roms DIR]
#                                     [--build DIR] [--force]
#
# --out defaults to tools/disks/images/fujinet (gitignored); the app's own
# disks folder (Drives > Open Disks Folder) is a handy alternative.
#
# Copyright (C) 2026 Thomas Cherryhomes
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${ROOT}/tools/disks/images/fujinet"
LIB="${HOME}/Workspace/fujinet-lib"
ROMS="${ROOT}/tools/roms"
BUILD="${ROOT}/build"
FORCE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out) OUT="$2"; shift 2 ;;
        --lib) LIB="$2"; shift 2 ;;
        --roms) ROMS="$2"; shift 2 ;;
        --build) BUILD="$2"; shift 2 ;;
        --force) FORCE=1; shift ;;
        *) echo "usage: $0 [--out DIR] [--lib DIR] [--roms DIR] [--build DIR] [--force]" >&2
           exit 2 ;;
    esac
done

HDOS_SRC="${LIB}/testing/r2r/h89-hdos"
CPM_SRC="${LIB}/testing/r2r/h89-cpm"
IMAGES="${ROOT}/tools/disks/images"
HDOS_OS="${IMAGES}/HDOS_2_0_Operating_System.h17disk"
CPM_OS="${IMAGES}/CPM_2_2_update_1.h8d"
RUN="${BUILD}/core/h89_session_run"

APPMAKE="${APPMAKE:-$(command -v z88dk-appmake || echo "${HOME}/Workspace/z88dk/bin/z88dk-appmake")}"
[[ -x "${APPMAKE}" ]] || { echo "missing: z88dk-appmake (set APPMAKE)" >&2; exit 1; }
shopt -s nullglob
abs_files=("${HDOS_SRC}"/*.abs)
com_files=("${CPM_SRC}"/*.COM)
shopt -u nullglob
[[ ${#abs_files[@]} -gt 0 || ${#com_files[@]} -gt 0 ]] || {
    echo "no test programs in ${LIB}/testing/r2r/h89-{hdos,cpm}" >&2
    echo "(make them with: make -C ${LIB}/testing h89-hdos h89-cpm)" >&2
    exit 1
}

WORK="$(mktemp -d "${TMPDIR:-/tmp}/h89-disks-XXXXXX")"
trap 'rm -rf "${WORK}"' EXIT
mkdir -p "${WORK}/out" "${OUT}"
CONTENTS="${WORK}/out/CONTENTS.txt"
{
    echo "FujiNet test disks for FujiNet Go H89, made $(date +%F) by"
    echo "tools/disks/make-fujinet-disks.sh from fujinet-lib $(git -C "${LIB}" log -1 --format='%h (%s)' 2>/dev/null || echo '(unknown)')."
    echo
} > "${CONTENTS}"

# Largest first, so first fit packs the disks well
by_size() {
    local f
    for f in "$@"; do printf '%s %s\n' "$(wc -c < "$f" | tr -d ' ')" "$f"; done | sort -rn
}

# HDOS: the program's bytes after its 8-byte ABS header (FF 00, load,
# length, entry) go back on at the same origin. appmake adds a file to an
# existing image with --image and fails when the image is full, so each
# program goes on the first disk it fits.
hdos_disks=()
hdos_lists=()
while read -r _ abs; do
    [[ -n "${abs}" ]] || continue
    name="$(basename "${abs}" .abs | tr '[:lower:]' '[:upper:]')"
    load=$(od -An -tu2 -j2 -N2 "${abs}" | tr -d ' ')
    entry=$(od -An -tu2 -j6 -N2 "${abs}" | tr -d ' ')
    [[ "${load}" == "${entry}" ]] || { echo "${abs}: entry != load; appmake can't keep it" >&2; exit 1; }
    tail -c +9 "${abs}" > "${WORK}/prog.bin"
    placed=0
    for i in "${!hdos_disks[@]}"; do
        if (cd "${WORK}" && "${APPMAKE}" +hdos -b prog.bin --org "${load}" -o "${name}" \
                --image "${hdos_disks[$i]}") > /dev/null 2>&1; then
            mv "${WORK}/prog.h8d" "${hdos_disks[$i]}"
            hdos_lists[$i]+=" ${name}.ABS"
            placed=1
            break
        fi
    done
    if [[ ${placed} -eq 0 ]]; then
        n=$(( ${#hdos_disks[@]} + 1 ))
        (cd "${WORK}" && "${APPMAKE}" +hdos -b prog.bin --org "${load}" -o "${name}") > /dev/null
        mv "${WORK}/prog.h8d" "${WORK}/out/fujinet-hdos-${n}.h8d"
        hdos_disks+=("${WORK}/out/fujinet-hdos-${n}.h8d")
        hdos_lists+=(" ${name}.ABS")
    fi
done < <(by_size "${abs_files[@]}")

# CP/M: the hz89 format (256-byte sectors, 10 a track, 40 tracks, 3 boot
# tracks, 1K blocks, 64 directory entries) leaves 90 blocks for files once
# the directory has its 2.
CPM_BLOCKS=90
cpm_lists=()
cpm_used=()
while read -r size com; do
    [[ -n "${com}" ]] || continue
    blocks=$(( (size + 1023) / 1024 ))
    placed=0
    for i in "${!cpm_lists[@]}"; do
        if (( cpm_used[i] + blocks <= CPM_BLOCKS )); then
            cpm_lists[$i]+=" $(basename "${com}")"
            cpm_used[$i]=$(( cpm_used[i] + blocks ))
            placed=1
            break
        fi
    done
    if [[ ${placed} -eq 0 ]]; then
        cpm_lists+=(" $(basename "${com}")")
        cpm_used+=("${blocks}")
    fi
done < <(by_size "${com_files[@]}")
for i in "${!cpm_lists[@]}"; do
    n=$(( i + 1 ))
    dir="${WORK}/cpm${n}"
    mkdir -p "${dir}"
    args=()
    for f in ${cpm_lists[$i]}; do
        cp "${CPM_SRC}/${f}" "${dir}/"
        if [[ ${#args[@]} -eq 0 ]]; then args+=(-b "${f}"); else args+=(-a "${f}"); fi
    done
    (cd "${dir}" && "${APPMAKE}" +cpmdisk -f hz89 --container raw --no-com-file \
        "${args[@]}" -o disk) > /dev/null
    # appmake (cpm_write_file) gives a file of exactly 16 blocks a second,
    # empty directory entry, all zeros, which DIR lists as a nameless file.
    # Mark those erased the way ERA does, E5 in the first byte only: Heath's
    # CP/M 2.2.04 ends its directory search at an entry that is all E5 (never
    # used), so a fully blanked one would hide every file after it. The
    # directory is the first 8 logical sectors after the boot tracks, in the
    # image's skewed order.
    python3 -I - "${dir}/disk" <<'EOF'
import sys
skew = [0, 4, 8, 2, 6, 1, 5, 9, 3, 7]
with open(sys.argv[1], 'r+b') as f:
    for ls in range(8):
        off = (3 * 10 + skew[ls]) * 256
        f.seek(off)
        sec = bytearray(f.read(256))
        for e in range(0, 256, 32):
            if not any(sec[e:e + 32]):
                sec[e] = 0xe5
        f.seek(off)
        f.write(sec)
EOF
    mv "${dir}/disk" "${WORK}/out/fujinet-cpm-${n}.h8d"
done

{
    for i in "${!hdos_disks[@]}"; do
        echo "fujinet-hdos-$(( i + 1 )).h8d:${hdos_lists[$i]}"
    done
    for i in "${!cpm_lists[@]}"; do
        echo "fujinet-cpm-$(( i + 1 )).h8d:${cpm_lists[$i]}"
    done
    echo
} >> "${CONTENTS}"

# Boot disks
if [[ -f "${HDOS_OS}" ]]; then
    cp "${HDOS_OS}" "${WORK}/out/hdos-2.0.h17disk"
    echo "hdos-2.0.h17disk: HDOS 2.0 (B at H:, SPACE twice, RETURN, a date with an 80s year)" >> "${CONTENTS}"
else
    echo "skipping hdos-2.0.h17disk: run tools/disks/fetch-disks.sh" >&2
fi
if [[ -f "${CPM_OS}" && -x "${RUN}" ]]; then
    cp "${CPM_OS}" "${WORK}/cpm.h8d"
    if "${RUN}" --roms "${ROMS}" --home "${WORK}/home" --timeout 120 \
        --disk "h17:1=${WORK}/cpm.h8d" \
        --until 'H:' --type 'B\r' \
        --prompt 'STANDARD SYSTEM (Y OR N)? <Y>:' --type '\r' \
        --prompt 'A>' --type 'MOVCPM17 64 *\r' --wait 15000 \
        --prompt 'A>' --type 'SYSGEN\r' \
        --prompt 'SOURCE DRIVE NAME (OR RETURN TO SKIP):' --type '\r' \
        --prompt 'DESTINATION DRIVE NAME (OR RETURN TO REBOOT):' --type 'A' \
        --prompt 'DESTINATION ON A, THEN TYPE RETURN' --type '\r' \
        --until 'FUNCTION COMPLETE' > "${WORK}/sysgen.log" 2>&1; then
        mv "${WORK}/cpm.h8d" "${WORK}/out/cpm-2.2.04-64k.h8d"
        echo "cpm-2.2.04-64k.h8d: CP/M 2.2.04, 64K (B at H:)" >> "${CONTENTS}"
    else
        echo "skipping cpm-2.2.04-64k.h8d: SYSGEN failed (ROMs in ${ROMS}?); log:" >&2
        tail -5 "${WORK}/sysgen.log" >&2
    fi
else
    echo "skipping cpm-2.2.04-64k.h8d: needs ${CPM_OS} (fetch-disks.sh) and ${RUN}" >&2
fi

for f in "${WORK}/out"/*; do
    dest="${OUT}/$(basename "$f")"
    if [[ -e "${dest}" && ${FORCE} -eq 0 ]] && ! cmp -s "$f" "${dest}"; then
        echo "keeping ${dest} (--force replaces it)"
        continue
    fi
    cp "$f" "${dest}"
    echo "wrote ${dest}"
done
