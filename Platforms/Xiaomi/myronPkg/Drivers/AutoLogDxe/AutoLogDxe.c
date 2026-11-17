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
#include <Protocol/AdvancedLogger.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/UniXNUMenuPlugin.h>
#include <Library/AdvancedLoggerAccessLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define AUTOLOG_TICK_2S       20000000ULL

//
// Trigger policy: do not guess how long the menu countdown takes.
//
// The last log showed "menu: ... timeout 30000 ms" even though the packed
// config says 10 s, and the QR viewer started at ~49.7 s with the countdown
// frozen at "Booting Hello payload in 10 s". Run() is called synchronously
// from a timer callback, so once the viewer owns the panel the menu never
// gets to boot its default entry and the log has no LoadImage result.
//
// Instead, wait until the official Advanced Logger actually contains a line
// from UniXNUBootEntry ("boot: ...") or Main.c ("returned ..."), which proves
// the boot attempt is over. AUTOLOG_MIN_TICKS keeps the requested ~50 s
// (driver dispatched at ~25.5 s, 12 x 2 s = ~49.5 s); AUTOLOG_MAX_TICKS is
// a hard ceiling so the QR export is never skipped entirely.
//
#define AUTOLOG_MIN_TICKS     12
#define AUTOLOG_MAX_TICKS     20

#define AUTOLOG_ENABLE_QR_VIEWER  1

STATIC BOOLEAN  mStarted = FALSE;
STATIC UINTN    mTicks   = 0;

//
// Has UniXNU already attempted to boot the default entry? Read the official
// Advanced Logger buffer through the same access library the QR viewer uses.
// Both a successful LoadImage and a failure emit a "boot: ..." line; Main.c
// additionally logs "menu: entry N returned ...". Either one means the menu
// countdown is over and it is safe to take the panel.
//
STATIC
BOOLEAN
BootAttemptLogged (
  VOID
  )
{
  ADVANCED_LOGGER_ACCESS_MESSAGE_LINE_ENTRY  Line;
  EFI_STATUS                                 Status;
  BOOLEAN                                    Found;

  Found = FALSE;
  ZeroMem (&Line, sizeof (Line));
  for ( ; ;) {
    Status = AdvancedLoggerAccessLibGetNextFormattedLine (&Line);
    if ((Status == EFI_END_OF_FILE) || EFI_ERROR (Status) ||
        (Line.MessageLen == 0) || (Line.Message == NULL))
    {
      break;
    }

    if ((AsciiStrStr (Line.Message, "boot: ") != NULL) ||
        (AsciiStrStr (Line.Message, "returned ") != NULL))
    {
      Found = TRUE;
      break;
    }
  }

  AdvancedLoggerAccessLibReset (&Line);
  return Found;
}

//
// Dump the config as the running firmware actually reads it. The last log
// reported a 30 s countdown while the packed file says 10 s; this settles
// which copy the menu opened without another round of speculation.
//
STATIC
VOID
ReportConfig (
  VOID
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                       *Handles;
  UINTN                            Count;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *File;
  CHAR8                            Buffer[512];
  UINTN                            Size;
  UINTN                            Index;

  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiSimpleFileSystemProtocolGuid,
                   NULL,
                   &Count,
                   &Handles
                   );
  if (EFI_ERROR (Status) || (Count == 0) || (Handles == NULL)) {
    DEBUG ((DEBUG_ERROR, "AUTOLOG: cannot dump config, no volume (%r)\n", Status));
    return;
  }

  Status = gBS->HandleProtocol (Handles[0], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  gBS->FreePool (Handles);
  if (EFI_ERROR (Status) || (Fs == NULL)) {
    return;
  }

  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) {
    return;
  }

  Status = Root->Open (Root, &File, L"\\EFI\\UniXNU\\unixnu.tom", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    DEBUG ((DEBUG_ERROR, "AUTOLOG: config open %r\n", Status));
    return;
  }

  ZeroMem (Buffer, sizeof (Buffer));
  Size   = sizeof (Buffer) - 1;
  Status = File->Read (File, &Size, Buffer);
  File->Close (File);
  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AUTOLOG: config read %r\n", Status));
    return;
  }

  Buffer[Size] = '\0';
  for (Index = 0; Index < Size; Index++) {
    if ((Buffer[Index] == '\n') || (Buffer[Index] == '\r')) {
      Buffer[Index] = '|';
    }
  }

  DEBUG ((DEBUG_ERROR, "AUTOLOG: runtime unixnu.tom = %a\n", Buffer));
}

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

  if (++mTicks < AUTOLOG_MIN_TICKS) {
    return;
  }

  if ((mTicks < AUTOLOG_MAX_TICKS) && !BootAttemptLogged ()) {
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
  DEBUG ((DEBUG_ERROR, "AUTOLOG: trigger at tick %u (boot attempt %a)\n",
          (UINT32)mTicks, BootAttemptLogged () ? "seen" : "ceiling"));
  ReportVolumes ();
  ReportConfig ();

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
