"""Give the arch timer a frequency on a platform that never programmed one.

This SoC leaves CNTFRQ_EL0 at zero: ABL does not write it and Mu does not either.
Every consumer of ArmGenericTimerGetTimerFreq() then divides by that zero, and
the DEBUG builds of the Xiaomi drivers simply assert:

    ASSERT ArmArchTimerLib.c +74: ArmGenericTimerGetTimerFreq () != 0

The board file of the known-good port records 19200000 Hz, and its Timer patch
takes the same shape as this one: read the register, and when it reads zero fall
back to the board value. Nothing is written to CNTFRQ_EL0, because a write from
EL1 traps when EL2 has not opened it.

Both physical and virtual counter libraries get the treatment, so it does not
matter which one a given module links.

Paths are discovered rather than assumed: ArmPkg sits under a submodule in some
Mu trees, which moves the file.
"""

import os
import sys

VALUE = '19200000'
OLD = """{
  return ArmReadCntFrq ();
}"""

NEW = """{
  UINTN  Freq;

  Freq = ArmReadCntFrq ();
  if (Freq == 0) {
    //
    // CNTFRQ_EL0 is left at zero by this SoC's ABL, and writing it from EL1
    // traps, so fall back to the frequency the board file records.
    //
    Freq = %s;
  }

  return Freq;
}""" % VALUE

NAMES = (
    'ArmGenericTimerPhyCounterLib.c',
    'ArmGenericTimerVirtCounterLib.c',
)


def find(root):
    """ArmPkg lives under a submodule in some Mu trees, so locate the files."""
    found = {}
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            if name in NAMES and name not in found:
                found[name] = os.path.join(dirpath, name)
    return found


def patch(path):
    with open(path, encoding='utf-8') as fh:
        text = fh.read()
    if 'Freq = %s;' % VALUE in text:
        return 'already patched'
    hits = text.count(OLD)
    if hits != 1:
        raise SystemExit('%s: 预期命中 1 次，实际 %d 次' % (path, hits))
    with open(path, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(text.replace(OLD, NEW, 1))
    return 'patched'


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: apply_mu_timer_freq.py <mu-root>')
    root = sys.argv[1].rstrip('/\\')
    found = find(root)
    for name in NAMES:
        if name not in found:
            continue
        rel = os.path.relpath(found[name], root).replace('\\', '/')
        print('%-72s %s' % (rel, patch(found[name])))
    missing = [n for n in NAMES if n not in found]
    if missing:
        raise SystemExit('没找到: %s' % ', '.join(missing))


if __name__ == '__main__':
    main()
