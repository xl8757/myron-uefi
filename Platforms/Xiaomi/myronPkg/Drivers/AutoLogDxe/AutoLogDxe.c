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

//
// Timing, measured from the last complete firmware log:
//   this driver is dispatched at ~25.5 s after power on,
//   the UniXNU menu appears at ~29.5 s,
//   the payload config sets a 10 s countdown, so the default boot entry is
//   attempted at ~39.5 s and its result is already in the log.
//
// 12 ticks x 2 s = 24 s after dispatch -> QR frames start at ~49.5 s, i.e.
// "fifty seconds". Starting the viewer earlier would block the menu countdown
// (Run() is called synchronously from a timer callback and only exits on a key
// press, which this board cannot produce), and the boot attempt would never be
// logged. Waiting until after it means one boot captures the whole story.
//
#define AUTOLOG_DELAY_TICKS   12

//
// The official viewer's event loop only exits on a key press. This board has
// no ConIn, so it rotates frames indefinitely -- which is exactly what we
// want for scanning, now that the menu has already had its turn.
//
#define AUTOLOG_ENABLE_QR_VIEWER  1

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
#if AUTOLOG_ENABLE_QR_VIEWER
  UNIXNU_PLUGIN_RECT            Content;
  UINT32                        Side;
#endif

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

  //
  // One-shot, non-blocking volume snapshot. It returns immediately, so the
  // menu keeps ownership of the screen and its countdown can finish.
  //
  ReportVolumes ();

#if AUTOLOG_ENABLE_QR_VIEWER
  Side = MIN (Gop->Mode->Info->HorizontalResolution, Gop->Mode->Info->VerticalResolution);
  Content.Width  = Side;
  Content.Height = Side;
  Content.X      = (Gop->Mode->Info->HorizontalResolution - Side) / 2;
  Content.Y      = (Gop->Mode->Info->VerticalResolution - Side) / 2;

  DEBUG ((DEBUG_ERROR, "AUTOLOG: starting official QR log viewer (%ux%u)\n",
          Content.Width, Content.Height));
  Plugin->Run (Plugin, Gop, &Content, NULL);
#else
  (VOID)Plugin;
  (VOID)Gop;
  DEBUG ((DEBUG_ERROR, "AUTOLOG: QR viewer disabled, leaving menu countdown intact\n"));
#endif
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
