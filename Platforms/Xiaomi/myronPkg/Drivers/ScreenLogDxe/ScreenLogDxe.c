/** @file
  Mirror the Advanced Logger tail onto the framebuffer as plain text.

  The board has no UART and the UEFI console only keeps a handful of rows, so a
  driver that asserts in its constructor left us photographing five lines at a
  time and guessing at the rest. The Advanced Logger already holds the whole
  boot log - every Mu line and every vendor line - but it is only exported when
  the UniXNU menu hands the panel to the QR plugin, which is far too late for
  anything that dies during DXE dispatch.

  This driver needs nothing: no GOP, no ConIn, no menu plugin. It renders the
  tail of the log straight into the framebuffer at 0xfc800000 with the same
  8x16 font the SEC screen log uses. At 1x scale that is ~140 columns by ~160
  rows, so a single photograph carries the whole tail instead of five lines.

  It runs first in APRIORI - ahead of EnvDxeEnhanced and the rest of the vendor
  stack - and redraws on a short periodic timer. If the machine then deadlocks
  inside a vendor constructor the screen already holds everything logged up to
  that point, and further ticks fill in the rest whenever interrupts survive.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Protocol/AdvancedLogger.h>
#include <Library/AdvancedLoggerAccessLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

//
// Same geometry the SEC screen log uses (BootShim/ScreenLog.c).
//
#define FB_BASE   0xfc800000ULL
#define FB_WIDTH  1200U
#define FB_HEIGHT 2608U
#define FB_STRIDE 1200U
#define MARGIN    24U

#define CELL_W    8U
#define CELL_H    16U

#define LOG_COLS  ((FB_WIDTH - 2U * MARGIN) / CELL_W)   // 144
#define LOG_ROWS  ((FB_HEIGHT - 2U * MARGIN) / CELL_H)  // 160

#define BG        0xff102040U
#define FG        0xffffffffU
#define BANNER_FG 0xff30d0ffU

#define TICK_250MS  2500000ULL

//
// 8x16 bitmap font, ASCII 0x20-0x7e. The SEC screen log already carries one
// copy; pulling it in by relative path keeps a single source of truth.
//
#include "../../../../../BootShim/ScreenFont.h"

STATIC UINTN  mTicks      = 0;
STATIC UINTN  mLastBytes  = 0;
STATIC UINTN  mStallTicks = 0;

STATIC
VOID
PutPixel (
  IN UINT32  X,
  IN UINT32  Y,
  IN UINT32  Color
  )
{
  volatile UINT32  *Fb = (volatile UINT32 *)(UINTN)FB_BASE;

  if ((X < FB_WIDTH) && (Y < FB_HEIGHT)) {
    Fb[Y * FB_STRIDE + X] = Color;
  }
}

STATIC
VOID
ClearScreen (
  VOID
  )
{
  volatile UINT32  *Fb = (volatile UINT32 *)(UINTN)FB_BASE;
  UINT32           Index;

  for (Index = 0; Index < FB_STRIDE * FB_HEIGHT; Index++) {
    Fb[Index] = BG;
  }
}

/**
  Draw one ASCII string at a cell row/column. Unknown bytes become '?'.

**/
STATIC
VOID
DrawText (
  IN CONST CHAR8  *Text,
  IN UINT32       Row,
  IN UINT32       Col,
  IN UINT32       Color
  )
{
  UINT32  X;
  UINT32  Y;
  UINT32  GlyphRow;
  UINT32  Bit;
  UINT32  Ch;
  UINT32  Limit;

  if (Row >= LOG_ROWS) {
    return;
  }

  Y     = MARGIN + Row * CELL_H;
  X     = MARGIN + Col * CELL_W;
  Limit = LOG_COLS - Col;

  while ((*Text != '\0') && (Limit > 0)) {
    Ch = (UINT8)*Text++;
    if ((Ch < 0x20) || (Ch > 0x7e)) {
      Ch = '?';
    }

    for (GlyphRow = 0; GlyphRow < CELL_H; GlyphRow++) {
      for (Bit = 0; Bit < CELL_W; Bit++) {
        PutPixel (
          X + Bit,
          Y + GlyphRow,
          ((font8x16[Ch - 0x20][GlyphRow] & (0x80U >> Bit)) != 0) ? Color : BG
          );
      }
    }

    X += CELL_W;
    Limit--;
  }
}

