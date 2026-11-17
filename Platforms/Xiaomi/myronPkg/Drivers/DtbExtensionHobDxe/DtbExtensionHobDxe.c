/** @file
  Publish the device tree to DXE the way the Qualcomm stack expects it.

  Mu's PlatformPeiLib builds the gEfiDtbExtensionHobGuid HOB by scanning the
  "FD Reserved" memory region for the HOB registration function of the stock
  UEFI image and decoding its ADRP instructions (see XblProtocols.c). That works
  when the stock firmware is the one running, because the stock image is what
  sits in that region.

  It cannot work for a payload booted with "fastboot boot": the region holds
  whatever ABL left there, not a stock UEFI, so the scan finds nothing, no
  DtbExtension HOB is built, and every vendor DXE driver that resolves its
  regions through the device tree fails its lookup. EnvDxeEnhanced is the first
  of them, and its SmemLib then asserts on an uninitialised lock.

  Mu's SEC already records the x0 argument ABL hands over - the live device
  tree, with the dtbo overlay applied - in a GUID HOB. This driver republishes
  that same address under the GUID the vendor drivers look for. It runs as the
  first DXE driver, ahead of EnvDxeEnhanced and the rest of the vendor stack.

  DIAGNOSTIC BUILD. Two things make the plain DEBUG version useless here.

  First, the console only shows the last few dozen lines, so anything printed
  this early scrolls away before a driver further down the stack asserts.
  Second, DEBUG output from this module did not reach the screen at all on
  hardware even though the same channel printed the DXE core's own lines, so
  the report cannot rely on DEBUG.

  This version therefore writes straight to ConOut with OutputString, which is
  what the dispatcher's own load banner uses, and halts on failure so the
  report is the last thing on screen. A successful run returns normally and the
  stack continues.

  Set DTBHOB_VERBOSE to 0 to keep only the failure report.

**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/UefiLib.h>

#include "ScreenFont.h"

//
// Draw on the panel framebuffer directly.
//
// BootShim/ScreenLog.c proves this works here - its SEC stage lines are visible
// on hardware - but its symbol lives in the shim image and a DXE driver cannot
// link against it (lld reports "undefined symbol: myron_screen_line"). The board
// has no UART, and neither DEBUG nor ConOut from this module reached the panel
// while the DXE core's own ConOut lines did, so drawing the same way the shim
// does is the only report channel available.
//
#define DTBHOB_FB_BASE  0xfc800000ULL
#define DTBHOB_FB_WIDTH 1200U
#define DTBHOB_FB_HEIGHT 2608U
#define DTBHOB_FB_STRIDE 1200U
#define DTBHOB_FB_SCALE 2U
#define DTBHOB_FB_MARGIN 24U
#define DTBHOB_FB_BG   0xff102040U
#define DTBHOB_FB_FG   0xffffffffU

STATIC
VOID
ScreenLine (
  IN CONST CHAR8  *Text,
  IN UINTN        Row
  )
{
  volatile UINT32  *Fb   = (volatile UINT32 *)(UINTN)DTBHOB_FB_BASE;
  UINTN             X    = DTBHOB_FB_MARGIN;
  UINTN             Y    = DTBHOB_FB_MARGIN + Row * 16U * DTBHOB_FB_SCALE;
  UINTN             Gy;
  UINTN             Gx;

  if (Y + 16U * DTBHOB_FB_SCALE > DTBHOB_FB_HEIGHT) {
    return;
  }

  for (Gy = 0; Gy < 16U * DTBHOB_FB_SCALE; Gy++) {
    for (Gx = DTBHOB_FB_MARGIN; Gx < DTBHOB_FB_WIDTH - DTBHOB_FB_MARGIN; Gx++) {
      Fb[(Y + Gy) * DTBHOB_FB_STRIDE + Gx] = DTBHOB_FB_BG;
    }
  }

  while ((*Text != '\0') && (X + 8U * DTBHOB_FB_SCALE <= DTBHOB_FB_WIDTH - DTBHOB_FB_MARGIN)) {
    UINTN          Ch = (UINTN)(UINT8)*Text++;
    CONST UINT8   *Glyph;

    if ((Ch < 0x20) || (Ch > 0x7e)) {
      Ch = '?';
    }

    Glyph = font8x16[Ch - 0x20];
    for (Gy = 0; Gy < 16; Gy++) {
      volatile UINT32  *Line = Fb + (Y + Gy * DTBHOB_FB_SCALE) * DTBHOB_FB_STRIDE + X;
      for (Gx = 0; Gx < 8; Gx++) {
        UINT32  Color = (Glyph[Gy] & (0x80U >> Gx)) ? DTBHOB_FB_FG : DTBHOB_FB_BG;
        Line[Gx * DTBHOB_FB_SCALE]                    = Color;
        Line[Gx * DTBHOB_FB_SCALE + 1]                = Color;
        Line[DTBHOB_FB_STRIDE + Gx * DTBHOB_FB_SCALE]     = Color;
        Line[DTBHOB_FB_STRIDE + Gx * DTBHOB_FB_SCALE + 1] = Color;
      }
    }

    X += 8U * DTBHOB_FB_SCALE;
  }

  __asm__ volatile ("dsb sy" ::: "memory");
}

//
// Halt instead of returning when the tree cannot be republished, so the report
// stays visible. 0 restores the original quiet behaviour.
//
#ifndef DTBHOB_HALT_ON_FAILURE
#define DTBHOB_HALT_ON_FAILURE  1
#endif

//
// Print the successful path too. The entry banner and the tree header are what
// say whether SEC recorded x0 and whether it looks like a real FDT.
//
#ifndef DTBHOB_VERBOSE
#define DTBHOB_VERBOSE  1
#endif

//
// Mu-Silicium SEC stores the incoming x0 value in this HOB as a UINT64.
//
STATIC CONST EFI_GUID  mEntryArgHobGuid = {
  0xDAEE82E6, 0x8090, 0x4E72, { 0xBF, 0xB9, 0x78, 0xC1, 0x3C, 0xD4, 0xE3, 0x59 }
};

/**
  Draw one line of the report on the panel and echo it to ConOut.

  Neither DEBUG nor ConOut from this module reached the console on hardware,
  while the DXE core's own ConOut lines did, and the board has no UART. The
  framebuffer writer below is the channel that is known to work here: it is the
  one the SEC stage messages use, and those are visible.

  @param[in] Text  NUL-terminated ASCII text; the part before any newline.
**/
STATIC EFI_SYSTEM_TABLE  *mSystemTable;
STATIC UINTN             mRow = 20;

