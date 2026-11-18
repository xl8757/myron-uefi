/** @file
  Publish the FAT initrd from a Mu-Silicium FDT handoff as a read-only block device.

  SPDX-License-Identifier: BSD-2-Clause
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>

// Mu-Silicium SEC stores the incoming x0 value in this HOB as a UINT64.
STATIC CONST EFI_GUID mEntryArgHobGuid = {
  0xDAEE82E6, 0x8090, 0x4E72, { 0xBF, 0xB9, 0x78, 0xC1, 0x3C, 0xD4, 0xE3, 0x59 }
};

STATIC
UINT64
ReadCells (
  IN CONST VOID  *Data,
  IN UINT32      Cells
  )
{
  if (Cells == 2) {
    return Fdt64ToCpu (ReadUnaligned64 (Data));
  }

  return Fdt32ToCpu (ReadUnaligned32 (Data));
}

#define RAM_BLOCK_SIZE  512

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} RAM_BLOCK_DEVICE_PATH;
#pragma pack()

STATIC RAM_BLOCK_DEVICE_PATH  mDevicePath = {
  {
    { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { (UINT8)sizeof (VENDOR_DEVICE_PATH), (UINT8)(sizeof (VENDOR_DEVICE_PATH) >> 8) } },
    { 0xA9C6827C, 0x1BD8, 0x410C, { 0x88, 0x5A, 0xC4, 0x65, 0x00, 0xF7, 0xD8, 0x36 } }
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 } }
};

STATIC UINT8                    *mRamBase;
STATIC EFI_BLOCK_IO_MEDIA       mMedia;

STATIC
EFI_STATUS
EFIAPI
RamBlockReset (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN BOOLEAN                ExtendedVerification
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
RamBlockRead (
  IN  EFI_BLOCK_IO_PROTOCOL  *This,
  IN  UINT32                 MediaId,
  IN  EFI_LBA                Lba,
  IN  UINTN                  BufferSize,
  OUT VOID                   *Buffer
  )
{
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (MediaId != mMedia.MediaId) {
    return EFI_MEDIA_CHANGED;
  }

  if ((BufferSize % RAM_BLOCK_SIZE) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if ((Lba > mMedia.LastBlock) || ((BufferSize / RAM_BLOCK_SIZE) > (mMedia.LastBlock + 1 - Lba))) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (Buffer, mRamBase + Lba * RAM_BLOCK_SIZE, BufferSize);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
RamBlockWrite (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  IN VOID                   *Buffer
  )
{
  return EFI_WRITE_PROTECTED;
}

STATIC
EFI_STATUS
EFIAPI
RamBlockFlush (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

STATIC EFI_BLOCK_IO_PROTOCOL  mBlockIo = {
  EFI_BLOCK_IO_PROTOCOL_REVISION,
  &mMedia,
  RamBlockReset,
  RamBlockRead,
  RamBlockWrite,
  RamBlockFlush
};

//
// Where the boot image asks ABL to place the ramdisk.
//
// 0x01000000 is the stock value: the phone's own vendor_boot.img asks the
// bootloader for exactly this address, and it is bootimg.py's default
// (base + ramdisk_offset). Two other addresses were tried here and neither
// worked - 0xA6F00000 (the 1 MiB Initrd_Small window) and 0xF0000000 (the 56 MiB
// XNU_Payload window) - and a probe of six DRAM addresses found no payload at
// any of them, which is what a bootloader that never accepted the address looks
// like. MemoryMapLib does not declare 0x01000000, but MemoryMapLib only
// describes the regions UEFI itself uses; the bootloader's map is its own, and
// the stock image proves it loads there.
//
// This has to match the ramdisk address make_payload.py writes into the boot
// image header, or the driver finds no FAT boot sector and publishes nothing.
//
#define RAMDISK_FALLBACK_BASE  0x01000000
#define RAMDISK_FALLBACK_MAX   0x03800000

//
// Diagnostics for a board with no readable output channel. Everything printed
// during DXE scrolls away, the BDS clears the console, and the menu app paints
// over most of what is left. Two channels that survive all of that:
//
//  - a timer rewrites the status line at row 0 of the console, the one row that
//    stays visible in the log area the menu leaves uncovered;
//  - the same timer paints small squares in the top-left corner of whatever
//    framebuffer the GOP currently reports, on top of any graphics.
//
// One white square: the firmware volume copy. Two: the loader hand-over. Three:
// the fixed-address fallback. Red instead of white: that path failed. The line
// names the path in words as well.
//
#define MARKER_SQUARE  48
#define MARKER_PITCH   64
#define MARKER_MARGIN  8
#define MARKER_WHITE   0xFFFFFFFFU
// Opaque red in BGRA; the exact hue does not matter, only that it is not
// white, because "not white" is the failure signal.
#define MARKER_RED     0xFF0000FFU

STATIC CHAR16  mStatus[96];
STATIC UINTN   mMarkerCount;
STATIC UINT32  mMarkerColor = MARKER_WHITE;

STATIC
VOID
DrawMarkers (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE  *Mode;
  UINT32                        *Row;
  UINTN                         Index;
  UINTN                         X;
  UINTN                         Y;

  if (mMarkerCount == 0) {
    return;
  }

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR (Status) || (Gop == NULL) || (Gop->Mode == NULL)) {
    return;
  }

  Mode = Gop->Mode;
  // Only a blt-only mode has no linear framebuffer to write into. Bitmask
  // pixel formats are fine: the markers are 0xFFFFFFFF or near-solid, so the
  // channel order does not change whether they read as white or as a colour.
  if ((Mode->Info == NULL) || (Mode->Info->PixelFormat == PixelBltOnly)) {
    return;
  }

  for (Index = 0; Index < mMarkerCount; Index++) {
    for (Y = MARKER_MARGIN; (Y < MARKER_MARGIN + MARKER_SQUARE) && (Y < Mode->Info->VerticalResolution); Y++) {
      Row = (UINT32 *)(UINTN)(Mode->FrameBufferBase + (UINT64)Y * Mode->Info->PixelsPerScanLine * 4);
      for (X = MARKER_MARGIN + Index * MARKER_PITCH;
           (X < MARKER_MARGIN + Index * MARKER_PITCH + MARKER_SQUARE) && (X < Mode->Info->HorizontalResolution);
           X++)
      {
        Row[X] = mMarkerColor;
      }
    }
  }
}

STATIC
VOID
PrintStatusLine (
  VOID
  )
{
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->SetCursorPosition (gST->ConOut, 0, 0);
    gST->ConOut->OutputString (gST->ConOut, mStatus);
  }
}

STATIC
VOID
EFIAPI
StatusTick (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  PrintStatusLine ();
  DrawMarkers ();
}

STATIC
VOID
PublishStatus (
  IN UINTN          MarkerCount,
  IN UINT32         MarkerColor,
  IN CONST CHAR16   *Text
  )
{
  EFI_EVENT  Event;
  UINTN      Index;

  Index = 0;
  while ((Index < 70) && (Text[Index] != L'\0')) {
    mStatus[Index] = Text[Index];
    Index++;
  }

  // Pad with spaces so an earlier, longer line cannot survive beside this one.
  while (Index < 70) {
    mStatus[Index] = L' ';
    Index++;
  }

  mStatus[Index] = L'\0';
  mMarkerCount   = MarkerCount;
  mMarkerColor   = MarkerColor;

  PrintStatusLine ();
  DrawMarkers ();

  Event = NULL;
  if (!EFI_ERROR (gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_APPLICATION, StatusTick, NULL, &Event))) {
    gBS->SetTimer (Event, TimerPeriodic, 20000000);
  }
}

STATIC
VOID
PublishRange (
  IN UINT64  Start,
  IN UINT64  End
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  //
  // The loader does not always end the initrd on a sector boundary: the nubia
  // NX769J hands over an end address whose low bits are 0x430. Round the
  // published length up instead of refusing the ramdisk; the volume is
  // read-only, and the extra bytes stay inside the RAM reserved for it.
  //
  if (((End - Start) % RAM_BLOCK_SIZE) != 0) {
    UINT64  Rounded = ((End - Start) + RAM_BLOCK_SIZE - 1) / RAM_BLOCK_SIZE * RAM_BLOCK_SIZE;
    DEBUG ((DEBUG_WARN, "UNIXNURAMDISK: 0x%lx-0x%lx rounded to 0x%lx bytes\n", Start, End, Rounded));
    End = Start + Rounded;
  }

  mRamBase                = (UINT8 *)(UINTN)Start;
  mMedia.MediaId          = 1;
  mMedia.RemovableMedia   = FALSE;
  mMedia.MediaPresent     = TRUE;
  mMedia.LogicalPartition = FALSE;
  mMedia.ReadOnly         = TRUE;
  mMedia.WriteCaching     = FALSE;
  mMedia.BlockSize        = RAM_BLOCK_SIZE;
  mMedia.IoAlign          = 1;
  mMedia.LastBlock        = (End - Start) / RAM_BLOCK_SIZE - 1;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiDevicePathProtocolGuid,
                  &mDevicePath,
                  &gEfiBlockIoProtocolGuid,
                  &mBlockIo,
                  NULL
                  );
  DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: block device 0x%lx size 0x%lx %r\n", Start, End - Start, Status));
}

STATIC
BOOLEAN
PublishInitrd (
  IN CONST VOID  *Fdt
  )
{
  INT32       Chosen;
  CONST VOID  *StartProp;
  CONST VOID  *EndProp;
  INT32       StartLength;
  INT32       EndLength;
  UINT64      Start;
  UINT64      End;

  Chosen = FdtPathOffset (Fdt, "/chosen");
  if (Chosen < 0) {
    return FALSE;
  }

  StartProp = FdtGetProp (Fdt, Chosen, "linux,initrd-start", &StartLength);
  EndProp   = FdtGetProp (Fdt, Chosen, "linux,initrd-end", &EndLength);
  if ((StartProp == NULL) || (EndProp == NULL) ||
      ((StartLength != 4) && (StartLength != 8)) || ((EndLength != 4) && (EndLength != 8)))
  {
    return FALSE;
  }

  Start = ReadCells (StartProp, StartLength / 4);
  End   = ReadCells (EndProp, EndLength / 4);
  if (End <= Start) {
    return FALSE;
  }

  PublishStatus (2, MARKER_WHITE, L"UNIXNURAMDISK: payload from /chosen");
  PublishRange (Start, End);
  return TRUE;
}

//
// The payload volume embedded in the firmware volume (see myron.fdf). This is
// the path that does not depend on the loader at all.
//
STATIC CONST EFI_GUID  mPayloadVolumeGuid = {
  0xA1B2C3D4, 0x5E6F, 0x4A7B, { 0x8C, 0x9D, 0x0E, 0x1F, 0x2A, 0x3B, 0x4C, 0x5D }
};

/**
  Publish the payload volume carried by the firmware volume.

  @retval TRUE   A volume was found and published.
  @retval FALSE  No such section in any firmware volume.
**/
STATIC
BOOLEAN
PublishPayloadVolume (
  VOID
  )
{
  EFI_STATUS  Status;
  VOID        *Data;
  UINTN       Size;

  Data   = NULL;
  Size   = 0;
  Status = GetSectionFromAnyFv (&mPayloadVolumeGuid, EFI_SECTION_RAW, 0, &Data, &Size);
  if (EFI_ERROR (Status) || (Data == NULL) || (Size == 0)) {
    DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: no payload volume in the FV: %r\n", Status));
    PublishStatus (1, MARKER_RED, L"UNIXNURAMDISK: payload not in the FV");
    return FALSE;
  }

  DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: payload volume at 0x%lx, %lu bytes\n", (UINT64)(UINTN)Data, (UINT64)Size));
  PublishStatus (1, MARKER_WHITE, L"UNIXNURAMDISK: payload from the FV");
  PublishRange ((UINT64)(UINTN)Data, (UINT64)(UINTN)Data + Size);
  return TRUE;
}

