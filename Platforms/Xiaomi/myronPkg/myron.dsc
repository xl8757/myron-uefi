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
  # The panel is not re-initialised: ABL leaves a live framebuffer at 0xfc800000.
  # SimpleFbDxe consumes it; a vendor display driver would own MDSS and clash
  # with the running scan-out client.
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
  # Captured MPIDRs: six cores at Aff2=0 and the two primes at Aff2=1.
  gArmPlatformTokenSpaceGuid.PcdClusterCount|2
  # SM8850 GIC addresses from the captured live device tree. Without these the
  # platform inherits the SM8550 defaults and the GIC is programmed at the wrong
  # address while interrupts are being routed.
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x17000000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x17080000

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
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1200
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2608
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

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

[Components]
  #
  # ACPI Tables
  #
  aston/AcpiTables.inf

  #
  # Boot manager and the XNU loader
  #
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
  myronPkg/Drivers/UniXNURamDiskDxe/UniXNURamDiskDxe.inf
  DarwinBootPkg/Driver/DarwinBoot.inf