STATIC
VOID
Say (
  IN CONST CHAR8  *Text
  )
{
  CHAR8   Line[64];
  UINTN   Index;

  //
  // One framebuffer row per call, so the report is a list of lines rather than
  // one overwritten row. ScreenLine takes a single row number.
  //
  for (Index = 0; (Index < sizeof (Line) - 1) && (Text[Index] != '\0') && (Text[Index] != '\n'); Index++) {
    Line[Index] = Text[Index];
  }

  Line[Index] = '\0';
  ScreenLine (Line, mRow);
  mRow++;

  //
  // Also try ConOut: harmless when it works, ignored when it does not.
  //
  if ((mSystemTable != NULL) && (mSystemTable->ConOut != NULL)) {
    CHAR16  Wide[64];
    for (Index = 0; (Index < ARRAY_SIZE (Wide) - 1) && (Line[Index] != '\0'); Index++) {
      Wide[Index] = (CHAR16)Line[Index];
    }
    Wide[Index]   = L'\0';
    Wide[Index + 1] = L'\0';
    mSystemTable->ConOut->OutputString (mSystemTable->ConOut, Wide);
  }
}

/**
  Print a label and a 64-bit value in hex as one line.

  @param[in] Label  ASCII prefix.
  @param[in] Value  Value to append in hex.
**/
STATIC
VOID
SayHex (
  IN CONST CHAR8       *Label,
  IN EFI_PHYSICAL_ADDRESS  Value
  )
{
  CHAR8  Buffer[160];
  UINTN  Position;
  UINTN  Nibble;

  for (Position = 0; (Position < ARRAY_SIZE (Buffer) - 20) && (Label[Position] != '\0'); Position++) {
    Buffer[Position] = Label[Position];
  }

  Buffer[Position++] = '0';
  Buffer[Position++] = 'x';
  for (Nibble = 16; Nibble > 0; Nibble--) {
    UINTN  Digit = (UINTN)((Value >> ((Nibble - 1) * 4)) & 0xF);
    Buffer[Position++] = (CHAR8)(Digit < 10 ? '0' + Digit : 'a' + Digit - 10);
  }

  Buffer[Position++] = '\n';
  Buffer[Position]   = '\0';
  Say (Buffer);
}

/**
  Read a 32-bit big-endian word out of the device tree header.

  @param[in] Base  Candidate device tree.
  @param[in] Off   Byte offset inside the header.

  @retval  The word at Off.
**/
STATIC
UINT32
FdtWord (
  IN CONST UINT8  *Base,
  IN UINTN        Off
  )
{
  return ((UINT32)Base[Off] << 24) | ((UINT32)Base[Off + 1] << 16) |
         ((UINT32)Base[Off + 2] << 8) | (UINT32)Base[Off + 3];
}