/**
  Size a FAT volume sitting at Base from its own boot sector.

  @param[in]  Base  Candidate volume.
  @param[out] Size  Length in bytes taken from the BPB.

  @retval TRUE   A boot sector with a sane geometry is there.
  @retval FALSE  Nothing usable.
**/
STATIC
BOOLEAN
FatVolumeSize (
  IN  UINT64  Base,
  OUT UINT64  *Size
  )
{
  CONST UINT8  *Sector = (CONST UINT8 *)(UINTN)Base;
  UINT16       BytesPerSector;
  UINT16       Total16;
  UINT32       Total32;
  UINT8        SectorsPerCluster;
  UINT64       Total;

  if ((Sector[510] != 0x55) || (Sector[511] != 0xAA)) {
    return FALSE;
  }

  BytesPerSector    = ReadUnaligned16 ((CONST UINT16 *)(Sector + 11));
  SectorsPerCluster = Sector[13];
  Total16           = ReadUnaligned16 ((CONST UINT16 *)(Sector + 19));
  Total32           = ReadUnaligned32 ((CONST UINT32 *)(Sector + 32));

  if ((BytesPerSector != RAM_BLOCK_SIZE) ||
      ((SectorsPerCluster != 1) && (SectorsPerCluster != 2) &&
       (SectorsPerCluster != 4) && (SectorsPerCluster != 8) &&
       (SectorsPerCluster != 16) && (SectorsPerCluster != 32)))
  {
    return FALSE;
  }

  Total = (Total16 != 0) ? Total16 : Total32;
  if ((Total == 0) || (Total * RAM_BLOCK_SIZE > RAMDISK_FALLBACK_MAX)) {
    return FALSE;
  }

  *Size = Total * RAM_BLOCK_SIZE;
  return TRUE;
}

