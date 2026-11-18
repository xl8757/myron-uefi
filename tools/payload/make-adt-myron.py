#!/usr/bin/env python3
"""Generate the Apple device tree for myron from the hardware DTS.

Why this exists instead of reusing ios-sweet's mkadt.py: that tree was written for
xnu-8796 and a generated ADT from it carries a 64-byte /chosen/random-seed. xnu-12377
reads four 64-byte seeds on ARM and panics with fewer - UniXNU's devicetree.py says so
in as many words - and the kernel died silently before any console existed, which is
exactly what that panic looks like from outside.

UniXNU generates its ADT from the compiled hardware device tree plus the OS profile,
so this does the same thing with the inputs this repository has:

  dts/myron.dts          the full board tree (compiled to DTB by the caller)
  os-profiles/...        the OS profile merged with board.toml
  board.toml             [payload] and [payload.adt]

It is deliberately a thin wrapper: the tree itself comes from UniXNU's devicetree.py,
which is vendored under tools/unixnu/ so the ADT format tracks the kernel it targets.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from unixnu import devicetree  # noqa: E402
from unixnu.profiles import merge  # noqa: E402
import adt  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dtb", required=True, type=Path,
                    help="compiled hardware device tree (dtc -I dts -O dtb)")
    ap.add_argument("--board", required=True, type=Path, help="board.toml")
    ap.add_argument("--os-profile", required=True, type=Path, help="profile.toml")
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--unique-chip-id",
                    help="hex seed for /chosen/unique-chip-id; required when the board asks for it")
    args = ap.parse_args()

    import tomllib
    config = merge(tomllib.loads(args.os_profile.read_text(encoding="utf-8")),
                   tomllib.loads(args.board.read_text(encoding="utf-8")))

    blob = devicetree.generate(args.dtb, config, unique_chip_id=args.unique_chip_id,
                               data=False, trust=False, addon=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)

    # The property this port has actually been bitten by: xnu-12377 reads four 64-byte
    # seeds on ARM and panics with fewer, while the ios-sweet tree it replaces carried
    # one 64-byte seed. Verify it through a real parse rather than by scanning bytes -
    # the serialised form is name[32] + size(4) + value, with the flags in the top bit
    # of size, and a naive search reads the wrong word and reports a bogus length.
    print(f"ADT: {args.output} ({len(blob)} bytes)")

    node, _ = adt.parse(blob)
    chosen = next((child for child in node.children if child.name == "chosen"), None)
    if chosen is None:
        print("  /chosen MISSING", file=sys.stderr)
        return 1
    seed = chosen.prop("random-seed")
    if seed is None:
        print("  random-seed MISSING", file=sys.stderr)
        return 1
    print(f"  random-seed: {len(seed)} bytes"
          f"  {'OK (xnu-12377 needs 256)' if len(seed) >= 256 else 'TOO SHORT'}")
    return 0 if len(seed) >= 256 else 1


if __name__ == "__main__":
    sys.exit(main())
