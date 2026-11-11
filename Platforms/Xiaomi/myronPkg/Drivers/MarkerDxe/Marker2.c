/** @file
  路标 2 的入口：只画自己的那条带，然后立刻返回，不碰任何硬件。
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>

VOID
MarkerDraw (
  IN UINT8  Index
  );

EFI_STATUS
EFIAPI
Marker2Entry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  MarkerDraw (1);
  return EFI_SUCCESS;
}
