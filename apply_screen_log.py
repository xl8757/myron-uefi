"""Add explicit SEC stage messages to the pinned Mu source for DEBUG builds.

The myron board has no UART and the panel is not re-initialised, so the
framebuffer at 0xfc800000 is the only console available before DXE installs the
UEFI console. `BootShim/ScreenLog.c` writes there with general registers only;
this script anchors one line per SEC milestone so a black screen becomes
"the last row that appeared" instead of a guess.

Strict by design: every anchor must occur exactly once, so a Mu update that
changes the SEC flow fails the build instead of silently instrumenting nothing.
"""

from pathlib import Path
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--mu-root', type=Path, required=True)
args = parser.parse_args()

path = args.mu_root / 'Silicon/Silicium/SiliciumPkg/Sec/Sec.c'
source = path.read_text()

edits = [
    ('#include "Sec.h"',
     '#include "Sec.h"\nextern void myron_screen_line(const char *, unsigned);'),
    ('  ArmDisableDataCache ();',
     '  myron_screen_line("[7] SecEntry: disable D-cache", 7);\n  ArmDisableDataCache ();'),
    ('  ArmInvalidateInstructionCache ();',
     '  myron_screen_line("[8] D-cache off; invalidate I-cache", 8);\n  ArmInvalidateInstructionCache ();'),
    ('  ArmEnableInstructionCache ();',
     '  ArmEnableInstructionCache ();\n  myron_screen_line("[9] I-cache ready; invalidate stack/FD", 9);'),
    ('  ArmWriteVBar ((UINTN)SecVectorTable);',
     '  ArmWriteVBar ((UINTN)SecVectorTable);\n  myron_screen_line("[10] SEC exception vectors installed", 10);'),
    ('  PlatformInitialize ();',
     '  myron_screen_line("[11] SecMain: platform handoff", 11);\n  PlatformInitialize ();'),
    ('  Status = InitializeMemory ();',
     '  myron_screen_line("[12] InitializeMemory: HOB and MMU", 12);\n  Status = InitializeMemory ();'),
    ('  // Build CPU HOB',
     '  myron_screen_line("[13] Memory/MMU initialized", 13);\n\n  // Build CPU HOB'),
    ('  Status = PlatformPeim ();',
     '  myron_screen_line("[14] PlatformPeim: stock XBL HOBs", 14);\n  Status = PlatformPeim ();'),
    ('  ProcessLibraryConstructorList ();',
     '  myron_screen_line("[15] XBL HOBs ready; constructors", 15);\n  ProcessLibraryConstructorList ();'),
    ('  Status = DecompressFirstFv ();',
     '  myron_screen_line("[16] Decompress firmware volume", 16);\n  Status = DecompressFirstFv ();'),
    ('  Status = LoadDxeCoreFromFv (NULL, 0);',
     '  myron_screen_line("[17] Firmware ready; load DXE core", 17);\n  Status = LoadDxeCoreFromFv (NULL, 0);'),
]

for before, after in edits:
    if source.count(before) != 1:
        raise SystemExit('Pinned SEC source differs or was already instrumented: ' + before)
    source = source.replace(before, after, 1)

path.write_text(source)
print('Applied %d explicit SEC screen stages to the pinned source' % len(edits))
