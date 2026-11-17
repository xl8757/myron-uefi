##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = myron
  PLATFORM_GUID                  = F365ECFF-923B-55B4-AF04-4995B26D5311
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/myronPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = myronPkg/myron.fdf
  # 0 = SimpleFbDxe uses the framebuffer ABL already left at 0xfc800000.
  #
  # With the vendor DisplayDxe the DXE stack faults inside EnvDxe at
  # 0x17422030 (a synchronous external abort), and DisplayDxe also paints the
  # console as horizontal streaks because its mode is not this panel's, which
  # makes the screen useless for reading diagnostics. SimpleFbDxe leaves the
  # bootloader framebuffer alone and prints the DXE console cleanly, so the
  # fault is legible. Getting the panel to initialise properly is a separate
  # job from getting past EnvDxe.
  USE_CUSTOM_DISPLAY_DRIVER      = 0

  #
  # 0 = SM8550-AB
  # 1 = SM8550-AC
  #
  SOC_TYPE                       = 0

!include KailuaPkg/KailuaPkg.dsc.inc

[PcdsFixedAtBuild]
  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000
  # GIC and cluster geometry are left at the platform defaults on purpose.
  #
  # These three PCDs were added here from the captured live device tree, on the
  # reasoning that the SM8850 addresses differ from the SM8550 defaults. They do
  # not: the sibling package for this exact phone (songyuanPkg) sets none of
  # them and boots, and with them set the aston EnvDxe takes a synchronous
  # external abort on its first GIC access at 0x17422030, which is inside the
  # 0x17000000 distributor range. The vendor binaries are built against the
  # defaults, so overriding them moved the programming window away from what
  # they expect.

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xA760D000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Xiaomi"
!if $(DEVICE_MODEL) == 0
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Redmi K90 Pro Max"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"myron"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"25102RKBEC"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"d22ap"
!elseif $(DEVICE_MODEL) == 1
  # The same board is sold as the Poco F8 Ultra in some markets.
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Poco F8 Ultra"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"myron"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"25102RKBEC"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"d22ap"
!endif

  #
  # Simple Frame Buffer
  #
  # This phone's panel: 1200x2608 with a 4800-byte stride (SurfaceFlinger).
  #
  # The panel itself is 1200x2608 (SurfaceFlinger) with a 4800-byte stride, and
  # FINDINGS.md records the firmware GOP as 1220x2656, so these numbers are not
  # the panel's. They are what the vendor DisplayDxe needs the PCDs to say: with
  # 1200x2608 it takes a synchronous external abort at 0x17422030 inside EnvDxe
  # before the panel is up, and with 1264x2780 it initialises and draws.
  #
  # The consequence is a mis-scaled console: the text renders as horizontal
  # streaks covering the whole panel, which is the geometry mismatch showing up
  # as presentation rather than as a fault. Fixing that means teaching
  # DisplayDxe this panel's real mode - a separate job from getting the stack to
  # run, and the stack running is what this value buys.
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1200
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2608
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  #
  # Debug. SiliciumPkg sets 0x2F for DEBUG, where 0x20 is
  # DEBUG_PROPERTY_ASSERT_DEADLOOP_ENABLED: a stock vendor driver that returns
  # EFI_NOT_FOUND halts the whole boot with no way back but a power cycle. Keep
  # asserts and print output, drop the deadloop so the dispatcher continues.
  #
!if $(TARGET) == DEBUG
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x0F
!endif

  #
  # The screen is the only output channel on this board, and it holds about
  # fifty lines. The DXE core's pool/page tracing alone fills a whole page
  # before the first driver starts, which pushes the interesting part off the
  # top. Keep errors, warnings, the driver load lines and DEBUG(): that is
  # 0x80000000 | 0x40 | 0x04 | 0x02, and drop the allocation noise.
  #
!if $(TARGET) == DEBUG
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000046
!endif

  #
  # Platform PEI
  #
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

[LibraryClasses]
  #
  # SEC handoff. The KailuaPkg default calls ArmSmmuDetach (MdpStreams), which
  # clears the translations of the display and firmware clients ABL left
  # running. This override keeps that state and adds the DEBUG screen logger.
  #
  PlatformSecLib|myronPkg/Library/PlatformSecLib/PlatformSecLib.inf

  #
  # Memory Libraries
  #
  MemoryMapLib|myronPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # QCOM Libraries
  #
  ConfigurationMapLib|myronPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf

  #
  # UniXNU
  #
  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf

  #
  # Advanced Logger (official UniXNU Tools -> Advanced Logger QR export).
  # These are new library classes; none overrides an existing mapping.
  #
  AssertLib|AdvLoggerPkg/Library/AssertLib/AssertLib.inf
  AdvancedLoggerHdwPortLib|AdvLoggerPkg/Library/AdvancedLoggerHdwPortLibNull/AdvancedLoggerHdwPortLibNull.inf
  AdvancedLoggerAccessLib|AdvLoggerPkg/Library/AdvancedLoggerAccessLib/AdvancedLoggerAccessLib.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  QrEncoderLib|MsGraphicsPkg/Library/QrEncoderLib/QrEncoderLib.inf


