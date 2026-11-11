#!/usr/bin/env python3
"""Check the two artifacts the firmware build must produce.

Neither check can be done from the source tree alone, so both run after the
build:

  * BootShim.bin must contain the handoff corrections this port adds. A size
    comparison is not enough - the stock shim and this one differ by only a few
    instructions - so the instruction encodings are matched directly.
  * boot.img must be an Android boot image whose kernel really starts with that
    shim and whose declared kernel size matches the file, otherwise the ABL
    copies the wrong number of bytes into the firmware volume.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

# AArch64 encodings, little-endian on disk.
MSR_DAIFSET_F = struct.pack("<I", 0xD5034FDF)   # msr daifset, #0xf
IC_IALLU = struct.pack("<I", 0xD508751F)        # ic iallu
DSB_SY = struct.pack("<I", 0xD5033F9F)          # dsb sy

FD_BASE = 0xA7100000
FD_SIZE = 0x300000
PAGE = 4096


def fail(message: str) -> None:
    raise SystemExit("artifacts: " + message)


def check_shim(path: Path) -> None:
    data = path.read_bytes()
    if not 64 <= len(data) <= 4096:
        fail(f"{path.name} is {len(data)} bytes; expected a small raw image")
    for name, pattern in (("msr daifset", MSR_DAIFSET_F), ("ic iallu", IC_IALLU), ("dsb sy", DSB_SY)):
        if pattern not in data:
            fail(f"{path.name} does not contain {name}; the port's BootShim was not used")
    # ARM64 image header: text_offset and image_size are fixed at 8 and 16.
    # The magic sits at 56 in the upstream Linux header, but Mu's BootShim
    # emits one reserved quad fewer, so accept it anywhere in the header.
    if b"ARM\x64" not in data[:64]:
        fail(f"{path.name} has no ARM64 image magic in its header")
    text_offset, image_size = struct.unpack_from("<QQ", data, 8)
    if text_offset != FD_BASE:
        fail(f"{path.name} text_offset is {text_offset:#x}, expected {FD_BASE:#x}")
    if image_size < FD_SIZE:
        fail(f"{path.name} image_size {image_size:#x} is smaller than one firmware volume")
    print(f"shim: {len(data)} bytes, text_offset={text_offset:#x}, image_size={image_size:#x}")


def check_boot_img(path: Path) -> None:
    data = path.read_bytes()
    if data[:8] != b"ANDROID!":
        fail(f"{path.name} is not an Android boot image")
    kernel_size, kernel_addr, ramdisk_size, _, _, _, _, page_size = struct.unpack_from("<8I", data, 8)
    header_version = struct.unpack_from("<I", data, 1640)[0]
    if header_version != 0:
        fail(f"header_version is {header_version}; this board needs v0 or the ABL ignores the payload")
    if page_size != PAGE:
        fail(f"page size {page_size} is not {PAGE}")
    if kernel_addr != 0x8000:
        fail(f"kernel_addr is {kernel_addr:#x}; the tested images use base 0 + kernel_offset 0x8000")

    start = page_size
    kernel = data[start:start + kernel_size]
    if len(kernel) != kernel_size:
        fail("kernel section is truncated")

    # kernel = gzip(BootShim + FD) + appended DTB. The DTB follows the gzip
    # member, so decompress exactly one member and keep the remainder.
    try:
        engine = zlib.decompressobj(31)
        body = engine.decompress(kernel) + engine.flush()
        trailing = engine.unused_data
    except zlib.error as exc:
        fail(f"kernel does not start with a gzip stream ({exc})")

    if b"ARM\x64" not in body[:64]:
        fail("the decompressed kernel does not start with the shim's ARM64 header")
    for name, pattern in (("msr daifset", MSR_DAIFSET_F), ("ic iallu", IC_IALLU)):
        if pattern not in body[:512]:
            fail(f"the shim inside the boot image lacks {name}")

    payload = len(body)  # shim + firmware volume
    if trailing:
        if trailing[:4] != b"\xd0\x0d\xfe\xed":
            fail("data after the gzip member is not a flattened device tree")
        print(f"  appended dtb: {len(trailing)} bytes")
    print(
        "boot.img: %d bytes, header v%d, kernel %d (gzip) -> %#x decompressed, "
        "ramdisk %d, kernel_addr %#x" % (len(data), header_version, kernel_size, payload, ramdisk_size, kernel_addr)
    )
    if payload < FD_SIZE:
        fail(f"decompressed payload is {payload:#x}, smaller than one firmware volume ({FD_SIZE:#x})")
    tail = data[start + kernel_size:]
    if tail:
        print(f"  trailing after kernel: {len(tail)} bytes")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--shim", type=Path, required=True)
    ap.add_argument("--img", type=Path, required=True)
    args = ap.parse_args()
    check_shim(args.shim)
    check_boot_img(args.img)
    print("PASS")


if __name__ == "__main__":
    main()
