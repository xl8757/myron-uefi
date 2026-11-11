/** @file
  路标 4 的入口：只画自己的那条带，然后立刻返回，不碰任何硬件。
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>

VOID
MarkerDraw (
  IN UINT8  Index
  );

EFI_STATUS
EFIAPI
Marker4Entry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  MarkerDraw (3);
  return EFI_SUCCESS;
}
