  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

  INF Binaries/myron/EnvDxeEnhanced/EnvDxeEnhanced.inf

  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf

  INF Binaries/myron/ShmBridgeDxeLA/ShmBridgeDxeLA.inf
  INF Binaries/myron/ScmDxeCompat/ScmDxeCompat.inf
  INF Binaries/myron/TzDxeLA/TzDxeLA.inf

  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf

  INF Binaries/myron/ChipInfo/ChipInfo.inf
  INF Binaries/myron/IPCCDxe/IPCCDxe.inf
  #INF Binaries/aston/QcomPkg/Drivers/GLinkDxe/GLinkDxe.inf
  INF Binaries/myron/PlatformInfoDxeDriver/PlatformInfoDxeDriver.inf
  INF Binaries/myron/DALSys/DALSys.inf
  INF Binaries/myron/HALIOMMU/HALIOMMU.inf
  INF Binaries/myron/HWIODxeDriver/HWIODxeDriver.inf
  INF Binaries/myron/I2C/I2C.inf
  INF Binaries/myron/DALTLMM/DALTLMM.inf
  INF Binaries/myron/PmicDxe/PmicDxe.inf

  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf

  INF Binaries/myron/DiskIoDxe/DiskIoDxe.inf
  INF Binaries/myron/PartitionDxe/PartitionDxe.inf

  INF FatPkg/EnhancedFatDxe/Fat.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf

  INF Binaries/myron/UFSDxe/UFSDxe.inf
  INF Binaries/myron/CmdDbDxe/CmdDbDxe.inf
  INF Binaries/myron/PwrUtilsDxe/PwrUtilsDxe.inf
  INF Binaries/myron/RpmhDxe/RpmhDxe.inf
  INF Binaries/myron/NpaDxe/NpaDxe.inf
  INF Binaries/myron/ULogDxe/ULogDxe.inf
  INF Binaries/myron/VcsDxe/VcsDxe.inf
  INF Binaries/myron/ClockDxe/ClockDxe.inf
  INF Binaries/myron/SPMI/SPMI.inf
  INF Binaries/myron/ICBDxe/ICBDxe.inf
  INF Binaries/myron/SmemDxe/SmemDxe.inf

  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

  INF Binaries/myron/RscDxe/RscDxe.inf
  # INF Binaries/aston/QcomPkg/Drivers/OGaugeAuthDxe/OGaugeAuth.inf  # not present in the myron stock firmware

  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf

!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
  INF Binaries/myron/CPRDxe/CPRDxe.inf
!endif
  INF Binaries/myron/GpiDxe/GpiDxe.inf
  INF Binaries/myron/SPI/SPI.inf
  INF Binaries/myron/SdccDxe/SdccDxe.inf
  INF Binaries/myron/FeatureEnablerDxe/FeatureEnablerDxe.inf

!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
  INF Binaries/myron/DisplayDxe/DisplayDxe.inf
!else
  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
!endif

  INF Binaries/myron/ButtonsDxe/ButtonsDxe.inf
  INF Binaries/myron/ChargerExDxe/ChargerExDxe.inf
  # INF Binaries/aston/QcomPkg/Drivers/OcdtDxe/OplusProject.inf  # not present in the myron stock firmware
  INF Binaries/myron/PmicGlinkDxe/PmicGlinkDxe.inf
  INF Binaries/myron/QcomChargerDxeLA/QcomChargerDxeLA.inf
  INF Binaries/myron/UsbPwrCtrlDxe/UsbPwrCtrlDxe.inf
  INF Binaries/myron/TsensDxe/TsensDxe.inf
  # INF Binaries/aston/QcomPkg/Drivers/LimitsDxe/LimitsDxe.inf  # not present in the myron stock firmware
  INF Binaries/myron/DDRInfoDxe/DDRInfoDxe.inf
  INF Binaries/myron/UsbfnDwc3Dxe/UsbfnDwc3Dxe.inf
  INF Binaries/myron/XhciPciEmulation/XhciPciEmulation.inf
  INF Binaries/myron/XhciDxe/XhciDxe.inf

  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf

  INF Binaries/myron/UsbMsdDxe/UsbMsdDxe.inf
  INF Binaries/myron/UsbDeviceDxe/UsbDeviceDxe.inf
  INF Binaries/myron/UsbConfigDxe/UsbConfigDxe.inf
  INF Binaries/myron/UsbInitDxe/UsbInitDxe.inf
  INF Binaries/myron/UCDxe/UCDxe.inf
  INF Binaries/myron/RngDxe/RngDxe.inf
  INF Binaries/myron/PILDxe/PILDxe.inf
  INF Binaries/myron/PILProxyDxe/PILProxyDxe.inf
  INF Binaries/myron/SPSSDxe/SPSSDxe.inf
