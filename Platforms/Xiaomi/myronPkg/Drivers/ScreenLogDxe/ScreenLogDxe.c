/** @file
  Put the whole boot log on the framebuffer, whatever produced it.

  The board has no UART, so a driver that asserts inside a library constructor
  left us photographing five console rows and guessing at the rest. Worse, the
  two log channels do not overlap: Mu's DEBUG goes to the Advanced Logger only
  (a scan of a captured log shows zero "InstallProtocolInterface" lines), while
  the vendor binaries write straight to ConOut. Reading one of them explains
  half the boot and none of the failure.

  This driver merges both into one ring and paints it at 1x scale with the SEC
  font - about 140 columns by 160 rows - straight into 0xfc800000. It needs no
  GOP, no ConIn and no menu plugin, and APRIORI runs it ahead of EnvDxeEnhanced,
  so when that driver deadlocks in its constructor the panel already holds every
  line logged up to that point. A 250 ms tick keeps filling it in whenever
  interrupts survive the deadlock.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Protocol/AdvancedLogger.h>
#include <Protocol/SimpleTextOut.h>
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
#define HOOK_FG   0xff80ff80U

#define TICK_250MS  2500000ULL

//
// Shared with the SEC screen log by relative include - one font, one source.
//
#include "../../../../../BootShim/ScreenFont.h"

//
// Merged line ring. Fixed size on purpose: this driver must not allocate
// before the machine has proved it can survive long enough to need memory.
//
#define RING_LINES  256

STATIC CHAR8  mRing[RING_LINES][LOG_COLS + 1];
STATIC UINTN  mRingHead;
STATIC UINTN  mRingCount;
STATIC UINTN  mTotalLines;

STATIC UINTN             mTicks;
STATIC UINTN             mStallTicks;
STATIC UINTN             mLoggerLinesRead;
STATIC EFI_SYSTEM_TABLE  *mSystemTable;

//
// ConOut capture. The vendor DebugLib is prebuilt and writes here; nothing in
// the Advanced Logger ever sees it.
//
STATIC EFI_TEXT_STRING                    mOrigOutputString;
STATIC EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL    *mHookedConOut;
STATIC CHAR8                              mPending[LOG_COLS + 1];
STATIC UINTN                              mPendingLen;

STATIC
VOID
PushLine (
  IN CONST CHAR8  *Text,
  IN UINTN        Length
  )
{
  if (Length == 0) {
    return;
  }

  if (Length > LOG_COLS) {
    Length = LOG_COLS;
  }

  CopyMem (mRing[mRingHead], Text, Length);
  mRing[mRingHead][Length] = '\0';

  mRingHead = (mRingHead + 1) % RING_LINES;
  if (mRingCount < RING_LINES) {
    mRingCount++;
  }

  mTotalLines++;
}

STATIC
VOID
FlushPending (
  VOID
  )
{
  while ((mPendingLen > 0) &&
         ((mPending[mPendingLen - 1] == '\n') || (mPending[mPendingLen - 1] == '\r')))
  {
    mPending[--mPendingLen] = '\0';
  }

  PushLine (mPending, mPendingLen);
  mPendingLen = 0;
}

/**
  ConOut wrapper: tee every console string into the ring, then forward it.

  The vendor assert happens at an arbitrary TPL inside a library constructor,
  so this must stay allocation-free and must never fail loudly.

**/
STATIC
EFI_STATUS
EFIAPI
HookedOutputString (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN CHAR16                           *String
  )
{
  CHAR16  Ch;
  CHAR16  *Walk;

  Walk = String;
  if (Walk != NULL) {
    while ((Ch = *Walk++) != L'\0') {
      if (Ch == L'\n') {
        FlushPending ();
      } else if (Ch == L'\r') {
        //
        // ConOut sends "\r\n"; the \n does the real work.
        //
        continue;
      } else if (mPendingLen < LOG_COLS) {
        if (Ch > 0x7e) {
          Ch = L'?';
        }

        mPending[mPendingLen++] = (CHAR8)Ch;
      }
    }
  }

  //
  // Forward the untouched pointer: the walk above only advanced a copy.
  //
  return mOrigOutputString (This, String);
}

STATIC
VOID
HookConOut (
  VOID
  )
{
  if ((mHookedConOut != NULL) || (mSystemTable == NULL) ||
      (mSystemTable->ConOut == NULL) || (mSystemTable->ConOut->OutputString == NULL))
  {
    return;
  }

  mHookedConOut     = mSystemTable->ConOut;
  mOrigOutputString = mSystemTable->ConOut->OutputString;
  mSystemTable->ConOut->OutputString = HookedOutputString;

  PushLine ("[screenlog] hooked ConOut -> vendor output is being captured", 58);
}

//
// Pixel and text primitives. 1x scale: 144 columns by 160 rows fit the panel.
//
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
  Pull whatever is new in the Advanced Logger into the same ring.

  Only Mu's own DEBUG lands there; the vendor console traffic is captured by
  HookedOutputString. Merging both is the only way to see one coherent boot.

