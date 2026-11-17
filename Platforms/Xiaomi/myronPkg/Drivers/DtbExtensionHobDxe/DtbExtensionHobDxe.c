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

  DIAGNOSTIC BUILD. The console only shows the last couple of dozen lines, so a
  line printed here scrolls away long before a driver further down the stack
  asserts and leaves its message on screen. To make this driver's outcome
  visible, it now halts on failure instead of returning success quietly. The
  screen then ends on this driver's report, which distinguishes the two cases
  worth telling apart:

    - halted here: the device tree could not be republished, and the printed
      reason says which check failed. The vendor stack never gets to run, so
      any later assertion is a consequence, not a second bug.
    - reached SmemDxe and asserted there: the tree was republished, so SmemDxe
      fails for its own reason.

  Set DT BHOB to release mode (-D DT BHOB_HALT=0) once bring-up is past this.

**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/UefiLib.h>

//
// Halt instead of returning on a failed publish. 1 keeps the diagnostic on
// screen; 0 restores the quiet behaviour documented above.
//
#ifndef DTBHOB_HALT_ON_FAILURE
#define DTBHOB_HALT_ON_FAILURE  1
#endif

//
// Mu-Silicium SEC stores the incoming x0 value in this HOB as a UINT64.
//
STATIC CONST EFI_GUID  mEntryArgHobGuid = {
  0xDAEE82E6, 0x8090, 0x4E72, { 0xBF, 0xB9, 0x78, 0xC1, 0x3C, 0xD4, 0xE3, 0x59 }
};

/**
  Read a 32-bit big-endian word out of the device tree header.

  @param[in] Base  Candidate device tree.
  @param[in] Off   Byte offset inside the header.

  @retval  The word at Off, or MAX_UINT32 when Base is not a device tree.
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

  UINT32 Total  = FdtWord (Base, 4);
  UINT32 Struct = FdtWord (Base, 8);
  UINT32 Strings = FdtWord (Base, 12);

  if ((Total < 0x40) || (Struct < 0x28) || (Struct >= Total) ||
      (Strings >= Total) || (Struct <= Strings)) {
    return FALSE;
  }

  return TRUE;
}

/**
  Report a failed publish and, in the diagnostic build, stop so the message
  stays on the console.

  @param[in] Reason  Why the tree could not be published.

  @retval EFI_SUCCESS  Only reached when DTBHOB_HALT_ON_FAILURE is 0.
**/
STATIC
EFI_STATUS
Fail (
  IN CONST CHAR8  *Reason
  )
{
  DEBUG ((DEBUG_ERROR, "DTBHOB: FAIL %a\n", Reason));
  DEBUG ((DEBUG_ERROR, "DTBHOB: the vendor stack will not resolve the tree\n"));

#if DTBHOB_HALT_ON_FAILURE
  DEBUG ((DEBUG_ERROR, "DTBHOB: halting here to keep this message on screen\n"));
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

  DEBUG ((DEBUG_ERROR, "DTBHOB: entry, looking for the SEC entry argument\n"));

  if (GetFirstGuidHob (&gEfiDtbExtensionHobGuid) != NULL) {
    //
    // Already published means something upstream did the work. That is a good
    // outcome, not a failure, so do not halt on it.
    //
    DEBUG ((DEBUG_ERROR, "DTBHOB: DtbExtension already published, keeping it\n"));
    return EFI_SUCCESS;
  }

  Hob = GetFirstGuidHob (&mEntryArgHobGuid);
  if (Hob == NULL) {
    return Fail ("no entry argument HOB; SEC did not record x0");
  }

  DEBUG ((DEBUG_ERROR, "DTBHOB: entry argument HOB size %u\n",
          (UINT32)GET_GUID_HOB_DATA_SIZE (Hob)));

  if (GET_GUID_HOB_DATA_SIZE (Hob) < sizeof (UINT64)) {
    return Fail ("entry argument HOB smaller than a pointer");
  }

  Dtb = *(CONST UINT64 *)GET_GUID_HOB_DATA (Hob);
  DEBUG ((DEBUG_ERROR, "DTBHOB: x0 = 0x%lx\n", Dtb));

  if (!IsDeviceTree (Dtb)) {
    DEBUG ((DEBUG_ERROR, "DTBHOB: magic at x0 is 0x%x, expected 0xd00dfeed\n",
            FdtWord ((CONST UINT8 *)(UINTN)Dtb, 0)));
    return Fail ("x0 does not point at a flattened device tree");
  }

  DEBUG ((DEBUG_ERROR, "DTBHOB: tree size 0x%x, off_struct 0x%x, off_strings 0x%x\n",
          FdtWord ((CONST UINT8 *)(UINTN)Dtb, 4),
          FdtWord ((CONST UINT8 *)(UINTN)Dtb, 8),
          FdtWord ((CONST UINT8 *)(UINTN)Dtb, 12)));

  Existing = Dtb;
  BuildGuidDataHob (&gEfiDtbExtensionHobGuid, &Existing, sizeof (Existing));
  DEBUG ((DEBUG_ERROR, "DTBHOB: OK published device tree at 0x%lx\n", Dtb));

  return EFI_SUCCESS;
}
