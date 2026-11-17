APRIORI DXE {
  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

  # DtbExtension 必须先于厂商驱动发布：EnvDxeEnhanced 下一个就要把它读出来
  # 交给整个厂商栈，晚一步这一轮就白跑。
  INF myronPkg/Drivers/DtbExtensionHobDxe/DtbExtensionHobDxe.inf

  INF Binaries/myron/EnvDxeEnhanced/EnvDxeEnhanced.inf

  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf

  INF Binaries/myron/SmemDxe/SmemDxe.inf
  INF Binaries/myron/DALSys/DALSys.inf
  INF Binaries/myron/HWIODxeDriver/HWIODxeDriver.inf
  INF Binaries/myron/ChipInfo/ChipInfo.inf
  INF Binaries/myron/PlatformInfoDxeDriver/PlatformInfoDxeDriver.inf
  INF Binaries/myron/HALIOMMU/HALIOMMU.inf
  INF Binaries/myron/ULogDxe/ULogDxe.inf
  INF Binaries/myron/IPCCDxe/IPCCDxe.inf
  INF Binaries/myron/GlinkDxe/GlinkDxe.inf
  INF Binaries/myron/CmdDbDxe/CmdDbDxe.inf
  INF Binaries/myron/MailboxDxe/MailboxDxe.inf
  INF Binaries/myron/QcomScmiDxe/QcomScmiDxe.inf
  INF Binaries/myron/Qup/Qup.inf
  INF Binaries/myron/I3CMaster/I3CMaster.inf
  INF Binaries/myron/PsStateDxe/PsStateDxe.inf
  INF Binaries/myron/EXFG/EXFG.inf
  INF Binaries/myron/PwrUtilsDxe/PwrUtilsDxe.inf
  INF Binaries/myron/NpaDxe/NpaDxe.inf
  INF Binaries/myron/RpmhDxe/RpmhDxe.inf
  INF Binaries/myron/VcsDxe/VcsDxe.inf
  INF Binaries/myron/ClockDxe/ClockDxe.inf
  INF Binaries/myron/ICBDxe/ICBDxe.inf
  INF Binaries/myron/ShmBridgeDxeLA/ShmBridgeDxeLA.inf
  INF Binaries/myron/ScmDxeCompat/ScmDxeCompat.inf
  INF Binaries/myron/DALTLMM/DALTLMM.inf
  INF Binaries/myron/I2C/I2C.inf
  INF Binaries/myron/SPMI/SPMI.inf

  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf

  INF Binaries/myron/PmicDxe/PmicDxe.inf
  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf

  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf

  INF Binaries/myron/SdccDxe/SdccDxe.inf
  INF Binaries/myron/UFSDxe/UFSDxe.inf

  INF FatPkg/EnhancedFatDxe/Fat.inf

  INF Binaries/myron/TzDxeLA/TzDxeLA.inf

  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf

  INF Binaries/myron/DDRInfoDxe/DDRInfoDxe.inf
  # INF Binaries/aston/QcomPkg/Drivers/OcdtDxe/OplusProject.inf  # not present in the myron stock firmware
  INF Binaries/myron/FeatureEnablerDxe/FeatureEnablerDxe.inf
  INF Binaries/myron/PILProxyDxe/PILProxyDxe.inf
  INF Binaries/myron/PILDxe/PILDxe.inf
  INF Binaries/myron/SPSSDxe/SPSSDxe.inf

  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

  INF Binaries/myron/SPI/SPI.inf
  INF Binaries/myron/PmicGlinkDxe/PmicGlinkDxe.inf
  INF Binaries/myron/UsbPwrCtrlDxe/UsbPwrCtrlDxe.inf
  INF Binaries/myron/QcomChargerDxeLA/QcomChargerDxeLA.inf
  INF Binaries/myron/ChargerExDxe/ChargerExDxe.inf
  INF Binaries/myron/UsbfnDwc3Dxe/UsbfnDwc3Dxe.inf
  INF Binaries/myron/XhciPciEmulation/XhciPciEmulation.inf
  INF Binaries/myron/XhciDxe/XhciDxe.inf

  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf

  INF Binaries/myron/UsbMsdDxe/UsbMsdDxe.inf
  INF Binaries/myron/UsbDeviceDxe/UsbDeviceDxe.inf
  INF Binaries/myron/UsbConfigDxe/UsbConfigDxe.inf
  INF Binaries/myron/ButtonsDxe/ButtonsDxe.inf
  INF Binaries/myron/TsensDxe/TsensDxe.inf

!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
  INF Binaries/myron/DisplayDxe/DisplayDxe.inf
!else
  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
!endif

  # INF Binaries/aston/QcomPkg/Drivers/LimitsDxe/LimitsDxe.inf  # not present in the myron stock firmware
!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
  INF Binaries/myron/CPRDxe/CPRDxe.inf
!endif
  INF Binaries/myron/GpiDxe/GpiDxe.inf
  INF Binaries/myron/UCDxe/UCDxe.inf
  INF Binaries/myron/RngDxe/RngDxe.inf

  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf

  INF Binaries/myron/RscDxe/RscDxe.inf
  # INF Binaries/aston/QcomPkg/Drivers/OGaugeAuthDxe/OGaugeAuth.inf  # not present in the myron stock firmware
}