**/
STATIC
VOID
IngestAdvancedLogger (
  VOID
  )
{
  ADVANCED_LOGGER_ACCESS_MESSAGE_LINE_ENTRY  Line;
  EFI_STATUS                                 Status;
  UINTN                                      Length;
  UINTN                                      Seen;

  //
  // Restart from the top and skip what is already in the ring. The accessor
  // keeps its own cursor; resetting keeps every tick deterministic instead of
  // depending on where the previous tick stopped.
  //
  ZeroMem (&Line, sizeof (Line));
  AdvancedLoggerAccessLibReset (&Line);
  Seen = 0;

  for ( ; ;) {
    Status = AdvancedLoggerAccessLibGetNextFormattedLine (&Line);
    if ((Status == EFI_END_OF_FILE) || EFI_ERROR (Status) ||
        (Line.MessageLen == 0) || (Line.Message == NULL))
    {
      break;
    }

    Seen++;
    if (Seen <= mLoggerLinesRead) {
      continue;
    }

    Length = Line.MessageLen;
    while ((Length > 0) &&
           ((Line.Message[Length - 1] == '\n') || (Line.Message[Length - 1] == '\r')))
    {
      Length--;
    }

    PushLine ((CONST CHAR8 *)Line.Message, Length);
    mLoggerLinesRead = Seen;
  }

  AdvancedLoggerAccessLibReset (&Line);
}

/**
  Append helpers for the banner. Deliberately not AsciiSPrint: this driver
  must link with nothing beyond BaseLib and the logger accessor, and a missing
  PrintLib mapping in the DSC is not worth a build round-trip.

**/
STATIC
UINTN
AppendStr (
  OUT CHAR8        *Buffer,
  IN  UINTN        Capacity,
  IN  UINTN        At,
  IN  CONST CHAR8  *Text
  )
{
  while ((*Text != '\0') && (At + 1 < Capacity)) {
    Buffer[At++] = *Text++;
  }

  return At;
}

STATIC
UINTN
AppendNum (
  OUT CHAR8  *Buffer,
  IN  UINTN  Capacity,
  IN  UINTN  At,
  IN  UINT64 Value
  )
{
  CHAR8  Digits[24];
  UINTN  Count;

  Count = 0;
  do {
    Digits[Count++] = (CHAR8)('0' + (Value % 10));
    Value /= 10;
  } while ((Value != 0) && (Count < sizeof (Digits)));

  while ((Count > 0) && (At + 1 < Capacity)) {
    Buffer[At++] = Digits[--Count];
  }

  return At;
}

STATIC
VOID
Render (
  VOID
  )
{
  UINTN   Index;
  UINTN   Row;
  UINTN   Source;
  UINTN   At;
  CHAR8   Banner[128];
  UINT32  BodyRows;

  ClearScreen ();

  At = 0;
  At = AppendStr (Banner, sizeof (Banner), At, "SCREENLOG tick=");
  At = AppendNum (Banner, sizeof (Banner), At, mTicks);
  At = AppendStr (Banner, sizeof (Banner), At, " lines=");
  At = AppendNum (Banner, sizeof (Banner), At, mRingCount);
  At = AppendStr (Banner, sizeof (Banner), At, " total=");
  At = AppendNum (Banner, sizeof (Banner), At, mTotalLines);
  At = AppendStr (Banner, sizeof (Banner), At, " logger=");
  At = AppendNum (Banner, sizeof (Banner), At, mLoggerLinesRead);
  At = AppendStr (Banner, sizeof (Banner), At, " stall=");
  At = AppendNum (Banner, sizeof (Banner), At, mStallTicks);
  At = AppendStr (Banner, sizeof (Banner), At, " conout=");
  At = AppendStr (Banner, sizeof (Banner), At,
                   (mHookedConOut != NULL) ? "hooked" : "waiting");
  Banner[At] = '\0';

  DrawText (Banner, 0, 0, mStallTicks >= 8 ? BANNER_FG : FG);

  if (mStallTicks >= 8) {
    DrawText (">>> LOG STOPPED GROWING - this is where the machine is stuck <<<", 1, 0, BANNER_FG);
    BodyRows = LOG_ROWS - 3;
  } else {
    BodyRows = LOG_ROWS - 2;
  }

  for (Index = 0; Index < mRingCount && Index < BodyRows; Index++) {
    Source = (mRingHead + RING_LINES - mRingCount + Index) % RING_LINES;
    Row    = Index + 2;
    DrawText (mRing[Source], Row, 0, FG);
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
  UINTN  Before;

  mTicks++;
  HookConOut ();

  //
  // mTotalLines covers both channels, so "no new lines anywhere" is the real
  // signal that the machine stopped emitting.
  //
  Before = mTotalLines;
  IngestAdvancedLogger ();

  if (mTotalLines == Before) {
    mStallTicks++;
  } else {
    mStallTicks = 0;
  }

  Render ();
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

  mSystemTable = SystemTable;

  //
  // Paint once immediately: if the very next driver deadlocks in its
  // constructor, this is the only frame that will ever reach the panel.
  //
  HookConOut ();
  IngestAdvancedLogger ();
  Render ();

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
