#!/usr/bin/env python3
"""check_exports -- libfujinet exports its entry points and nothing else.

The FujiNet runtime is dlopen()ed into the same process as GTK or Qt, and
it links its own copies of things those toolkits also bring (expat,
mbedTLS, cJSON, mongoose ...). Any of those symbols leaking out of the
shared library could bind in place of the toolkit's. The version script
(ELF), the export list (Mach-O) and dllexport (PE) that
tools/fujinet/build-fujinet-desktop.sh patches in keep them in; this checks
that they did -- and that the fujinet_desktop_* entry points the session
dlsym()s (core/src/fujinet_runtime.c) really are there.

Pattern from fujinet-go-atari7800-desktop's check of its MAME library.

Usage: check_exports.py <library>. Exit 77 (skip) without the tools or the
library (a -DWITH_FUJINET=OFF build, or ctest before the runtime built).

Copyright (C) 2026 Thomas Cherryhomes
SPDX-License-Identifier: GPL-3.0-or-later
"""
import os
import re
import shutil
import subprocess
import sys

# The desktop entry wrapper's own exports. fujinet_android_submit_sam_audio
# is the SAM speech hook the firmware's PC build calls by its Android name.
ALLOWED = re.compile(r"^_?fujinet_(desktop|android)_\w+$")
REQUIRED = ("fujinet_desktop_start_runtime", "fujinet_desktop_stop_runtime",
            "fujinet_desktop_last_error_message")


def elf(lib):
    nm = shutil.which("nm")
    if not nm:
        return None
    out = subprocess.run([nm, "-D", "--defined-only", lib],
                         capture_output=True, text=True, check=True).stdout
    syms = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1] in "TtDdBbRrVvWwiu":
            syms.append(parts[2].split("@")[0])
    # a version script's node names show up as absolute symbols
    return [s for s in syms if not re.match(r"^[A-Z0-9_]+$", s)]


def macho(lib):
    nm = shutil.which("nm")
    if not nm:
        return None
    out = subprocess.run([nm, "-gU", lib], capture_output=True, text=True,
                         check=True).stdout
    return [line.split()[-1] for line in out.splitlines() if line.strip()]


def pe(lib):
    tool = shutil.which("objdump") or shutil.which("x86_64-w64-mingw32-objdump")
    if not tool:
        return None
    out = subprocess.run([tool, "-p", lib], capture_output=True, text=True,
                         check=True).stdout
    syms, inside = [], False
    for line in out.splitlines():
        if "[Ordinal/Name Pointer] Table" in line:
            inside = True
            continue
        if inside:
            # "[   0] +base[   1]  0000 fujinet_desktop_start_runtime"
            m = re.match(r"\s*\[\s*\d+\]\s+(?:\+base\[\s*\d+\]\s+[0-9a-fA-F]+\s+)?(\S+)", line)
            if m:
                syms.append(m.group(1))
            elif line.strip() == "":
                if syms:
                    break
    return syms


def main():
    if len(sys.argv) != 2:
        print("usage: check_exports.py <library>")
        return 2
    lib = sys.argv[1]
    if not os.path.exists(lib):
        print(f"SKIP: {lib} has not been built")
        return 77
    if lib.endswith(".dll"):
        syms = pe(lib)
    elif lib.endswith(".dylib"):
        syms = macho(lib)
    else:
        syms = elf(lib)
    if syms is None:
        print("SKIP: no nm/objdump")
        return 77
    names = {s.lstrip("_") if lib.endswith(".dylib") else s for s in syms}
    bad = sorted(s for s in syms if not ALLOWED.match(s))
    good = sorted(s for s in syms if ALLOWED.match(s))
    print(f"{len(good)} fujinet_* exports: {' '.join(good)}")
    missing = [r for r in REQUIRED if r not in names]
    if missing:
        print(f"FAIL: missing {' '.join(missing)}")
        return 1
    if bad:
        print(f"FAIL: {len(bad)} other symbols exported:")
        for s in bad[:50]:
            print("  " + s)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
