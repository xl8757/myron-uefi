#!/usr/bin/env python3
"""Validate the myron Mu-Silicium memory map before building UEFI.

The map mixes live-FDT reserved ranges with allocations owned by UEFI/XNU.  An
overlap can corrupt still-running Qualcomm firmware before DarwinBoot starts, so
this check is intentionally strict and has no device access.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
MAP = ROOT / "Platforms/Xiaomi/myronPkg/Library/MemoryMapLib/MemoryMapLib.c"
ENTRY = re.compile(
    r'\{"(?P<name>[^"]+)",\s*'
    r'(?P<base>0x[0-9A-Fa-f]+),\s*'
    r'(?P<size>0x[0-9A-Fa-f]+),\s*'
    r'(?P<hob>\w+),\s*(?P<resource>\w+),'
)


def fail(message: str) -> None:
    raise SystemExit(f"memory-map: {message}")


text = MAP.read_text(encoding="utf-8")
entries = []
for match in ENTRY.finditer(text):
    item = match.groupdict()
    # Mu looks a region up with an exact AsciiStriCmp. Stripping the name here
    # would hide the padded names that made "Display_Reserved" unfindable, so
    # edge whitespace is a hard failure instead of a silent normalisation.
    if item["name"] != item["name"].strip():
        fail(f'{item["name"]!r} has edge whitespace; Mu uses exact AsciiStriCmp name lookup')
    item["base"] = int(item["base"], 16)
    item["size"] = int(item["size"], 16)
    entries.append(item)

if len(entries) < 70:
    fail(f"only {len(entries)} descriptors parsed; expected the live-FDT map plus UEFI allocations")

by_name = {entry["name"]: entry for entry in entries}
if len(by_name) != len(entries):
    fail("duplicate memory region name")

for entry in entries:
    if entry["base"] % 4096 or entry["size"] % 4096:
        fail(f"{entry['name']} is not aligned to the ARM MMU's 4K mapping granule")

# The display and the shared-memory clients keep running after the handoff.
# A write-back mapping of either one loses writes the firmware never sees.
for name, attribute in (
    ("Display_Reserved", "WRITE_THROUGH_XN"),
    ("SMEM_REGION", "UNCACHED_UNBUFFERED_XN"),
    ("PDP_NS_SHARED_REGION", "UNCACHED_UNBUFFERED_XN"),
    ("SHM", "UNCACHED_UNBUFFERED_XN"),
):
    if not re.search(r'\{"' + re.escape(name) + r'"[^\n]*,\s*' + attribute + r'\}', text):
        fail(f"{name} must retain its framebuffer/shared-memory cache attribute")

required = {
    "GUNYAH_HYP_REGION": (0x80000000, 0x00E00000),
    "Display_Reserved": (0xFC800000, 0x02B00000),
    "DUMP_DISPLAY_REGION": (0xFF3FF000, 0x00001000),
    "RAMOOPS_REGION": (0xFF400000, 0x00200000),
    "HYPERVISOR_EXTENDED_": (0xC3DD0000, 0x00A30000),
    "TRUST_UI_VM_REGION": (0xF3800000, 0x05000000),
    "OEM_VM_REGION": (0xF8800000, 0x03800000),
    "UEFI_FD": (0xA7100000, 0x00300000),
    "Kernel": (0xA8000000, 0x10000000),
    "XNU_Payload": (0xF0000000, 0x03800000),
    "DXE_Heap": (0xB8000000, 0x0BDD0000),
}
for name, expected in required.items():
    entry = by_name.get(name)
    if entry is None:
        fail(f"missing {name}")
    actual = entry["base"], entry["size"]
    if actual != expected:
        fail(f"{name} is {actual[0]:#x}+{actual[1]:#x}, expected {expected[0]:#x}+{expected[1]:#x}")

if by_name["XNU_Payload"]["base"] + by_name["XNU_Payload"]["size"] != by_name["TRUST_UI_VM_REGION"]["base"]:
    fail("XNU payload no longer ends at the trust-UI reservation boundary")
if by_name["DXE_Heap"]["base"] + by_name["DXE_Heap"]["size"] != by_name["HYPERVISOR_EXTENDED_"]["base"]:
    fail("DXE heap no longer ends at the hypervisor-extended reservation boundary")

# Only AddMem descriptors participate in the physical-memory HOB map.  MMIO
# AddDev/NoHob ranges deliberately occupy a different resource namespace.
memory = sorted(
    (entry for entry in entries if entry["hob"] == "AddMem"),
    key=lambda entry: (entry["base"], entry["size"], entry["name"]),
)
for left, right in zip(memory, memory[1:]):
    left_end = left["base"] + left["size"]
    if left_end > right["base"]:
        fail(
            f'{left["name"]} {left["base"]:#x}..{left_end:#x} overlaps '
            f'{right["name"]} {right["base"]:#x}..{right["base"] + right["size"]:#x}'
        )

print(f"PASS: {len(entries)} descriptors, {len(memory)} AddMem ranges, no overlaps")
