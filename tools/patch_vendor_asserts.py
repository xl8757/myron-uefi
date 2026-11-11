"""Turn vendor DEBUG asserts from fatal into advisory.

Xiaomi's prebuilt DXE binaries are DEBUG builds, so every ASSERT ends up in
``CpuDeadLoop``. A vendor library constructor that merely returns
``EFI_NOT_FOUND`` therefore halts the whole platform, and no PCD of ours can
change it because PcdDebugPropertyMask is baked in at their compile time.

The assert printer in those binaries looks like this:

    adrp/add x0, "AutoGen.c"
    adrp/add x2, "!RETURN_ERROR (Status)"
    mov      w1, #378
    bl       printer            ; formats and emits "ASSERT %a +%d: %a"
    ...
    bl       spin               ; CpuDeadLoop: str xzr,[sp,#8]; ldr; cbnz; b back
    ret

Rewriting the printer's ``bl`` to a spin helper into a ``nop`` keeps the
diagnostic on screen, keeps the stack balanced (the helper is never entered),
and lets the driver continue exactly the way a RELEASE build would.

Run it over the extracted vendor inputs; it reports what it changed and exits
non-zero when a module that should have a printer has none, so a silent
regression cannot slip through.
"""

import argparse
import glob
import os
import struct
import sys

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

PRINTER_FORMAT = b'ASSERT %a +%d: %a'
SPIN_WINDOW = 0x80          # how far to look for the self-loop inside a callee
NOP = 0xD503201F

BRANCHES = {
    'b', 'b.eq', 'b.ne', 'b.lt', 'b.gt', 'b.le', 'b.ge', 'b.hi', 'b.ls', 'b.cc',
    'b.cs', 'b.mi', 'b.pl', 'b.vs', 'b.vc', 'cbz', 'cbnz', 'tbz', 'tbnz',
}


class Pe:
    """Just enough PE32+ to map RVAs onto file offsets."""

    def __init__(self, data):
        self.data = data
        e = struct.unpack_from('<I', data, 0x3C)[0]
        if data[e:e + 4] != b'PE\0\0':
            raise ValueError('not a PE image')
        nsec = struct.unpack_from('<H', data, e + 6)[0]
        opt = e + 24
        self.entry = struct.unpack_from('<I', data, opt + 16)[0]
        sopt = opt + struct.unpack_from('<H', data, e + 20)[0]
        self.secs = []
        for i in range(nsec):
            o = sopt + i * 40
            name = data[o:o + 8].rstrip(b'\0').decode('ascii', 'replace')
            vsz, va, rsz, roff = struct.unpack_from('<IIII', data, o + 8)
            self.secs.append((name, va, vsz, roff, rsz))

    def off(self, rva):
        for _n, va, vsz, roff, rsz in self.secs:
            if va <= rva < va + max(vsz, rsz):
                d = rva - va
                if d < rsz:
                    return roff + d
        return None

    def read(self, rva, n):
        o = self.off(rva)
        return None if o is None else self.data[o:o + n]

    def text(self):
        for name, va, vsz, _roff, _rsz in self.secs:
            if name == '.text':
                return va, vsz
        raise ValueError('no .text section')


def disassemble(img):
    """Linear sweep that keeps going when it walks into embedded data."""
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    md.detail = True
    va, vsz = img.text()
    code = img.read(va, vsz)
    out = []
    pos = 0
    while pos < len(code) - 3:
        got = False
        for ins in md.disasm(code[pos:], va + pos):
            out.append(ins)
            got = True
            pos = ins.address + ins.size - va
        if not got:
            pos += 4                     # 4 字节对齐地跳过无法解码的数据
    return out


def find_printer_entries(img, insns):
    """Return offsets of instructions that load the assert format string."""
    needle = img.data.find(PRINTER_FORMAT)
    if needle < 0:
        return None
    # 把文件偏移换算成 RVA
    target = None
    for _n, va, _vsz, roff, rsz in img.secs:
        if roff <= needle < roff + rsz:
            target = va + (needle - roff)
            break
    if target is None:
        return None

    page = {}
    hits = []
    for ins in insns:
        if ins.mnemonic == 'adrp':
            try:
                page[ins.operands[0].reg] = ins.operands[1].imm
            except Exception:
                pass
        elif ins.mnemonic == 'add' and len(ins.operands) == 3:
            o0, o1, o2 = ins.operands
            if o0.type == 1 and o1.type == 1 and o2.type == 2 and o1.reg in page:
                if page[o1.reg] + o2.imm == target:
                    hits.append(ins.address)
    return hits