/**
  Copy one Advanced Logger line into a fixed-width ASCII buffer.

  @retval TRUE   Something was copied.
  @retval FALSE  The line was empty.

**/
STATIC
BOOLEAN
CopyLine (
  IN CONST ADVANCED_LOGGER_ACCESS_MESSAGE_LINE_ENTRY  *Line,
  OUT CHAR8                                           *Buffer,
  IN UINTN                                            BufferSize
  )
{
  UINTN  Length;

  if ((Line->Message == NULL) || (Line->MessageLen == 0) || (BufferSize < 2)) {
    return FALSE;
  }

  Length = Line->MessageLen;
  if (Length > BufferSize - 1) {
    Length = BufferSize - 1;
  }

  CopyMem (Buffer, Line->Message, Length);
  Buffer[Length] = '\0';

  //
  // Drop the trailing newline the formatter leaves behind.
  //
  while ((Length > 0) && ((Buffer[Length - 1] == '\n') || (Buffer[Length - 1] == '\r'))) {
    Buffer[--Length] = '\0';
  }

  return Length > 0;
}

/**
  Redraw the tail of the Advanced Logger onto the framebuffer.

**/
STATIC
VOID
RenderLogTail (
  VOID
  )
{
  ADVANCED_LOGGER_ACCESS_MESSAGE_LINE_ENTRY  Line;
  EFI_STATUS                                 Status;
  CHAR8                                      Ring[LOG_ROWS][LOG_COLS + 1];
  UINTN                                      Count;
  UINTN                                      Head;
  UINTN                                      Index;
  UINTN                                      Row;
  UINTN                                      TotalBytes;
  CHAR8                                      Banner[96];

  ZeroMem (Ring, sizeof (Ring));
  Count     = 0;
  Head      = 0;
  TotalBytes = 0;

  ZeroMem (&Line, sizeof (Line));
  for ( ; ;) {
    Status = AdvancedLoggerAccessLibGetNextFormattedLine (&Line);
    if ((Status == EFI_END_OF_FILE) || EFI_ERROR (Status) ||
        (Line.MessageLen == 0) || (Line.Message == NULL))
    {
      break;
    }

    TotalBytes += Line.MessageLen;

    if (CopyLine (&Line, Ring[Head], sizeof (Ring[0]))) {
      Head = (Head + 1) % LOG_ROWS;
      if (Count < LOG_ROWS) {
        Count++;
      }
    }
  }

  AdvancedLoggerAccessLibReset (&Line);

  //
  // Stall detection: a frozen log means the machine stopped emitting. That is
  // the case this driver exists for, so say it out loud instead of leaving a
  // silent screen that looks the same as a healthy one.
  //
  if (TotalBytes == mLastBytes) {
    mStallTicks++;
  } else {
    mStallTicks = 0;
    mLastBytes  = TotalBytes;
  }

  ClearScreen ();

  AsciiSPrint (
    Banner,
    sizeof (Banner),
    "SCREENLOG  tick=%lu  lines=%lu  bytes=%lu  stall=%lu",
    (UINT64)mTicks,
    (UINT64)Count,
    (UINT64)TotalBytes,
    (UINT64)mStallTicks
    );
  DrawText (Banner, 0, 0, mStallTicks >= 4 ? BANNER_FG : FG);

  if (mStallTicks >= 4) {
    DrawText (">>> LOG STOPPED GROWING - this is where the machine is stuck <<<", 1, 0, BANNER_FG);
  }

  //
  // Ring[Head - Count .. Head) is chronological. Draw it from row 2 so the
  // banner never gets scrolled away.
  //
  for (Index = 0; Index < Count; Index++) {
    Row = (Head + LOG_ROWS - Count + Index) % LOG_ROWS;
    DrawText (Ring[Row], Index + 2, 0, FG);
  }
}

STATIC
VOID
EFIAPI
ScreenLogTick (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mTicks++;
  RenderLogTail ();
}

EFI_STATUS
EFIAPI
ScreenLogDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Event;

  //
  // Paint once immediately: if the very next driver deadlocks in its
  // constructor, this is the only frame that will ever reach the panel.
  //
  RenderLogTail ();

  Event = NULL;
  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  ScreenLogTick,
                  NULL,
                  &Event
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->SetTimer (Event, TimerPeriodic, TICK_250MS);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (Event);
  }

  return Status;
}
