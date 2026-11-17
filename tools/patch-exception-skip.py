#!/usr/bin/env python3
"""Make a synchronous exception skip the faulting instruction instead of halting.

Mu's AArch64 DXE handler ends with

    LogToConsole (UnicodeBuffer);
    ASSERT (FALSE);
    CpuDeadLoop ();

so the first abort in any vendor driver parks the platform. The register dump
and stack trace are printed before that, which is genuinely useful, but the boot
then stops, and a single bad MMIO read means the rest of the platform is never
exercised and the log ends there.

This turns the halt into a skipped instruction:

    LogToConsole (UnicodeBuffer);

    // SKIP ON FAULT: log it, step over the instruction, keep the platform running.
    ASSERT (SystemContext.SystemContextAArch64->ELR != 0);
    SystemContext.SystemContextAArch64->ELR += 4;

    return;

Advancing ELR by 4 is what makes this terminate. The handler runs on the
exception's own context, and AArch64's exception return restores ELR into PC, so
without the increment the faulting load would simply fault again forever. AArch64
instructions are fixed 4-byte encodings, so +4 is the next instruction for every
case that matters here.

What this is and is not:

- It is a bring-up aid. A driver that took the abort continues with whatever
  state it had; nothing is repaired. The point is that one boot produces the
  full register dump *and* everything the platform does afterwards, so the log
  says which drivers survive.
- It is not a correctness fix, and it must not be carried into anything claiming
  to be a working system. The patch prints a banner on every occurrence so a log
  cannot be mistaken for a clean run.

The anchors are checked for exact, single occurrence. A Mu update that changes
the handler fails the patch instead of silently instrumenting nothing.
"""

from pathlib import Path
import argparse
import re
import sys

# The halt site, matched without its trailing brace so the same literal works
# whether the file uses LF or CRLF; the newline style is preserved by
# normalising first. Two ASSERT (FALSE) calls exist in this file - one in the
# "unknown exception type" branch, which has no CpuDeadLoop after it - so the
# anchor includes the CpuDeadLoop line, which makes it unique.
OLD_HALT_LF = "ASSERT (FALSE);\n  CpuDeadLoop ();"

# Replacement, expressed with LF and converted to the file's own newline style.
NEW_HALT_LF = """//
  // SKIP ON FAULT: this was ASSERT(FALSE); CpuDeadLoop();
  //
  // Everything above has already reached the console: the register dump, the
  // stack trace and the described syndrome. Halting here means one bad MMIO
  // read in one vendor driver hides everything the platform would have done
  // afterwards, which is the wrong trade during bring-up.
  //
  // ELR has to advance or the exception return re-executes the instruction that
  // faulted and faults again forever. AArch64 instructions are 4 bytes, so +4
  // is the next instruction for every case that reaches here.
  //
  DEBUG ((
    DEBUG_ERROR,
    "\\nSKIP ON FAULT: ELR 0x%lx -> 0x%lx, continuing after a synchronous exception\\n",
    SystemContext.SystemContextAArch64->ELR,
    SystemContext.SystemContextAArch64->ELR + 4
    ));
  if (SystemContext.SystemContextAArch64->ELR != 0) {
    SystemContext.SystemContextAArch64->ELR += 4;
  }

  return;"""


def patch(path: Path, check: bool) -> int:
    raw = path.read_bytes()
    text = raw.decode("utf-8")

    if "SKIP ON FAULT" in text:
        print("%s: already patched" % path.name)
        return 0

    crlf = "\r\n" in text
    old = OLD_HALT_LF.replace("\n", "\r\n") if crlf else OLD_HALT_LF
    new = NEW_HALT_LF.replace("\n", "\r\n") if crlf else NEW_HALT_LF

    count = text.count(old)
    if count != 1:
        print(
            "%s: expected exactly 1 halt site, found %d (newline=%s)"
            % (path.name, count, "CRLF" if crlf else "LF"),
            file=sys.stderr,
        )
        return 1

    if check:
        print("%s: halt site found, would patch" % path.name)
        return 0

    path.write_bytes(text.replace(old, new, 1).encode("utf-8"))
    print("%s: halt site replaced with skip-and-continue" % path.name)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mu-root", type=Path, required=True)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    target = (
        args.mu_root
        / "Mu_Basecore/UefiCpuPkg/Library/CpuExceptionHandlerLib/AArch64/CpuExceptionHandlerLib.c"
    )
    if not target.is_file():
        print("not found: %s" % target, file=sys.stderr)
        return 1

    return patch(target, args.check)


if __name__ == "__main__":
    sys.exit(main())
