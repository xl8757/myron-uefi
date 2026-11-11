#!/usr/bin/env python3
"""Pin the Android boot image geometry that this board's ABL accepts.

Mu's mkbootimg.py defaults to base 0x10000000, tags_offset 0x100 and a 2048-byte
page. The images that reach the firmware on this phone were built with base 0,
kernel_offset 0x8000, tags_offset 0x1e00000 and 4096-byte pages, which is also
what UniXNU's bootimg step emits. build_uefi.py only forwards the keys listed in
its own argument map, so page size cannot come from the device toml; the pinned
script defaults are the only place to set it.

Every replacement is exact and must match once, so a Mu update that rewords the
parser fails here instead of silently building a differently shaped image.
"""

from __future__ import annotations

import argparse
from pathlib import Path

REPLACEMENTS = [
    (
        "parser.add_argument('--base', type=parse_int, default=0x10000000,",
        "parser.add_argument('--base', type=parse_int, default=0,",
        "base",
    ),
    (
        "parser.add_argument('--tags_offset', type=parse_int, default=0x00000100,",
        "parser.add_argument('--tags_offset', type=parse_int, default=0x01e00000,",
        "tags_offset",
    ),
    (
        "choices=[2**i for i in range(11, 15)], default=2048,",
        "choices=[2**i for i in range(11, 15)], default=4096,",
        "pagesize",
    ),
    (
        "parser.add_argument('--kernel_offset', type=parse_int, default=0x00008000,",
        "parser.add_argument('--kernel_offset', type=parse_int, default=0x00008000,",
        "kernel_offset",  # already correct; kept as a guard that the line still reads this way
    ),
]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mu-root", type=Path, required=True)
    args = ap.parse_args()

    path = args.mu_root / "Resources/Scripts/mkbootimg.py"
    source = path.read_text()
    for before, after, label in REPLACEMENTS:
        count = source.count(before)
        if count != 1:
            raise SystemExit(f"mkbootimg.py: {label} anchor matched {count} times, expected 1")
        source = source.replace(before, after, 1)

    path.write_text(source)

    check = path.read_text()
    for needle, label in (
        ("default=4096,", "pagesize 4096"),
        ("'--base', type=parse_int, default=0,", "base 0"),
        ("default=0x01e00000,", "tags_offset 0x1e00000"),
    ):
        if needle not in check:
            raise SystemExit(f"mkbootimg.py: {label} did not stick")
    print("mkbootimg.py: base 0, kernel_offset 0x8000, tags_offset 0x1e00000, pagesize 4096")


if __name__ == "__main__":
    main()
