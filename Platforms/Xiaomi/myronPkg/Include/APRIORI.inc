APRIORI DXE {
  #
  # 只保留 DXE 核心和 PCD 驱动。
  #
  # 原厂固件卷里没有 APRIORI 文件：把那个 0x2a8000 的卷按 FFS 走一遍，
  # 225 个条目全是模块和数据文件，没有一项是 GUID 列表形式的 APRIORI。
  # 也就是说原厂的调度顺序就是文件的物理顺序，而这个物理顺序我们在
  # DXE.inc 里已经照抄了（SmemDxe 在原厂是第 54 项，在我们这里是第 53 项）。
  #
  # 之前这里把几乎全部厂商驱动按另一套顺序重排了一遍，后果是把
  # SmemDxe 从第 54 位提到第 9 位，跑到了 DALSys / HWIODxeDriver /
  # ChipInfo 前面。这些驱动的 depex 都是 TRUE，分发器不会拦，
  # 于是它们在依赖还没就绪时就启动，在库构造函数里拿到 EFI_NOT_FOUND
  # 并触发厂商 DEBUG 构建里编译进去的断言死循环。
  #
  # 依赖顺序这种事不该由我们再猜一遍：原厂怎么排，就怎么排。
  #
  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
}