/**
  Decide whether an address carries a plausible flattened device tree.

  @param[in] Address  Candidate address.

  @retval TRUE   The magic, the size and the two block offsets all line up.
  @retval FALSE  Anything else.
**/
STATIC
BOOLEAN
IsDeviceTree (
  IN EFI_PHYSICAL_ADDRESS  Address
  )
{
  CONST UINT8  *Base;

  if ((Address == 0) || ((Address & 3) != 0)) {
    return FALSE;
  }

  Base = (CONST UINT8 *)(UINTN)Address;
  if (FdtWord (Base, 0) != 0xD00DFEED) {
    return FALSE;
  }

  UINT32 Total   = FdtWord (Base, 4);
  UINT32 Struct  = FdtWord (Base, 8);
  UINT32 Strings = FdtWord (Base, 12);

  if ((Total < 0x40) || (Struct < 0x28) || (Struct >= Total) ||
      (Strings >= Total) || (Struct <= Strings)) {
    return FALSE;
  }

  return TRUE;
}

/**
  Report a failed publish and, when enabled, stop so it stays on screen.

  @param[in] Reason  Why the tree could not be published.

  @retval EFI_SUCCESS  Only reached when DTBHOB_HALT_ON_FAILURE is 0.
**/
STATIC
EFI_STATUS
Fail (
  IN CONST CHAR8  *Reason
  )
{
  Say ("DTBHOB: FAIL ");
  Say (Reason);
  Say ("\nDTBHOB: the vendor stack will not resolve the tree\n");

#if DTBHOB_HALT_ON_FAILURE
  Say ("DTBHOB: halted; this is the last output by design\n");
  CpuDeadLoop ();
#endif

  return EFI_SUCCESS;
}

/**
  Build the DtbExtension HOB from the device tree ABL passed in x0.

  @param[in]  ImageHandle  The firmware allocated handle for the EFI image.
  @param[in]  SystemTable  A pointer to the EFI System Table.

  @retval EFI_SUCCESS  The HOB was published, or halting is disabled.
**/
EFI_STATUS
EFIAPI
DtbExtensionHobEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HOB_GUID_TYPE     *Hob;
  EFI_PHYSICAL_ADDRESS  Dtb;
  EFI_PHYSICAL_ADDRESS  Existing;

  mSystemTable = SystemTable;

#if DTBHOB_VERBOSE
  Say ("DTBHOB: entry\n");
#endif

  if (GetFirstGuidHob (&gEfiDtbExtensionHobGuid) != NULL) {
    //
    // Already published means something upstream did the work. That is a good
    // outcome, not a failure, so do not halt on it.
    //
    Say ("DTBHOB: DtbExtension already published, keeping it\n");
    return EFI_SUCCESS;
  }

  Hob = GetFirstGuidHob (&mEntryArgHobGuid);
  if (Hob == NULL) {
    return Fail ("no entry argument HOB; SEC did not record x0");
  }

#if DTBHOB_VERBOSE
  SayHex ("DTBHOB: entry argument HOB size ", (EFI_PHYSICAL_ADDRESS)GET_GUID_HOB_DATA_SIZE (Hob));
#endif

  if (GET_GUID_HOB_DATA_SIZE (Hob) < sizeof (UINT64)) {
    return Fail ("entry argument HOB smaller than a pointer");
  }

  Dtb = *(CONST UINT64 *)GET_GUID_HOB_DATA (Hob);
  SayHex ("DTBHOB: x0 ", Dtb);

  if (!IsDeviceTree (Dtb)) {
    SayHex ("DTBHOB: magic ", (EFI_PHYSICAL_ADDRESS)FdtWord ((CONST UINT8 *)(UINTN)Dtb, 0));
    return Fail ("x0 does not point at a flattened device tree");
  }

  SayHex ("DTBHOB: tree size ", (EFI_PHYSICAL_ADDRESS)FdtWord ((CONST UINT8 *)(UINTN)Dtb, 4));
  SayHex ("DTBHOB: off_struct ", (EFI_PHYSICAL_ADDRESS)FdtWord ((CONST UINT8 *)(UINTN)Dtb, 8));
  SayHex ("DTBHOB: off_strings ", (EFI_PHYSICAL_ADDRESS)FdtWord ((CONST UINT8 *)(UINTN)Dtb, 12));

  Existing = Dtb;
  BuildGuidDataHob (&gEfiDtbExtensionHobGuid, &Existing, sizeof (Existing));
  SayHex ("DTBHOB: OK published device tree at ", Dtb);

  return EFI_SUCCESS;
}
