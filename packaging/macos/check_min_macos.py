#!/usr/bin/env python3
"""Fails when any Mach-O inside the .app requires a newer macOS than the given minimum (default 11.0).

Usage: check_min_macos.py Psi.app [11.0]
"""
import os
import re
import subprocess
import sys


def min_version(path: str) -> str | None:
    try:
        out = subprocess.run(["otool", "-l", path], capture_output=True, text=True, check=True).stdout
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    m = re.search(r"LC_BUILD_VERSION.*?minos (\d+\.\d+)", out, re.S) or re.search(r"LC_VERSION_MIN_MACOSX.*?version (\d+\.\d+)", out, re.S)
    return m.group(1) if m else None


def is_macho(path: str) -> bool:
    try:
        with open(path, "rb") as f:
            magic = f.read(4)
    except OSError:
        return False
    return magic in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")


def main() -> int:
    app = sys.argv[1]
    limit = tuple(int(x) for x in (sys.argv[2] if len(sys.argv) > 2 else "11.0").split("."))
    bad = []
    checked = 0
    for root, _, files in os.walk(app):
        for name in files:
            p = os.path.join(root, name)
            if os.path.islink(p) or not is_macho(p):
                continue
            v = min_version(p)
            checked += 1
            if v and tuple(int(x) for x in v.split(".")) > limit:
                bad.append((p, v))
    print(f"checked {checked} Mach-O files")
    for p, v in bad:
        print(f"  requires macOS {v}: {p}")
    if bad:
        print("FAIL: some binaries need a newer macOS than", ".".join(map(str, limit)))
        return 1
    print("OK: everything runs on macOS", ".".join(map(str, limit)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
