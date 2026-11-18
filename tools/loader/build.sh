#!/bin/bash
# Build the myron bare-metal loader: ADT, payload, Image, and a header-v4 boot image.
#
# This is the second architecture tried on this board. The first was the UniXNU UEFI
# firmware, which needs a payload it can read either from the boot image ramdisk or
# from the firmware volume, and neither route works here:
#
#   - the bootloader passes x0 = 0, so there is no /chosen/linux,initrd-start, and
#     six fixed addresses probed on hardware were all empty;
#   - the firmware volume route needs SEC to decompress FvMain at its full size,
#     about 18 MiB for a kernel-sized payload, and the memory map has no such window
#     - the phone black-screens.
#
# The approach here is the one ShueiTsuki/data-a-live proved on the same SoC (SM8850):
# a small freestanding loader is packed as an arm64 Image, the XNU payload rides
# inside it as a .incbin section, and ABL jumps straight into it. No UEFI, no ramdisk,
# no firmware volume. The loader acts as iBoot: it copies the kernel segments to
# phys_base, lays down the ADT and the boot_args behind them, and enters XNU with the
# MMU off.
#
# Inputs:
#   KERNEL   linked XNU Mach-O. Must keep LC_DYLD_CHAINED_FIXUPS and the bytes they
#            point at: XNU applies those chains itself after entry, so the payload has
#            to carry them intact. Stripping __LINKEDIT destroys them - that mistake
#            was made once already.
#   CC/LD    an AArch64 clang and ld.lld able to target aarch64-none-elf
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
KERNEL=${KERNEL:?KERNEL must point at the linked kernel Mach-O}
OUT=${OUT:-$repo/out/loader}
CC=${CC:-clang}
LD=${LD:-ld.lld}
OBJCOPY=${OBJCOPY:-llvm-objcopy}
READELF=${READELF:-llvm-readelf}
PYTHON=${PYTHON:-python3}

mkdir -p "$OUT"

echo "== kernel =="
stat -c '%s %n' "$KERNEL"

# --- 1. the Apple device tree, with myron's own GIC, framebuffer and DRAM ----------
# --highmem puts the kernel RAM in the high bank at 0x880000000. The low 2 GiB bank
# holds every fixed Qualcomm XPU carve-out (MPSS 235 MiB, ADSP 85 MiB, and so on), so
# it is not usable as kernel-managed memory.
echo "== ADT =="
"$PYTHON" "$here/../payload/mkadt.py" "$OUT/myron.adt" --highmem

# --- 2. pack the kernel + ADT into the SWEETXNU payload ---------------------------
echo "== payload =="
# --no-fixups: keep the chained fixups in the segment bytes for XNU to apply
# itself. UniXNU's DarwinBoot states the rule: "XNU rebases and signs its own fixup
# chains after entry; firmware only copies root mappings". The ios-sweet helper
# predates that and rewrites every pointer to a plain VA, which a kernel that
# expects to do the rebasing cannot use.
"$PYTHON" "$here/../payload/mkpayload.py" "$KERNEL" "$OUT/myron.adt" "$OUT/payload.bin" \
  --cmdline "${CMDLINE:-debug=0xc serial=3 cpus=1 keepsyms=1 -v}" --no-fixups

# --- 3. build the loader ---------------------------------------------------------
echo "== loader =="
# No Apple boot logo: logo.S takes an empty file and the loader leaves the screen
# black until the kernel console appears.
: > "$OUT/logo.bin"
flags=(--target=aarch64-none-elf -O2 -ffreestanding -fno-builtin -nostdlib -fno-pic
       -fno-stack-protector -mgeneral-regs-only -mstrict-align -Wall -Wextra -Werror)

"$CC" "${flags[@]}" -c "$here/start.S"   -o "$OUT/start.o"
"$CC" "${flags[@]}" -DPAYLOAD="\"$OUT/payload.bin\"" -c "$here/payload.S" -o "$OUT/payload.o"
"$CC" "${flags[@]}" -DLOGO="\"$OUT/logo.bin\""       -c "$here/logo.S"    -o "$OUT/logo.o"
"$CC" "${flags[@]}" -c "$here/loader.c"  -o "$OUT/loader.o"

# The image is linked at 0 and loaded at an unknown address, so any absolute
# relocation in the C would be wrong. Fail loudly rather than load garbage.
if "$READELF" -r "$OUT/loader.o" | grep -q R_AARCH64_ABS; then
  echo "absolute relocation in loader.c" >&2
  exit 1
fi

"$LD" -T "$here/loader.ld" --build-id=none \
  "$OUT/start.o" "$OUT/loader.o" "$OUT/logo.o" "$OUT/payload.o" -o "$OUT/loader.elf"
"$OBJCOPY" -O binary "$OUT/loader.elf" "$OUT/Image"

# --- 4. the header has to describe the whole file --------------------------------
# ABL loads image_size bytes starting at text_offset. The payload sits at the end of
# the image, so an image_size that stops before it would leave the loader reading
# whatever the bootloader happened to put there.
echo "== header check =="
"$PYTHON" - "$OUT/Image" "$OUT/payload.bin" <<'PY'
import struct, sys
img = open(sys.argv[1], 'rb').read()
pl = open(sys.argv[2], 'rb').read()
assert img[0x38:0x3c] == b'ARM\x64', 'no arm64 Image magic at offset 56'
text_offset, = struct.unpack_from('<Q', img, 8)
size, = struct.unpack_from('<Q', img, 16)
flags, = struct.unpack_from('<Q', img, 24)
assert text_offset == 0, 'text_offset is %#x, expected 0' % text_offset
assert size == len(img), 'image_size %#x != file size %#x' % (size, len(img))
off = img.find(pl[:64])
assert off > 0, 'payload not found in the image'
assert off % 4096 == 0, 'payload at %#x is not 4K aligned' % off
assert img[off:off + len(pl)] == pl, 'payload bytes differ'
print('Image %d bytes (%0.2f MiB), flags %#x, payload at %#x (%d bytes)'
      % (len(img), len(img) / 2**20, flags, off, len(pl)))
PY

# --- 5. pack it as an Android boot image, header v4, kernel only ------------------
# Header v4, no ramdisk: that is what the stock boot_b uses on this board and what
# data-a-live boots on the same SoC. The v0 header this project used before carries
# load addresses, and the bootloader does not act on them here.
echo "== boot image =="
"$PYTHON" "$repo/tools/mkbootimg_v4.py" "$OUT/Image" "$OUT/myron-loader-boot.img"
stat -c '%s %n' "$OUT/myron-loader-boot.img"
sha256sum "$OUT/myron-loader-boot.img"

echo
echo "boot this with:  fastboot boot $OUT/myron-loader-boot.img"
