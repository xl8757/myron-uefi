/** @file
  Publish the device tree the Qualcomm vendor stack expects.

  Mu's PlatformPeiLib derives the DtbExtension address by scanning the
  "FD Reserved" region for the stock UEFI's HOB registration function and
  decoding its ADRP instructions (XblProtocols.c). That only works when the
  stock firmware is the one running, because the stock image is what sits in
  that region.

  Under "fastboot boot" the region holds whatever ABL left there, the scan finds
  nothing, no DtbExtension HOB is built, and every vendor DXE driver that
  resolves its regions through the tree fails its lookup. EnvDxeEnhanced is the
  first of them; its SmemLib then asserts on an uninitialised lock, and the same
  failure cascades into SmemDxe and the rest.

  The tree is already in this image: myron.fdf embeds it as a FREEFORM file with
  GUID 25462CDA-221F-47DF-AC1D-259CFAA4E326 and UI "DeviceTreeBlob". This driver
  reads that section and republishes it under gEfiDtbExtensionHobGuid, which is
  the only thing the vendor stack actually looks for.

  Why not reuse EmbeddedPkg's DtPlatformDxe: that driver publishes the tree
  through the EFI_DT_PLATFORM_PROTOCOL / configuration-table path. The Qualcomm
  drivers do not consult it; they look up gEfiDtbExtensionHobGuid directly, so
  both have to be present and it is this HOB that unblocks them.

  It runs from APRIORI, ahead of EnvDxeEnhanced and the rest of the vendor
  stack, because the HOB has to exist before the first lookup.

  Copyright (c) 2026, myron port.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/HobLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "ScreenFont.h"

//
// The FREEFORM file myron.fdf embeds:
//     FILE FREEFORM = 25462CDA-221F-47DF-AC1D-259CFAA4E326
//       SECTION RAW = myronPkg/FdtBlob/myron.dtb
//       SECTION UI  = "DeviceTreeBlob"
//
STATIC CONST EFI_GUID  mDeviceTreeBlobGuid = {
  0x25462CDA, 0x221F, 0x47DF, { 0xAC, 0x1D, 0x25, 0x9C, 0xFA, 0xA4, 0xE3, 0x26 }
};

//
// Draw on the panel framebuffer directly.
//
// The board has no UART, and DEBUG output from this driver did not reach the
// panel on hardware even though the same channel printed the DXE core's own
// lines. This is the channel that is known to work here: it is what the SEC
// stage messages and the shim use, and those are visible.
//
#define DTBHOB_FB_BASE   0xfc800000ULL
#define DTBHOB_FB_WIDTH  1200U
#define DTBHOB_FB_HEIGHT 2608U
#define DTBHOB_FB_STRIDE 1200U
#define DTBHOB_FB_SCALE  2U
#define DTBHOB_FB_MARGIN 24U
#define DTBHOB_FB_BG     0xff102040U
#define DTBHOB_FB_FG     0xffffffffU

STATIC UINTN  mRow = 20;

STATIC
VOID
Say (
  IN CONST CHAR8  *Text
  )
{
  volatile UINT32  *Fb = (volatile UINT32 *)(UINTN)DTBHOB_FB_BASE;
  UINTN             X  = DTBHOB_FB_MARGIN;
  UINTN             Y  = DTBHOB_FB_MARGIN + mRow * 16U * DTBHOB_FB_SCALE;
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
    UINTN         Ch = (UINTN)(UINT8)*Text++;
    CONST UINT8  *Glyph;

    if ((Ch < 0x20) || (Ch > 0x7e)) {
      Ch = '?';
    }

    Glyph = font8x16[Ch - 0x20];
    for (Gy = 0; Gy < 16; Gy++) {
      volatile UINT32  *Line = Fb + (Y + Gy * DTBHOB_FB_SCALE) * DTBHOB_FB_STRIDE + X;
      for (Gx = 0; Gx < 8; Gx++) {
        UINT32  Color = (Glyph[Gy] & (0x80U >> Gx)) ? DTBHOB_FB_FG : DTBHOB_FB_BG;
        Line[Gx * DTBHOB_FB_SCALE]                        = Color;
        Line[Gx * DTBHOB_FB_SCALE + 1]                    = Color;
        Line[DTBHOB_FB_STRIDE + Gx * DTBHOB_FB_SCALE]     = Color;
        Line[DTBHOB_FB_STRIDE + Gx * DTBHOB_FB_SCALE + 1] = Color;
      }
    }

    X += 8U * DTBHOB_FB_SCALE;
  }

  __asm__ volatile ("dsb sy" ::: "memory");
  mRow++;
}

STATIC
VOID
SayHex (
  IN CONST CHAR8           *Label,
  IN EFI_PHYSICAL_ADDRESS  Value
  )
{
  CHAR8  Buffer[72];
  UINTN  At = 0;
  UINTN  Shift;

  while ((*Label != '\0') && (At < 48)) {
    Buffer[At++] = *Label++;
  }

  Buffer[At++] = ' ';
  Buffer[At++] = '0';
  Buffer[At++] = 'x';
  for (Shift = 16; Shift > 0; Shift--) {
    UINTN  Digit = (UINTN)((Value >> ((Shift - 1) * 4)) & 0xF);
    Buffer[At++] = (CHAR8)(Digit < 10 ? '0' + Digit : 'A' + Digit - 10);
  }

  Buffer[At] = '\0';
  Say (Buffer);
}

/**
  Read a 32-bit big-endian word out of a device tree header.
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
**/
STATIC
BOOLEAN
IsDeviceTree (
  IN CONST UINT8  *Base
  )
{
  UINT32  Total;
  UINT32  Struct;
  UINT32  Strings;

  if (Base == NULL) {
    return FALSE;
  }

  if (FdtWord (Base, 0) != 0xD00DFEED) {
    return FALSE;
  }

  Total   = FdtWord (Base, 4);
  Struct  = FdtWord (Base, 8);
  Strings = FdtWord (Base, 12);

  if ((Total < 0x40) || (Struct < 0x28) || (Struct >= Total) ||
      (Strings >= Total) || (Struct <= Strings))
  {
    return FALSE;
  }

  return TRUE;
}

