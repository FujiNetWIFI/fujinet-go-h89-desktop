#!/usr/bin/env bash
#
# Build the disks for trying FujiNet on the H89, which every release ships
# as fujinet-go-h89-<version>-disks.zip: fujinet-lib's H89 test programs
# (testing/r2r/h89-hdos/*.abs and testing/r2r/h89-cpm/*.COM, made by
# fujinet-lib's `make h89-cpm/r2r h89-hdos/r2r` and then
# `make -C testing h89-cpm/r2r h89-hdos/r2r`; the /r2r targets matter, as
# the plain ones leave programs that never reach the FujiNet)
# packed onto 100K H17 disks, which the default machine's H-17-1 drives
# take, and the boot disks to run them from:
#
#   fujinet-hdos-N.h8d     HDOS data disks: MOUNT SY1: then RUN SY1:FUJIDEV
#   fujinet-cpm-N.h8d      CP/M data disks: B:FUJIDEV; one also has FNON.COM,
#                          which starts the card on a real H89, and its
#                          source FNON.ASM (tools/disks/fnon/)
#   hdos-2.0.h17disk       HDOS 2.0
#   cpm-2.2.04-64k.h8d     CP/M 2.2.04 rebuilt as a 64K system; the
#                          distribution disk is a 32K one, too small for the
#                          bigger programs ("BAD LOAD")
#   README.txt             how to boot them, which program is on which
#                          disk, and where everything comes from
#   LICENSE.fujinet-lib    the test programs' licence
#
# The boot disks are the repository's (tools/disks/boot/, COMPLIANCE.md).
# --rebuild-cpm-boot remakes the 64K CP/M one from SEBHC's distribution
# disk (tools/disks/fetch-disks.sh) the way a user would -- CONFIGUR, then
# MOVCPM17 64 * and SYSGEN -- which needs a build
# (build/core/h89_session_run) and the Heath ROMs, so CI cannot.
# --rebuild-fnon remakes tools/disks/fnon/FNON.COM from FNON.ASM with that
# disk's own ASM and LOAD, under the same conditions.
#
# Needs z88dk's appmake (on PATH, or $APPMAKE) and python3. The disks are
# checked with tools/disks/check-fujinet-disks.py before they are written.
# Files already in --out are kept unless --force.
#
#   tools/disks/make-fujinet-disks.sh [--out DIR] [--lib DIR] [--force]
#   tools/disks/make-fujinet-disks.sh --rebuild-cpm-boot [--roms DIR] [--build DIR]
#   tools/disks/make-fujinet-disks.sh --rebuild-fnon [--roms DIR] [--build DIR]
#
# --out defaults to tools/disks/images/fujinet (gitignored); the app's own
# disks folder (Drives > Open Disks Folder) is a handy alternative.
#
# Copyright (C) 2026 Thomas Cherryhomes
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${ROOT}/tools/disks/images/fujinet"
LIB="${HOME:-}/Workspace/fujinet-lib"
ROMS="${ROOT}/tools/roms"
BUILD="${ROOT}/build"
FORCE=0
REBUILD_CPM=0
REBUILD_FNON=0
usage() {
    echo "usage: $0 [--out DIR] [--lib DIR] [--force]" >&2
    echo "       $0 --rebuild-cpm-boot [--roms DIR] [--build DIR]" >&2
    echo "       $0 --rebuild-fnon [--roms DIR] [--build DIR]" >&2
    exit 2
}
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out) OUT="$2"; shift 2 ;;
        --lib) LIB="$2"; shift 2 ;;
        --roms) ROMS="$2"; shift 2 ;;
        --build) BUILD="$2"; shift 2 ;;
        --force) FORCE=1; shift ;;
        --rebuild-cpm-boot) REBUILD_CPM=1; shift ;;
        --rebuild-fnon) REBUILD_FNON=1; shift ;;
        *) usage ;;
    esac
done

BOOT="${ROOT}/tools/disks/boot"
HDOS_BOOT="${BOOT}/hdos-2.0.h17disk"
CPM_BOOT="${BOOT}/cpm-2.2.04-64k.h8d"
FNON="${ROOT}/tools/disks/fnon"
APPMAKE="${APPMAKE:-$(command -v z88dk-appmake || echo "${HOME:-}/Workspace/z88dk/bin/z88dk-appmake")}"

