// Copyright (c) 2012-2013, ARM Limited.
// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <AsmMacroLib.h>

ASM_FUNC (PlatformAssemblyInitialize)
#ifndef MDEPKG_NDEBUG
  // A logger needs a valid stack even before the common SEC stack setup.
  MOV64 (x0, FixedPcdGet64 (PcdCPUCoresStackBase))
  MOV32 (x1, FixedPcdGet32 (PcdCPUCorePrimaryStackSize))
  add x0, x0, x1
  mov sp, x0
  b myron_screen_sec_entry
#else
  ret
#endif
