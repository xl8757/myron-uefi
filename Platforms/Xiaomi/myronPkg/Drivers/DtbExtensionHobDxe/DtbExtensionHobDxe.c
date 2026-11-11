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

**/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/UefiLib.h>

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
      (Strings >= Total) || (Struct <= Strings))
  {
    return FALSE;
  }

  return TRUE;
}

/**
  Build the DtbExtension HOB from the device tree ABL passed in x0.

  Returning an error here would make the dispatcher print a load failure and
  move on, which only adds noise: the interesting diagnostic is the DEBUG line.
  Always report success and let the next driver fail loudly if the tree is
  genuinely missing.

  @retval EFI_SUCCESS  Always.
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

  if (GetFirstGuidHob (&gEfiDtbExtensionHobGuid) != NULL) {
    DEBUG ((DEBUG_ERROR, "DTBHOB: DtbExtension already published, keeping it\n"));
    return EFI_SUCCESS;
  }

  Hob = GetFirstGuidHob (&mEntryArgHobGuid);
  if (Hob == NULL) {
    DEBUG ((DEBUG_ERROR, "DTBHOB: no entry argument HOB, ABL passed no tree\n"));
    return EFI_SUCCESS;
  }

  if (GET_GUID_HOB_DATA_SIZE (Hob) < sizeof (UINT64)) {
    DEBUG ((DEBUG_ERROR, "DTBHOB: entry argument HOB too small\n"));
    return EFI_SUCCESS;
  }

  Dtb = *(CONST UINT64 *)GET_GUID_HOB_DATA (Hob);
  if (!IsDeviceTree (Dtb)) {
    DEBUG ((DEBUG_ERROR, "DTBHOB: x0 0x%lx is not a device tree\n", Dtb));
    return EFI_SUCCESS;
  }

  Existing = Dtb;
  BuildGuidDataHob (&gEfiDtbExtensionHobGuid, &Existing, sizeof (Existing));
  DEBUG ((DEBUG_ERROR, "DTBHOB: published device tree at 0x%lx\n", Dtb));

  return EFI_SUCCESS;
}