#
# Advanced Logger, DXE Only mode (AdvLoggerPkg/Docs/ReadMe.md).
# Only DXE-phase modules route DEBUG() into the in-memory log; SEC keeps its
# serial/screen logger so the earliest boot path is untouched.
# The NULL class is the Mu-standard way to link the DxeCore instance into
# DxeMain.inf, which otherwise does not list AdvancedLoggerLib. Its constructor
# allocates the buffer and installs gAdvancedLoggerProtocolGuid.
#
[LibraryClasses.common.DXE_CORE]
  # DxeMain.inf does not list AdvancedLoggerLib, so the NULL class is the only
  # way to link the DxeCore instance. Keep DxeCore's own DebugLib unchanged:
  # its constructor allocates the buffer and installs the protocol, while the
  # modules we actually want to hear from are the DXE drivers below.
  NULL|AdvLoggerPkg/Library/AdvancedLoggerLib/DxeCore/AdvancedLoggerLib.inf
  VariablePolicyHelperLib|MdeModulePkg/Library/VariablePolicyHelperLib/VariablePolicyHelperLib.inf

[LibraryClasses.common.DXE_DRIVER, LibraryClasses.common.UEFI_DRIVER, LibraryClasses.common.UEFI_APPLICATION]
  DebugLib|AdvLoggerPkg/Library/BaseDebugLibAdvancedLogger/BaseDebugLibAdvancedLogger.inf
  AdvancedLoggerLib|AdvLoggerPkg/Library/AdvancedLoggerLib/Dxe/AdvancedLoggerLib.inf

[LibraryClasses.common.DXE_RUNTIME_DRIVER]
  DebugLib|AdvLoggerPkg/Library/BaseDebugLibAdvancedLogger/BaseDebugLibAdvancedLogger.inf
  AdvancedLoggerLib|AdvLoggerPkg/Library/AdvancedLoggerLib/Runtime/AdvancedLoggerLib.inf

[PcdsFeatureFlag.common]
  gAdvLoggerPkgTokenSpaceGuid.PcdAdvancedLoggerLocator|FALSE
  gAdvLoggerPkgTokenSpaceGuid.PcdAdvancedLoggerFixedInRAM|FALSE
  gAdvLoggerPkgTokenSpaceGuid.PcdAdvancedLoggerAutoWrapEnable|TRUE

[PcdsFixedAtBuild.common]
  gAdvLoggerPkgTokenSpaceGuid.PcdAdvancedLoggerBase|0x0
  gAdvLoggerPkgTokenSpaceGuid.PcdAdvancedLoggerPages|1024
  gAdvLoggerPkgTokenSpaceGuid.PcdAdvancedLoggerPreMemPages|24
  gAdvLoggerPkgTokenSpaceGuid.PcdAdvancedLoggerHdwPortDebugPrintErrorLevel|0xFFFFFFFF
  gUniXNUPkgTokenSpaceGuid.PcdUniXNUDeviceModel|"myron"

  #
  # The payload volume is a hand-built FAT12 image with 8.3 entries only and
  # no long-file-name records. The default path ends in ".toml" (4-char
  # extension): EnhancedFatDxe's FatCheckIs8Dot3Name() then marks it as a long
  # name and searches LFN entries that do not exist, so UniXNUConfigLoad()
  # never opens the config and reports "no boot entry found". A 3-char
  # extension is a legal 8.3 short name and matches the on-volume UNIXNU.TOM
  # entry directly (FAT name comparison is case-insensitive).
  #
  gUniXNUPkgTokenSpaceGuid.PcdUniXNUConfigPath|L"\\EFI\\UniXNU\\unixnu.tom"

[Components]
  #
  # Device tree. Mu derives the DtbExtension address by scanning the stock UEFI
  # image in "FD Reserved", which does not exist when the payload comes from
  # "fastboot boot". This PEIM republishes the tree ABL passed in x0 instead.
  #

  #
  # ACPI Tables
  #
  aston/AcpiTables.inf

  #
  # Boot manager and the XNU loader
  #
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
  UniXNUPkg/Driver/AdvancedLogDxe/AdvancedLogDxe.inf
  myronPkg/Drivers/AutoLogDxe/AutoLogDxe.inf
  myronPkg/Drivers/UniXNURamDiskDxe/UniXNURamDiskDxe.inf
  # 只编译不打包：这个 EFI 是 payload 卷里的启动条目，用来验证
  # 「菜单 -> 条目 -> 交接」这条链，不需要进固件卷。
  UniXNUPkg/Test/HelloPayload/HelloPayload.inf
  DarwinBootPkg/Driver/DarwinBoot.inf
