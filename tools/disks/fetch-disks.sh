#!/usr/bin/env bash
#
# Fetch the disk images the end-to-end tests boot (core/tests/hdos_boot_test.c
# and the local FujiNet runs in the README) into tools/disks/images/, which
# is gitignored: no operating system disk is ever committed (COMPLIANCE.md).
#
# The HDOS 2.0 distribution disks are Mark Garlanger's captures, published
# at heathkit.garlanger.com and placed in the public domain. They are in the
# H17Disk draft format: every sector as it was read off the disk. CP/M
# 2.2.04 (H17) and CP/M 2.2.0X with BIOS-80 (Z-89-37) come from the Society
# of Eight-Bit Heath Computerists' archive, as zips, which are unpacked
# beside them.
#
# Each download is checked against disks.lst (sha256).
#
# Copyright (C) 2026 Thomas Cherryhomes
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
OUT="${HERE}/images"
mkdir -p "${OUT}"

sha() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
    else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

while read -r sum name url; do
    [[ -z "${sum}" || "${sum}" == \#* ]] && continue
    dest="${OUT}/${name}"
    if [[ -f "${dest}" && "$(sha "${dest}")" == "${sum}" ]]; then
        echo "have ${name}"
        [[ "${name}" == *.zip ]] && unzip -oq "${dest}" -d "${OUT}"
        continue
    fi
    echo "fetching ${name}"
    curl -sSfL -o "${dest}.part" "${url}"
    got=$(sha "${dest}.part")
    if [[ "${got}" != "${sum}" ]]; then
        rm -f "${dest}.part"
        echo "error: ${name}: sha256 ${got}, expected ${sum}" >&2
        exit 1
    fi
    mv "${dest}.part" "${dest}"
    [[ "${name}" == *.zip ]] && unzip -oq "${dest}" -d "${OUT}"
done < "${HERE}/disks.lst"
