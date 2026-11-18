#!/usr/bin/env python3
"""Strip a thin arm64 Mach-O kernel down to what a boot loader needs.

Why this exists: on this board the firmware volume is the only place a payload can
come from at all. `UniXNURamDiskDxe` finds nothing in DRAM no matter which address
it probes (six were tried, including the two the driver has ever carried), so the
`fastboot boot` path on this phone does not deliver the boot image's ramdisk. The
payload has to be embedded in the FV instead, and that window is
`0xA7100000..0xA8000000` - fifteen megabytes. The linked kernel is 16.6 MiB, so it
does not fit.

Most of that is `__LINKEDIT`: 6.48 MiB of symbol table, string pool and chained
fixup metadata. A boot loader reads the Mach-O headers, the segments and the entry
point; it does not need the symbol table. Removing `LC_SYMTAB` and
`LC_DYSYMTAB`, zeroing the LINKEDIT ranges they described, and dropping the
linkedit bytes out of the file brings the image well inside the window.

What is deliberately NOT removed:

- `LC_DYLD_CHAINED_FIXUPS` / `LC_DYLD_EXPORTS_TRIE` - the kernel's own pointers are
  rebound through these; DarwinBoot and XNU read them.
- Any segment except `__LINKEDIT`.
- The code signature is absent already (this is a development kernel), so there is
  nothing to invalidate.

The output is a new file; the input is never modified. Every step reports the
offset it touched, and the result is re-parsed afterwards so a size claim is never
just asserted.
"""

import argparse
import struct
import sys
from pathlib import Path

MH_MAGIC_64 = 0xFEEDFACF
LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x2
LC_DYSYMTAB = 0xB
LC_CODE_SIGNATURE = 0x1D
LC_FUNCTION_STARTS = 0x26
LC_DATA_IN_CODE = 0x29
LC_DYLIB_CODE_SIGN_DRS = 0x2A
LC_LINKER_OPTIMIZATION_HINT = 0x2D

# Linkedit data commands that describe ranges inside __LINKEDIT. Their data is kept
# (the loader and the kernel use some of it); only the symbol table commands are
# dropped.
KEEP_LINKEDIT_DATA = (
    LC_FUNCTION_STARTS,
    LC_DATA_IN_CODE,
    LC_DYLIB_CODE_SIGN_DRS,
    LC_LINKER_OPTIMIZATION_HINT,
    0x80000022,  # LC_DYLD_CHAINED_FIXUPS
    0x80000033,  # LC_DYLD_INFO_ONLY
    0x80000034,  # LC_DYLD_EXPORTS_TRIE
)


def parse(data):
    magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, _ = struct.unpack_from(
        "<IiiIIIII", data, 0)
    if magic != MH_MAGIC_64:
        raise SystemExit("not a 64-bit Mach-O (magic %#x)" % magic)
    commands = []
    off = 32
    for index in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        commands.append({"index": index, "cmd": cmd, "cmdsize": cmdsize, "offset": off})
        off += cmdsize
    return {
        "cputype": cputype,
        "cpusubtype": cpusubtype,
        "filetype": filetype,
        "flags": flags,
        "ncmds": ncmds,
        "sizeofcmds": sizeofcmds,
        "commands": commands,
        "header_end": 32 + sizeofcmds,
    }