/**
  Publish the ramdisk at the address the boot image asks ABL to load it to.

  ABL only writes linux,initrd-start/end into /chosen when it recognises the
  payload as something it should describe, and it does not do that for a boot
  shim. The ramdisk is in memory either way; its own boot sector says how long
  it is, so nothing else is needed.
**/
#define DRAM_BASE  0x80000000ULL

/**
  Search DRAM for the payload's FAT boot sector.

  The bootloader picks the ramdisk address itself on this board and reports nothing
  about it (no device tree, x0 = 0), so the volume has to be found rather than
  looked up. Only regions at or above DRAM_BASE are read: everything below that is
  MMIO on this platform, which is exactly why the earlier probes at 0x01000000
  faulted instead of returning data.

  @param[out] FoundStart    First byte of the FAT volume.
  @param[out] FoundSize     Its size, taken from its own BPB.
  @param[out] ScannedBytes  How much DRAM the scan covered, for the log.

  @retval TRUE   A FAT volume was found.
  @retval FALSE  None was.
**/
STATIC
BOOLEAN
ScanDramForPayload (
  OUT UINT64  *FoundStart,
  OUT UINT64  *FoundSize,
  OUT UINT64  *ScannedBytes
  )
{
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Map;
  UINTN                  MapSize;
  UINTN                  MapKey;
  UINTN                  DescriptorSize;
  UINT32                 DescriptorVersion;
  UINTN                  Index;
  UINT64                 Address;
  UINT64                 RegionEnd;
  UINT64                 Size;

  *FoundStart   = 0;
  *FoundSize    = 0;
  *ScannedBytes = 0;

  Map     = NULL;
  MapSize = 0;
  Status  = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescriptorSize, &DescriptorVersion);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: scan: GetMemoryMap sizing returned %r\n", Status));
    return FALSE;
  }

  MapSize += 16 * DescriptorSize;
  Status   = gBS->AllocatePool (EfiLoaderData, MapSize, (VOID **)&Map);
  if (EFI_ERROR (Status) || (Map == NULL)) {
    DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: scan: AllocatePool returned %r\n", Status));
    return FALSE;
  }

  Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescriptorSize, &DescriptorVersion);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: scan: GetMemoryMap returned %r\n", Status));
    gBS->FreePool (Map);
    return FALSE;
  }

  for (Index = 0; Index < MapSize / DescriptorSize; Index++) {
    EFI_MEMORY_DESCRIPTOR  *Entry;

    Entry = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Index * DescriptorSize);
    if (Entry->PhysicalStart < DRAM_BASE) {
      continue;
    }

    if (Entry->NumberOfPages < 256) {
      continue;
    }

    Address   = Entry->PhysicalStart;
    RegionEnd = Entry->PhysicalStart + (Entry->NumberOfPages << EFI_PAGE_SHIFT);

    for ( ; Address + 0x200 <= RegionEnd; Address += EFI_PAGE_SIZE) {
      CONST UINT8  *Probe;

      Probe = (CONST UINT8 *)(UINTN)Address;
      if ((Probe[0] != 0xEB) || (CompareMem (Probe + 3, "MSWIN4.1", 8) != 0)) {
        continue;
      }

      if (!FatVolumeSize (Address, &Size)) {
        continue;
      }

      DEBUG ((
        DEBUG_ERROR,
        "UNIXNURAMDISK: scan found a FAT volume at 0x%lx, %lu bytes from its BPB\n",
        Address,
        Size
        ));
      *FoundStart = Address;
      *FoundSize  = Size;
      gBS->FreePool (Map);
      return TRUE;
    }

    *ScannedBytes += RegionEnd - Entry->PhysicalStart;
  }

  gBS->FreePool (Map);
  DEBUG ((
    DEBUG_ERROR,
    "UNIXNURAMDISK: scan of %lu bytes of DRAM found no FAT volume\n",
    *ScannedBytes
    ));
  return FALSE;
}

