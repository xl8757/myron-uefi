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
// Where the boot image asks ABL to place the ramdisk. The classic 0x01000000
// sits outside the DRAM this platform maps, so a device published over it would
// fault on the first read. The small 1 MiB Initrd window at 0xA6F00000 worked
// for the test volume but cannot hold a real XNU payload; the memory map
// already reserves XNU_Payload = 0xF0000000, 0x03800000 (56 MiB) for exactly
// this purpose, so point the ramdisk there and allow up to 64 MiB from the BPB.
//
#define RAMDISK_FALLBACK_BASE  0xF0000000
#define RAMDISK_FALLBACK_MAX   0x04000000

//
// The 48 MiB FAT volume is almost entirely zero, so the FFS file stores only
// its used prefix. DXE clears the XNU_Payload reservation and copies the
// prefix back before PublishFallback() reads the BPB. The whole volume then
// lives in RAM without depending on ABL, UFS or any vendor driver.
//
#define SPARSE_MAGIC           SIGNATURE_32 ('U', 'X', 'S', 'P')
#define SPARSE_HEADER_SIZE     12
#define XNU_PAYLOAD_MAX_SIZE   0x03800000ULL

STATIC CONST EFI_GUID  mSparsePayloadGuid = {
  0xB2C3D4E5, 0x6F7A, 0x4B8C, { 0x9D, 0x0E, 0x1F, 0x2A, 0x3B, 0x4C, 0x5D, 0x6E }
};

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
STATIC
BOOLEAN
UnpackSparseVolume (
  VOID
  )
{
  EFI_STATUS    Status;
  VOID          *Data;
  UINTN         Size;
  CONST UINT8   *Blob;
  UINT32        Total;
  UINT32        Used;

  Data = NULL;
  Size = 0;
  Status = GetSectionFromAnyFv (&mSparsePayloadGuid, EFI_SECTION_RAW, 0, &Data, &Size);
  if (EFI_ERROR (Status) || (Data == NULL) || (Size < SPARSE_HEADER_SIZE)) {
    DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: no sparse payload in the FV: %r\n", Status));
    return FALSE;
  }

  Blob = (CONST UINT8 *)Data;
  if (ReadUnaligned32 ((CONST UINT32 *)Blob) != SPARSE_MAGIC) {
    DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: bad sparse payload magic\n"));
    return FALSE;
  }

  Total = ReadUnaligned32 ((CONST UINT32 *)(Blob + 4));
  Used  = ReadUnaligned32 ((CONST UINT32 *)(Blob + 8));
  if ((Total == 0) || (Total > RAMDISK_FALLBACK_MAX) ||
      (Total > XNU_PAYLOAD_MAX_SIZE) || (Used > Total) ||
      ((UINTN)Used + SPARSE_HEADER_SIZE != Size))
  {
    DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: bad sparse geometry total=%u used=%u section=%u\n",
            Total, Used, (UINT32)Size));
    return FALSE;
  }

  ZeroMem ((VOID *)(UINTN)RAMDISK_FALLBACK_BASE, Total);
  CopyMem ((VOID *)(UINTN)RAMDISK_FALLBACK_BASE, Blob + SPARSE_HEADER_SIZE, Used);
  DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: restored %u/%u bytes to 0x%lx\n",
          Used, Total, (UINT64)RAMDISK_FALLBACK_BASE));
  return TRUE;
}

STATIC
BOOLEAN
PublishFallback (
  VOID
  )
{
  UINT64  Size;

  if (!FatVolumeSize (RAMDISK_FALLBACK_BASE, &Size)) {
    DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: no ramdisk volume at 0x%lx\n",
            (UINT64)RAMDISK_FALLBACK_BASE));
    return FALSE;
  }

  DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: XNU_Payload ramdisk at 0x%lx, %lu bytes from its BPB\n",
          (UINT64)RAMDISK_FALLBACK_BASE, Size));
  PublishStatus (2, MARKER_WHITE, L"UNIXNURAMDISK: payload from XNU_Payload ramdisk");
  PublishRange (RAMDISK_FALLBACK_BASE, RAMDISK_FALLBACK_BASE + Size);
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
  // Rebuild the large volume from its sparse copy in the FD first. This is the
  // only payload channel that reaches the device: ABL ignores the boot ramdisk
  // and the vendor UFS stack deadlocks without the stock boot environment.
  //
  UnpackSparseVolume ();

  //
  // Try the large ramdisk in the XNU_Payload reservation first. It is the only
  // channel that can hold a real XNU payload (kernel + trust cache + ADT);
  // the 3 MiB FD cannot. If ABL declined to place it there, fall back to the
  // small firmware-volume copy so the menu and the QR log channel still work
  // and one boot tells us which path the board actually supports.
  //
  if (PublishFallback ()) {
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: falling back to the firmware volume copy\n"));

  Dtb = 0;
  Hob = GetFirstGuidHob (&mEntryArgHobGuid);
  if ((Hob != NULL) && (GET_GUID_HOB_DATA_SIZE (Hob) >= sizeof (UINT64))) {
    Dtb = ReadUnaligned64 ((CONST UINT64 *)GET_GUID_HOB_DATA (Hob));
  }

  if ((Dtb != 0) && ((Dtb & 3) == 0) && (FdtCheckHeader ((VOID *)(UINTN)Dtb) == 0)) {
    DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: device tree at 0x%lx, %u bytes\n", Dtb, (UINT32)FdtTotalSize ((VOID *)(UINTN)Dtb)));
    if (PublishPayloadVolume ()) {
      return EFI_SUCCESS;
    }

    if (PublishInitrd ((VOID *)(UINTN)Dtb)) {
      return EFI_SUCCESS;
    }
  } else {
    DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: x0 0x%lx is not a device tree\n", Dtb));
    if (PublishPayloadVolume ()) {
      return EFI_SUCCESS;
    }
  }

  PublishStatus (3, MARKER_RED, L"UNIXNURAMDISK: no payload anywhere");
  return EFI_SUCCESS;
}
