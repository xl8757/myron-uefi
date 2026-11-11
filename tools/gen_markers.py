#!/usr/bin/env python3
"""生成 4 个「路标」DXE 驱动。

目的：这台机器没有 UART，屏幕在 BDS 之前也没有控制台输出，固件复位时什么信息
都留不下。于是让每个路标在启动流程的不同位置，往 ABL 留下的 framebuffer
（0xFC800000，1200x2608，stride 4800）画一条全宽横带：

    路标 1  红色   DXE 核心 + PCD 起来之后
    路标 2  绿色   电源/时钟/IPCC 那批厂商驱动之后
    路标 3  蓝色   BdsDxe 之后（进入启动管理器）
    路标 4  白色   整个 APRIORI 清单跑完

屏幕上累积了几条带，就说明固件走到了哪一段。写 framebuffer 前先用
GetMemoryMap 确认该地址确实在内存映射里，避免 MMU 未映射导致 data abort
（那样会把我们自己的探针当成复位点，白跑一轮）。

用法:
    python tools/gen_markers.py <myronPkg 目录>
"""

import sys
from pathlib import Path

MARKER_C = r'''/** @file
  路标驱动：往 ABL 留下的 framebuffer 上画一条横带。

  这台机器没有 UART，固件复位时不留下任何信息。这里的做法是在启动流程的不同
  位置各放一个路标，每个画一条不同颜色、不同纵向位置的横带，屏幕上累积了几条
  就说明走过了几个位置。
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

// ABL 留下的 splash / framebuffer（见 FINDINGS：实测可写、按此几何显示正确）
#define FB_BASE    0x00000000FC800000ULL
#define FB_WIDTH   1200U
#define FB_STRIDE  4800U   // 1200 * 4，字节
#define FB_HEIGHT  2608U

// 每个路标一条全宽横带：y = 80 + index * 140，高 100
#define BAND_TOP(i)   (80U + (UINT32)(i) * 140U)
#define BAND_HEIGHT   100U

// 红 / 绿 / 蓝 / 白，四条一眼能数清
STATIC CONST UINT32  mBandColor[4] = {
  0x00FF0000U, 0x0000FF00U, 0x000000FFU, 0x00FFFFFFU
};

/**
  确认 framebuffer 落在 EFI 内存映射里。

  没确认就写，一旦这段没被 identity map，会 data abort —— 那就成了我们自己的
  探针制造复位，实验结论全错。
**/
STATIC
BOOLEAN
FrameBufferIsMapped (
  VOID
  )
{
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Map;
  UINTN                  MapSize;
  UINTN                  MapKey;
  UINTN                  DescSize;
  UINT32                 DescVersion;
  UINTN                  Offset;

  MapSize = 0;
  Map     = NULL;
  Status  = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVersion);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return FALSE;
  }

  MapSize += 4 * DescSize;
  Status   = gBS->AllocatePool (EfiBootServicesData, MapSize, (VOID **)&Map);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVersion);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  for (Offset = 0; Offset + DescSize <= MapSize; Offset += DescSize) {
    EFI_MEMORY_DESCRIPTOR  *Desc;
    EFI_PHYSICAL_ADDRESS   End;

    Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
    End  = Desc->PhysicalStart + LShiftU64 (Desc->NumberOfPages, EFI_PAGE_SHIFT);
    if ((FB_BASE >= Desc->PhysicalStart) && (FB_BASE + 0x1000ULL <= End)) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  画一条横带。Index 0..3。
**/
VOID
MarkerDraw (
  IN UINT8  Index
  )
{
  volatile UINT32  *Row;
  UINT32           X;
  UINT32           Y;
  UINT32           Top;
  UINT32           Color;

  if (Index > 3) {
    return;
  }

  if (!FrameBufferIsMapped ()) {
    DEBUG ((DEBUG_ERROR, "Marker %d: framebuffer 不在内存映射里，跳过\n", Index));
    return;
  }

  Color = mBandColor[Index];
  Top   = BAND_TOP (Index);
  if (Top + BAND_HEIGHT > FB_HEIGHT) {
    return;
  }

  for (Y = Top; Y < Top + BAND_HEIGHT; Y++) {
    Row = (volatile UINT32 *)(UINTN)(FB_BASE + (UINT64)Y * FB_STRIDE);
    for (X = 0; X < FB_WIDTH; X++) {
      Row[X] = Color;
    }
  }

  DEBUG ((DEBUG_INFO, "Marker %d drawn at y=%u\n", Index, Top));
}
'''

ENTRY_C = r'''/** @file
  路标 {n} 的入口：只画自己的那条带，然后立刻返回，不碰任何硬件。
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>

VOID
MarkerDraw (
  IN UINT8  Index
  );

EFI_STATUS
EFIAPI
Marker{n}Entry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  MarkerDraw ({idx});
  return EFI_SUCCESS;
}
'''

INF_TMPL = """[Defines]
  INF_VERSION                    = 0x0001001B
  BASE_NAME                      = Marker{n}Dxe
  FILE_GUID                      = 7A3{n}C4E1-5B2{n}-4D8{n}-9E1{n}-A6{n}B3C7D2E{n}F
  MODULE_TYPE                    = DXE_DRIVER
  VERSION_STRING                 = 1.0
  ENTRY_POINT                    = Marker{n}Entry

[Sources]
  Marker.c
  Marker{n}.c

[Packages]
  MdePkg/MdePkg.dec

[LibraryClasses]
  BaseLib
  BaseMemoryLib
  DebugLib
  UefiBootServicesTableLib
  UefiDriverEntryPoint

[Depex]
  TRUE
"""


def main() -> int:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = Path(sys.argv[1]) / "Drivers" / "MarkerDxe"
    root.mkdir(parents=True, exist_ok=True)

    (root / "Marker.c").write_text(MARKER_C, encoding="utf-8")

    for n, idx in ((1, 0), (2, 1), (3, 2), (4, 3)):
        (root / ("Marker%d.c" % n)).write_text(
            ENTRY_C.replace("{n}", str(n)).replace("{idx}", str(idx)), encoding="utf-8")
        (root / ("Marker%d.inf" % n)).write_text(
            INF_TMPL.replace("{n}", str(n)), encoding="utf-8")

    for f in sorted(root.iterdir()):
        print("  ", f.name, f.stat().st_size)
    return 0


if __name__ == "__main__":
    sys.exit(main())
