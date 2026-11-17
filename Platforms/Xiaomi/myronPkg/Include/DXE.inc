  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

  INF Binaries/aston/QcomPkg/Drivers/EnvDxe/EnvDxe.inf

  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf

  INF Binaries/aston/QcomPkg/Drivers/ShmBridgeDxe/ShmBridgeDxeLA.inf
  INF Binaries/aston/QcomPkg/Drivers/TzDxe/ScmDxeCompat.inf
  INF Binaries/aston/QcomPkg/Drivers/TzDxe/TzDxeLA.inf

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

  INF Binaries/aston/QcomPkg/Drivers/ChipInfoDxe/ChipInfoDxe.inf
  INF aston/QcomPkg/Drivers/IPCCDxe/IPCCDxe.inf
  #INF Binaries/aston/QcomPkg/Drivers/GLinkDxe/GLinkDxe.inf
  INF Binaries/aston/QcomPkg/Drivers/PlatformInfoDxe/PlatformInfoDxe.inf
  INF Binaries/aston/QcomPkg/Drivers/DALSYSDxe/DALSYSDxe.inf
  INF Binaries/aston/QcomPkg/Drivers/HALIOMMUDxe/HALIOMMUDxe.inf
  INF Binaries/aston/QcomPkg/Drivers/HWIODxe/HWIODxe.inf
  INF Binaries/aston/QcomPkg/Drivers/I2CDxe/I2CDxe.inf
  INF Binaries/aston/QcomPkg/Drivers/TLMMDxe/TLMMDxe.inf
  INF Binaries/aston/QcomPkg/Drivers/PmicDxe/PmicDxeLa.inf

  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf

  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf

  INF FatPkg/EnhancedFatDxe/Fat.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf

  INF aston/QcomPkg/Drivers/UFSDxe/UFSDxe.inf
  INF aston/QcomPkg/Drivers/CmdDbDxe/CmdDbDxe.inf
  INF aston/QcomPkg/Drivers/PsStateDxe/PsStateDxe.inf
  INF aston/QcomPkg/Drivers/PwrUtilsDxe/PwrUtilsDxe.inf
  INF aston/QcomPkg/Drivers/RpmhDxe/RpmhDxe.inf
  INF aston/QcomPkg/Drivers/NpaDxe/NpaDxe.inf
  INF aston/QcomPkg/Drivers/ULogDxe/ULogDxe.inf
  INF aston/QcomPkg/Drivers/VcsDxe/VcsDxe.inf
  INF aston/QcomPkg/Drivers/ClockDxe/ClockDxe.inf
  INF Binaries/aston/QcomPkg/Drivers/SPMIDxe/SPMIDxe.inf
  INF aston/QcomPkg/Drivers/ICBDxe/ICBDxe.inf
  INF aston/QcomPkg/Drivers/SmemDxe/SmemDxe.inf

  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

  INF aston/QcomPkg/Drivers/RscDxe/RscDxe.inf
  # INF Binaries/aston/QcomPkg/Drivers/OGaugeAuthDxe/OGaugeAuth.inf  # not present in the myron stock firmware

  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf

!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
  INF aston/QcomPkg/Drivers/CPRDxe/CPRDxe.inf
!endif
  INF aston/QcomPkg/Drivers/GpiDxe/GpiDxe.inf
  INF aston/QcomPkg/Drivers/SdccDxe/SdccDxe.inf
  INF aston/QcomPkg/Drivers/FeatureEnablerDxe/FeatureEnablerDxe.inf

!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
  INF aston/QcomPkg/Drivers/DisplayDxe/DisplayDxe.inf
!else
  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
!endif

  INF aston/QcomPkg/Drivers/ButtonsDxe/ButtonsDxe.inf
  INF aston/QcomPkg/Drivers/ChargerExDxe/ChargerExDxe.inf
  # INF Binaries/aston/QcomPkg/Drivers/OcdtDxe/OplusProject.inf  # not present in the myron stock firmware
  INF aston/QcomPkg/Drivers/PmicGlinkDxe/PmicGlinkDxe.inf
  INF aston/QcomPkg/Drivers/QcomChargerDxe/QcomChargerDxeLA.inf
  INF aston/QcomPkg/Drivers/UsbPwrCtrlDxe/UsbPwrCtrlDxe.inf
  INF aston/QcomPkg/Drivers/TsensDxe/TsensDxe.inf
  # INF Binaries/aston/QcomPkg/Drivers/LimitsDxe/LimitsDxe.inf  # not present in the myron stock firmware
  INF aston/QcomPkg/Drivers/DDRInfoDxe/DDRInfoDxe.inf
  INF aston/QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Dxe.inf
  INF aston/QcomPkg/Drivers/XhciDxe/XhciDxe.inf

  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf

  INF aston/QcomPkg/Drivers/UsbMsdDxe/UsbMsdDxe.inf
  INF aston/QcomPkg/Drivers/UsbDeviceDxe/UsbDeviceDxe.inf
  INF aston/QcomPkg/Drivers/UsbConfigDxe/UsbConfigDxe.inf
  INF aston/QcomPkg/Drivers/UsbInitDxe/UsbInitDxe.inf
  INF aston/QcomPkg/Drivers/UCDxe/UCDxe.inf
  INF aston/QcomPkg/Drivers/RNGDxe/RngDxe.inf
  INF aston/QcomPkg/Drivers/PILDxe/PILDxe.inf
  INF aston/QcomPkg/Drivers/PILProxyDxe/PILProxyDxe.inf
  INF aston/QcomPkg/Drivers/SPSSDxe/SPSSDxe.inf