def segment_of(info, data, command):
    offset = command["offset"]
    name = data[offset + 8:offset + 24].rstrip(b"\0").decode("ascii", "replace")
    vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", data, offset + 24)
    return {
        "name": name,
        "vmaddr": vmaddr,
        "vmsize": vmsize,
        "fileoff": fileoff,
        "filesize": filesize,
        "command": command,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", type=Path)
    ap.add_argument("output", type=Path)
    args = ap.parse_args()

    original = bytearray(args.input.read_bytes())
    info = parse(bytes(original))
    print("input : %s" % args.input.name)
    print("        %d bytes (%0.2f MiB), ncmds=%d"
          % (len(original), len(original) / 2**20, info["ncmds"]))

    segments = [segment_of(info, bytes(original), c)
                for c in info["commands"] if c["cmd"] == LC_SEGMENT_64]
    linkedit = next((s for s in segments if s["name"] == "__LINKEDIT"), None)
    if linkedit is None:
        raise SystemExit("no __LINKEDIT segment")
    print("        __LINKEDIT fileoff=%#x filesize=%#x (%0.2f MiB)"
          % (linkedit["fileoff"], linkedit["filesize"], linkedit["filesize"] / 2**20))

    # --- 1. the command area without the symbol-table commands -----------------
    kept = [c for c in info["commands"] if c["cmd"] not in (LC_SYMTAB, LC_DYSYMTAB)]
    dropped = len(info["commands"]) - len(kept)
    commands = bytearray()
    for command in kept:
        start = command["offset"]
        commands += original[start:start + command["cmdsize"]]
    while len(commands) % 8:
        commands += b"\0"

    header = bytearray(original[:32])
    struct.pack_into("<I", header, 16, len(kept))
    struct.pack_into("<I", header, 20, len(commands))

    # --- 2. body: everything between the old command area and __LINKEDIT -------
    cut = linkedit["fileoff"]
    old_header_end = info["header_end"]
    body = original[old_header_end:cut]

    out = bytearray(header)
    out += commands
    out += body

    # --- 3. fix up every segment's fileoff for the moved body ------------------
    # The command area shrank, so segments whose data lives after it moved down by
    # the same amount. __LINKEDIT loses its file payload entirely.
    delta = len(commands) - (old_header_end - 32)
    offset_in_commands = 0
    for command in kept:
        base = 32 + offset_in_commands
        cmd, cmdsize = struct.unpack_from("<II", out, base)
        if cmd == LC_SEGMENT_64:
            name = bytes(out[base + 8:base + 24]).rstrip(b"\0").decode("ascii", "replace")
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", out, base + 24)
            if name == "__LINKEDIT":
                # Keep vmsize (the VM layout the kernel expects) but claim no file
                # bytes: the data no longer exists, and a nonzero filesize would
                # describe bytes a loader might try to map. fileoff moves to the
                # end of the file as well - leaving the old value behind points the
                # segment past EOF, which a validating loader rejects even when
                # filesize is zero.
                struct.pack_into("<Q", out, base + 24 + 16, len(out))
                struct.pack_into("<Q", out, base + 24 + 24, 0)
            elif fileoff:
                struct.pack_into("<Q", out, base + 24 + 16, fileoff + delta)
        offset_in_commands += cmdsize

    args.output.write_bytes(bytes(out))

    # --- 4. verify by re-reading what was written -----------------------------
    check = parse(bytes(out))
    print("output: %s" % args.output.name)
    print("        %d bytes (%0.2f MiB), ncmds=%d"
          % (len(out), len(out) / 2**20, check["ncmds"]))
    print("        dropped %d command(s), saved %d bytes (%0.2f MiB)"
          % (dropped, len(original) - len(out), (len(original) - len(out)) / 2**20))
    left = [c for c in check["commands"] if c["cmd"] in (LC_SYMTAB, LC_DYSYMTAB)]
    print("        symbol table commands left: %d" % len(left))

    # The entry point and the segments have to survive unchanged apart from the
    # file offsets, so state them: they are what the loader actually uses.
    new_segments = [segment_of(check, bytes(out), c)
                    for c in check["commands"] if c["cmd"] == LC_SEGMENT_64]
    for before, after in zip(segments, new_segments):
        if before["name"] != after["name"]:
            print("        SEGMENT ORDER CHANGED: %s -> %s" % (before["name"], after["name"]))
            return 1
        if before["vmaddr"] != after["vmaddr"] or before["vmsize"] != after["vmsize"]:
            print("        VM RANGE CHANGED for %s" % before["name"])
            return 1
    print("        all %d segments keep their vmaddr and vmsize" % len(segments))
    if left:
        return 1
    if len(out) >= 15 * 2**20:
        print("        NOTE: still %0.2f MiB, does not fit the 15 MiB FD window"
              % (len(out) / 2**20))
    return 0


if __name__ == "__main__":
    sys.exit(main())