# CP/M wants text as CR LF lines ended by a ^Z
cpm_text() {
    sed 's/$/\r/' "$1" > "$2"
    printf '\032' >> "$2"
}

# The 64K CP/M disk, from disk 1 of SEBHC's CP/M 2.2.04 update
if [[ ${REBUILD_CPM} -eq 1 ]]; then
    CPM_DIST="${ROOT}/tools/disks/images/CPM_2_2_update_1.h8d"
    RUN="${BUILD}/core/h89_session_run"
    for f in "${CPM_DIST}" "${RUN}"; do
        [[ -e "$f" ]] || { echo "missing: $f (fetch-disks.sh, a build)" >&2; exit 1; }
    done
    WORK="$(mktemp -d "${TMPDIR:-/tmp}/h89-cpmboot-XXXXXX")"
    trap 'rm -rf "${WORK}"' EXIT
    cp "${CPM_DIST}" "${WORK}/cpm.h8d"
    "${RUN}" --roms "${ROMS}" --home "${WORK}/home" --timeout 120 \
        --disk "h17:1=${WORK}/cpm.h8d" \
        --until 'H:' --type 'B\r' \
        --prompt 'STANDARD SYSTEM (Y OR N)? <Y>:' --type '\r' \
        --prompt 'A>' --type 'MOVCPM17 64 *\r' --wait 15000 \
        --prompt 'A>' --type 'SYSGEN\r' \
        --prompt 'SOURCE DRIVE NAME (OR RETURN TO SKIP):' --type '\r' \
        --prompt 'DESTINATION DRIVE NAME (OR RETURN TO REBOOT):' --type 'A' \
        --prompt 'DESTINATION ON A, THEN TYPE RETURN' --type '\r' \
        --until 'FUNCTION COMPLETE' > "${WORK}/sysgen.log" 2>&1 \
        || { tail -5 "${WORK}/sysgen.log" >&2; echo "SYSGEN failed (ROMs in ${ROMS}?)" >&2; exit 1; }
    if cmp -s "${WORK}/cpm.h8d" "${CPM_BOOT}"; then
        echo "${CPM_BOOT}: unchanged"
    else
        cp "${WORK}/cpm.h8d" "${CPM_BOOT}"
        echo "wrote ${CPM_BOOT}"
    fi
    exit 0
fi

# FNON.COM, from FNON.ASM by the 64K CP/M disk's own ASM and LOAD
if [[ ${REBUILD_FNON} -eq 1 ]]; then
    RUN="${BUILD}/core/h89_session_run"
    [[ -e "${RUN}" ]] || { echo "missing: ${RUN} (a build)" >&2; exit 1; }
    [[ -x "${APPMAKE}" ]] || { echo "missing: z88dk-appmake (set APPMAKE)" >&2; exit 1; }
    WORK="$(mktemp -d "${TMPDIR:-/tmp}/h89-fnon-XXXXXX")"
    trap 'rm -rf "${WORK}"' EXIT
    cp "${CPM_BOOT}" "${WORK}/cpm.h8d"
    cpm_text "${FNON}/FNON.ASM" "${WORK}/FNON.ASM"
    # appmake starts a disk with a program, so a one-byte RET carries the source
    printf '\311' > "${WORK}/STUB.COM"
    (cd "${WORK}" && "${APPMAKE}" +cpmdisk -f hz89 --container raw --no-com-file \
        -b STUB.COM -a FNON.ASM -o src) > /dev/null
    "${RUN}" --roms "${ROMS}" --home "${WORK}/home" --timeout 120 \
        --disk "h17:1=${WORK}/cpm.h8d" --disk "h17:2=${WORK}/src" \
        --until 'H:' --type 'B\r' --until 'A>' --wait 1000 \
        --prompt 'A>' --type 'ASM B:FNON\r' --until 'END OF ASSEMBLY' --screen \
        --prompt 'A>' --type 'LOAD B:FNON\r' --until 'RECORDS WRITTEN' \
        --prompt 'A>' --wait 500 > "${WORK}/asm.log" 2>&1 \
        || { tail -5 "${WORK}/asm.log" >&2; echo "ASM or LOAD failed (ROMs in ${ROMS}?)" >&2; exit 1; }
    # ASM echoes each line it flags between its banner and END OF ASSEMBLY;
    # a clean run prints only the next address and the use factor there
    listing="$(sed -n '/CP\/M ASSEMBLER/,/END OF ASSEMBLY/p' "${WORK}/asm.log" \
        | sed 's/^\[fujinet\] //')"
    if ! grep -q 'CP/M ASSEMBLER' <<< "${listing}" \
            || grep -vE 'CP/M ASSEMBLER|^[0-9A-F]{4}$|USE FACTOR|END OF ASSEMBLY' <<< "${listing}" \
            | grep -q .; then
        echo "${listing}" >&2
        echo "ASM reported errors in FNON.ASM" >&2
        exit 1
    fi
    python3 -I - "${WORK}/src" "${WORK}/FNON.COM" <<'EOF'