/**
  Publish the embedded tree as the DtbExtension HOB the vendor stack reads.

  @param[in]  ImageHandle  The firmware allocated handle for the EFI image.
  @param[in]  SystemTable  A pointer to the EFI System Table.

  @retval EFI_SUCCESS  The HOB was published, or an earlier one already existed.
**/
EFI_STATUS
EFIAPI
DtbExtensionHobEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS           Status;
  VOID                 *Blob;
  UINTN                Size;
  EFI_PHYSICAL_ADDRESS Address;

  Say ("DTBHOB: entry");

  if (GetFirstGuidHob (&gEfiDtbExtensionHobGuid) != NULL) {
    Say ("DTBHOB: already published, keeping it");
    return EFI_SUCCESS;
  }

  //
  // GetSectionFromAnyFv is the lookup UniXNURamDiskDxe already uses in this
  // firmware for its own FREEFORM sections, so it is known to work here.
  //
  Status = GetSectionFromAnyFv (&mDeviceTreeBlobGuid, EFI_SECTION_RAW, 0, &Blob, &Size);
  if (EFI_ERROR (Status)) {
    SayHex ("DTBHOB: no DeviceTreeBlob, status ", (EFI_PHYSICAL_ADDRESS)Status);
    Say ("DTBHOB: vendor drivers will fail to resolve the tree");
    return EFI_SUCCESS;
  }

  SayHex ("DTBHOB: blob size ", (EFI_PHYSICAL_ADDRESS)Size);
  SayHex ("DTBHOB: blob at ", (EFI_PHYSICAL_ADDRESS)(UINTN)Blob);

  if (!IsDeviceTree ((CONST UINT8 *)Blob)) {
    SayHex ("DTBHOB: magic ", (EFI_PHYSICAL_ADDRESS)FdtWord ((CONST UINT8 *)Blob, 0));
    Say ("DTBHOB: blob is not a device tree");
    return EFI_SUCCESS;
  }

  SayHex ("DTBHOB: tree size ", (EFI_PHYSICAL_ADDRESS)FdtWord ((CONST UINT8 *)Blob, 4));

  Address = (EFI_PHYSICAL_ADDRESS)(UINTN)Blob;

  //
  // BuildGuidDataHob returns the HOB pointer, not an EFI_STATUS, and has no
  // failure mode worth reporting; verify the HOB exists instead.
  //
  BuildGuidDataHob (&gEfiDtbExtensionHobGuid, &Address, sizeof (Address));
  if (GetFirstGuidHob (&gEfiDtbExtensionHobGuid) == NULL) {
    Say ("DTBHOB: the HOB did not appear after building it");
    return EFI_SUCCESS;
  }

  SayHex ("DTBHOB: OK published the tree at ", Address);

  //
  // Leave the report on screen long enough to read it. The vendor stack follows
  // immediately and its own output would scroll this away.
  //
  gBS->Stall (3000000);

  return EFI_SUCCESS;
}
