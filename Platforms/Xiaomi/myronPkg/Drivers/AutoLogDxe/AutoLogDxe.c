/** @file
  Start the official AdvancedLogDxe QR viewer when the board has no ConIn.

  This board boots with vendor=none, so BDS reports
  "[Bds] Connect ConIn failed - Not Found!!" and neither the volume nor the
  power key produces input. UniXNU's Tools submenu has Timeout = -1 and starts
  on Back, so the official plugin can never be selected by a user.

  The official plugin protocol is a plain Run() callback. Once GOP and the
  plugin protocol are both present, and the 10 s main-menu countdown has
  elapsed, call Run() directly. Everything the viewer does -- reading the
  Advanced Logger buffer, OneSend framing, QR encoding, rotation -- stays in
  the official UniXNUPkg/Driver/AdvancedLogDxe; this driver only triggers it.

  DrawStatus is passed as NULL: the official viewer guards every use of it,
  and UniXNU keeps ownership of the footer. With no ConIn its event loop waits
  on the timer only, so the QR frames rotate indefinitely and can be scanned
  at leisure.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/UniXNUMenuPlugin.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define AUTOLOG_TICK_2S       20000000ULL
#define AUTOLOG_DELAY_TICKS   7   /* 14 s: let the 10 s menu countdown finish */

STATIC BOOLEAN  mStarted = FALSE;
STATIC UINTN    mTicks   = 0;

//
// One-shot snapshot of the file-system view, printed into the official Advanced
// Logger just before the QR viewer starts. With the log channel finally usable
// this answers "is there a SimpleFileSystem at all" and "does the 8.3 config
// path open", without spending another boot on guessing.
//
STATIC
VOID
ReportVolumes (
  VOID
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                       *Handles;
  UINTN                            Count;
  UINTN                            Index;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *File;

  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiSimpleFileSystemProtocolGuid,
                   NULL,
                   &Count,
                   &Handles
                   );
  DEBUG ((DEBUG_ERROR, "AUTOLOG: SimpleFileSystem handle count %u (%r)\n", (UINT32)Count, Status));
  if (EFI_ERROR (Status) || (Count == 0)) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiSimpleFileSystemProtocolGuid,
                    (VOID **)&Fs
                    );
    if (EFI_ERROR (Status) || (Fs == NULL)) {
      continue;
    }

    Status = Fs->OpenVolume (Fs, &Root);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "AUTOLOG: volume %u OpenVolume %r\n", (UINT32)Index, Status));
      continue;
    }

    Status = Root->Open (Root, &File, L"\\EFI\\UniXNU\\unixnu.tom", EFI_FILE_MODE_READ, 0);
    DEBUG ((DEBUG_ERROR, "AUTOLOG: volume %u open unixnu.tom %r\n", (UINT32)Index, Status));
    if (!EFI_ERROR (Status)) {
      File->Close (File);
    }

    Root->Close (Root);
  }

  gBS->FreePool (Handles);
}

STATIC
VOID
EFIAPI
AutoLogTick (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_HANDLE                    *Handles;
  UINTN                         Count;
  UNIXNU_MENU_PLUGIN_PROTOCOL   *Plugin;
  UNIXNU_PLUGIN_RECT            Content;
  UINT32                        Side;

  if (mStarted) {
    return;
  }

  if (++mTicks < AUTOLOG_DELAY_TICKS) {
    return;
  }

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR (Status) || (Gop == NULL) || (Gop->Mode == NULL) || (Gop->Mode->Info == NULL)) {
    return;
  }

  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gUniXNUMenuPluginProtocolGuid,
                   NULL,
                   &Count,
                   &Handles
                   );
  if (EFI_ERROR (Status) || (Count == 0) || (Handles == NULL)) {
    if (Handles != NULL) {
      gBS->FreePool (Handles);
    }

    DEBUG ((DEBUG_ERROR, "AUTOLOG: no menu plugin installed yet\n"));
    return;
  }

  Status = gBS->HandleProtocol (Handles[0], &gUniXNUMenuPluginProtocolGuid, (VOID **)&Plugin);
  gBS->FreePool (Handles);
  if (EFI_ERROR (Status) || (Plugin == NULL) || (Plugin->Run == NULL) ||
      (Plugin->Signature != UNIXNU_MENU_PLUGIN_PROTOCOL_SIGNATURE) ||
      (Plugin->Version != UNIXNU_MENU_PLUGIN_PROTOCOL_VERSION))
  {
    return;
  }

  mStarted = TRUE;
  gBS->SetTimer (Event, TimerCancel, 0);

  Side = MIN (Gop->Mode->Info->HorizontalResolution, Gop->Mode->Info->VerticalResolution);
  Content.Width  = Side;
  Content.Height = Side;
  Content.X      = (Gop->Mode->Info->HorizontalResolution - Side) / 2;
  Content.Y      = (Gop->Mode->Info->VerticalResolution - Side) / 2;

  ReportVolumes ();
  DEBUG ((DEBUG_ERROR, "AUTOLOG: starting official QR log viewer (%ux%u)\n",
          Content.Width, Content.Height));
  Plugin->Run (Plugin, Gop, &Content, NULL);
}

EFI_STATUS
EFIAPI
AutoLogDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Event;

  Event = NULL;
  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  AutoLogTick,
                  NULL,
                  &Event
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->SetTimer (Event, TimerPeriodic, AUTOLOG_TICK_2S);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (Event);
  }

  return Status;
}