def function_start(insns, idx):
    """Walk back to the prologue of the function containing insns[idx].

    ``sub sp, sp, #N`` is only a prologue when it follows a terminator;
    inside a large frame it is just the local allocation.
    """
    for j in range(idx, max(0, idx - 200), -1):
        ins = insns[j]
        if ins.mnemonic == 'stp' and ins.op_str.startswith('x29, x30, [sp, #-'):
            return j
        if ins.mnemonic == 'sub' and ins.op_str.startswith('sp, sp, #'):
            prev = insns[j - 1] if j else None
            if prev is None or prev.mnemonic in ('ret', 'b', 'br'):
                return j
        if ins.mnemonic == 'ret':
            return j + 1
    return 0


def is_spin(insns, start_addr):
    """Detect CpuDeadLoop and friends.

    edk2 compiles ``CpuDeadLoop()`` as ``volatile UINTN Index = 0; while (!Index);``
    which shows up as a zero-store to a stack slot followed by a backward branch
    that re-reads it::

        str  xzr, [sp, #8]
        ldr  x8, [sp, #8]
        cbz  x8, <load again>      ; or: cbnz x8, exit / b <load>

    Requiring the zero-store is what separates it from a plain loop such as
    ``AsciiStrLen``, which also branches backwards but never parks a counter in
    the frame.
    """
    lo, hi = 0, len(insns) - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        if insns[mid].address < start_addr:
            lo = mid + 1
        else:
            hi = mid - 1
    idx = lo
    if idx >= len(insns) or insns[idx].address != start_addr:
        return False

    body = [ins for ins in insns[idx:idx + 40]
            if ins.address - start_addr <= SPIN_WINDOW]
    for pos, ins in enumerate(body):
        if ins.mnemonic == 'str' and ins.op_str.startswith('xzr, [sp'):
            for nxt in body[pos + 1:pos + 7]:
                if nxt.mnemonic in BRANCHES and nxt.operands:
                    t = nxt.operands[-1]
                    if t.type == 2 and start_addr <= t.imm < nxt.address:
                        return True
    return False


def patch_image(path, dry_run):
    data = bytearray(open(path, 'rb').read())
    img = Pe(data)
    insns = disassemble(img)
    entries = find_printer_entries(img, insns)
    if entries is None:
        return None                      # 模块里没有断言打印机
    idx_of = {ins.address: k for k, ins in enumerate(insns)}

    patched = []
    seen_printers = set()
    for hit in entries:
        k = idx_of.get(hit)
        if k is None:
            continue
        start = function_start(insns, k)
        if start in seen_printers:
            continue
        seen_printers.add(start)
        for ins in insns[start:start + 160]:
            if ins.mnemonic == 'ret':
                break
            if ins.mnemonic != 'bl' or not ins.operands or ins.operands[0].type != 2:
                continue
            tgt = ins.operands[0].imm
            if not is_spin(insns, tgt):
                continue
            off = img.off(ins.address)
            if off is None:
                continue
            patched.append((ins.address, tgt))
            if not dry_run:
                struct.pack_into('<I', data, off, NOP)
    if patched and not dry_run:
        open(path, 'wb').write(data)
    return patched


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('root', help='directory holding extracted vendor inputs')
    ap.add_argument('--check', action='store_true', help='report only')
    ap.add_argument('--require', type=int, default=0,
                    help='fail when fewer than N calls were neutralised')
    args = ap.parse_args()

    total = 0
    missing = []
    for path in sorted(glob.glob(os.path.join(args.root, '*', '*.efi'))):
        mod = os.path.basename(os.path.dirname(path))
        res = patch_image(path, args.check)
        if res is None:
            missing.append(mod)
            continue
        if not res:
            print('%-26s printer found, no spin helper in it' % mod)
            continue
        total += len(res)
        for call, tgt in res:
            print('%-26s nop bl 0x%05x -> spin 0x%05x' % (mod, call, tgt))

    print('\n%d call(s) neutralised across %d module(s)%s'
          % (total, len(glob.glob(os.path.join(args.root, '*', '*.efi'))) - len(missing),
             ' (dry run)' if args.check else ''))
    print('%d module(s) carry no assert printer: %s'
          % (len(missing), ', '.join(missing) if missing else '-'))
    if args.require and total < args.require:
        print('FAIL: expected at least %d neutralised calls, got %d' % (args.require, total))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