import sys
# hz89: 3 boot tracks of 10 skewed 256-byte sectors, 1K blocks, the
# directory in the first two
SKEW = [0, 4, 8, 2, 6, 1, 5, 9, 3, 7]
image = open(sys.argv[1], 'rb').read()

def sector(n):
    off = ((3 + n // 10) * 10 + SKEW[n % 10]) * 256
    return image[off:off + 256]

directory = b''.join(sector(n) for n in range(8))
for e in range(0, len(directory), 32):
    entry = directory[e:e + 32]
    if entry[0] <= 15 and entry[1:12] == b'FNON    COM' and entry[15]:
        blocks = [b for b in entry[16:32] if b]
        data = b''.join(sector(4 * b + i) for b in blocks for i in range(4))
        open(sys.argv[2], 'wb').write(data[:entry[15] * 128])
        break
else:
    sys.exit('LOAD left no FNON.COM on the disk')
EOF
    if cmp -s "${WORK}/FNON.COM" "${FNON}/FNON.COM"; then
        echo "${FNON}/FNON.COM: unchanged"
    else
        cp "${WORK}/FNON.COM" "${FNON}/FNON.COM"
        echo "wrote ${FNON}/FNON.COM"
    fi
    exit 0
fi

HDOS_SRC="${LIB}/testing/r2r/h89-hdos"
CPM_SRC="${LIB}/testing/r2r/h89-cpm"
[[ -x "${APPMAKE}" ]] || { echo "missing: z88dk-appmake (set APPMAKE)" >&2; exit 1; }
shopt -s nullglob
abs_files=("${HDOS_SRC}"/*.abs)
com_files=("${CPM_SRC}"/*.COM)
shopt -u nullglob
[[ ${#abs_files[@]} -gt 0 && ${#com_files[@]} -gt 0 ]] || {
    echo "no test programs in ${LIB}/testing/r2r/h89-{hdos,cpm}" >&2
    echo "(make them in fujinet-lib: make h89-cpm/r2r h89-hdos/r2r && make -C testing h89-cpm/r2r h89-hdos/r2r)" >&2
    exit 1
}

WORK="$(mktemp -d "${TMPDIR:-/tmp}/h89-disks-XXXXXX")"
trap 'rm -rf "${WORK}"' EXIT
mkdir -p "${WORK}/out" "${OUT}"

# What the CP/M disks hold: the test programs and FNON
CPM_FILES="${WORK}/cpm-files"
mkdir -p "${CPM_FILES}"
cp "${com_files[@]}" "${FNON}/FNON.COM" "${CPM_FILES}/"
cpm_text "${FNON}/FNON.ASM" "${CPM_FILES}/FNON.ASM"

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
done < <(by_size "${CPM_FILES}"/*)
for i in "${!cpm_lists[@]}"; do
    n=$(( i + 1 ))
    dir="${WORK}/cpm${n}"
    mkdir -p "${dir}"
    # -b names its file .COM, so a program starts the disk and the rest are -a
    bin=""
    adds=()
    for f in ${cpm_lists[$i]}; do
        cp "${CPM_FILES}/${f}" "${dir}/"
        if [[ -z "${bin}" && "${f}" == *.COM ]]; then bin="${f}"; else adds+=(-a "${f}"); fi
    done
    (cd "${dir}" && "${APPMAKE}" +cpmdisk -f hz89 --container raw --no-com-file \
        -b "${bin}" "${adds[@]}" -o disk) > /dev/null
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


cp "${HDOS_BOOT}" "${CPM_BOOT}" "${WORK}/out/"
[[ -f "${LIB}/LICENSE" ]] && cp "${LIB}/LICENSE" "${WORK}/out/LICENSE.fujinet-lib"

lib_url="$(git -C "${LIB}" remote get-url origin 2>/dev/null \
    | sed -E 's#^git@github.com:#https://github.com/#; s#\.git$##' || true)"
lib_url="${lib_url:-https://github.com/FujiNetWIFI/fujinet-lib}"
lib_commit="$(git -C "${LIB}" rev-parse HEAD 2>/dev/null || echo unknown)"
if [[ -n "$(git -C "${LIB}" status --porcelain --untracked-files=no 2>/dev/null)" ]]; then
    lib_commit="${lib_commit}, with local changes"
fi
listing() {
    local kind=$1; shift
    local i
    local -n lists=$1
    for i in "${!lists[@]}"; do
        printf '  %-22s %s\n' "fujinet-${kind}-$(( i + 1 )).h8d" "${lists[$i]# }"
    done
}
cat > "${WORK}/out/README.txt" <<EOF
Disks for trying FujiNet on FujiNet Go H89
==========================================

All of them are 100K H17 disks (one side, 40 tracks), which the default
machine's three H-17-1 drives take. Put a boot disk in drive 1 and a data
disk in drive 2 or 3 (the Drives menu, or drag one onto the window). The
programs are fujinet-lib's integration tests: each talks to the FujiNet
built into the app, prints PASS or FAIL for every check, and ends with a
count.

HDOS 2.0
  hdos-2.0.h17disk       boot: B and RETURN at the H: prompt, SPACE twice,
                         RETURN at ACTION? <BOOT>, then a date with an 80s
                         year, such as 11-OCT-80
$(listing hdos hdos_lists)
  then:                  MOUNT SY1:
                         RUN SY1:FUJIDEV.ABS

CP/M 2.2.04
  cpm-2.2.04-64k.h8d     boot: B and RETURN at the H: prompt
$(listing cpm cpm_lists)
  then:                  B:FUJIDEV
  FNON.COM               starts the FujiNet card on a real H89, which holds
                         it in reset from power-on (the programs start it
                         too); FNON.ASM is its source, for ASM and LOAD

Writes are saved to the disk files, so keep these copies and use copies of
them if you want to start again later.

Where they come from
  The test programs   fujinet-lib, ${lib_url}
                      commit ${lib_commit}
                      (the source is that commit's testing/ folder), built
                      with z88dk. GPL-3.0 (LICENSE.fujinet-lib).
  HDOS 2.0            Heath's operating system, from Mark Garlanger's
                      capture of the distribution disk:
                      https://heathkit.garlanger.com/software/OSes/HDOS/2.0/
                      ("This software was placed in the public domain.")
  CP/M 2.2.04         Heath/Zenith's CP/M 2.2 for the H17 (Digital Research's
                      CP/M with Heath's BIOS and utilities), disk 1 of
                      CPM_2_2_04_Update.zip from the Society of Eight-Bit
                      Heath Computerists' archive,
                      https://sebhc.github.io/sebhc/software.html,
                      configured as a standard system and rebuilt for 64K
                      with MOVCPM17 64 * and SYSGEN. No licence is stated
                      for it.
  FNON                this project's tools/disks/fnon/, assembled with
                      that CP/M's ASM and LOAD. GPL-3.0-or-later.

Made by tools/disks/make-fujinet-disks.sh in
https://github.com/tschak909/fujinet-go-h89-desktop
EOF

python3 -I "${ROOT}/tools/disks/check-fujinet-disks.py" "${WORK}/out" "${HDOS_SRC}" "${CPM_FILES}"

for f in "${WORK}/out"/*; do
    dest="${OUT}/$(basename "$f")"
    if [[ -e "${dest}" && ${FORCE} -eq 0 ]] && ! cmp -s "$f" "${dest}"; then
        echo "keeping ${dest} (--force replaces it)"
        continue
    fi
    cp "$f" "${dest}"
    echo "wrote ${dest}"
done