STATIC
BOOLEAN
PublishFallback (
  VOID
  )
{
  UINT64  Start;
  UINT64  Size;
  UINT64  Scanned;

  if (!ScanDramForPayload (&Start, &Size, &Scanned)) {
    return FALSE;
  }

  PublishStatus (2, MARKER_WHITE, L"UNIXNURAMDISK: payload found by scan");
  PublishRange (Start, Start + Size);
  return TRUE;
}


EFI_STATUS
EFIAPI
UniXNURamDiskDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HOB_GUID_TYPE  *Hob;
  UINT64             Dtb;

  //
  // The firmware volume copy does not depend on anything the loader did, so it
  // is tried first. It used to sit behind an early return for a missing or
  // unparsable device tree, which meant a payload that was present in the
  // firmware volume was never looked at.
  //
  Dtb = 0;
  Hob = GetFirstGuidHob (&mEntryArgHobGuid);
  if ((Hob != NULL) && (GET_GUID_HOB_DATA_SIZE (Hob) >= sizeof (UINT64))) {
    Dtb = ReadUnaligned64 ((CONST UINT64 *)GET_GUID_HOB_DATA (Hob));
  }

  if ((Dtb != 0) && ((Dtb & 3) == 0) && (FdtCheckHeader ((VOID *)(UINTN)Dtb) == 0)) {
    DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: device tree at 0x%lx, %u bytes\n", Dtb, (UINT32)FdtTotalSize ((VOID *)(UINTN)Dtb)));
    if (PublishFallback ()) {
      return EFI_SUCCESS;
    }

    if (PublishInitrd ((VOID *)(UINTN)Dtb)) {
      return EFI_SUCCESS;
    }

    if (PublishPayloadVolume ()) {
      return EFI_SUCCESS;
    }
  } else {
    DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: x0 0x%lx is not a device tree\n", Dtb));
    if (PublishFallback ()) {
      return EFI_SUCCESS;
    }

    if (PublishPayloadVolume ()) {
      return EFI_SUCCESS;
    }
  }

  PublishStatus (3, MARKER_RED, L"UNIXNURAMDISK: no payload anywhere");
  return EFI_SUCCESS;
}
