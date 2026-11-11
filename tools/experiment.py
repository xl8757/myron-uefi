#!/usr/bin/env python3
"""在构建时改平台包的实验开关，用于定位复位点。

不修改仓库内容，只改 Mu-Silicium 工作副本里的 myronPkg：

    --display custom|simple   走厂商 DisplayDxe，或退回 SimpleFbDxe（不碰 MDSS）
    --skip DisplayDxe CPRDxe  从 DXE.inc / APRIORI.inc 里注释掉指定模块

用法:
    python tools/experiment.py --pkg Mu-Silicium/Platforms/Xiaomi/myronPkg \\
        --display simple --skip CPRDxe
"""

import argparse
import re
import sys
from pathlib import Path


def set_display(pkg: Path, mode: str) -> None:
    dsc = pkg / "myron.dsc"
    lines = dsc.read_text(encoding="utf-8").splitlines(True)
    hits = 0
    for i, line in enumerate(lines):
        if "USE_CUSTOM_DISPLAY_DRIVER" in line and "=" in line and not line.lstrip().startswith("#"):
            lines[i] = re.sub(r"=.*", "= " + ("1" if mode == "custom" else "0"), line)
            hits += 1
    if not hits:
        sys.exit("myron.dsc 里没找到 USE_CUSTOM_DISPLAY_DRIVER")
    dsc.write_text("".join(lines), encoding="utf-8")
    print("display = %s（改到 %d 行）" % (mode, hits))


def skip_modules(pkg: Path, names) -> None:
    if not names:
        print("skip = (none)")
        return
    total = 0
    already = 0
    for inc in ("DXE.inc", "APRIORI.inc"):
        p = pkg / "Include" / inc
        if not p.is_file():
            continue
        out = []
        for line in p.read_text(encoding="utf-8").splitlines(True):
            s = line.strip()
            hit = any(("/%s/" % n in s) or ("/%s." % n in s) for n in names)
            if s.startswith("#SKIPPED"):
                if hit:
                    already += 1
                out.append(line)
                continue
            if s.startswith("#"):
                out.append(line)
                continue
            if s.startswith("INF ") and hit:
                out.append("  #SKIPPED " + s + "\n")
                total += 1
                continue
            out.append(line)
        p.write_text("".join(out), encoding="utf-8")
    print("skip = %s（本次注释 %d 行，此前已注释 %d 行）" % (" ".join(names), total, already))
    if total == 0 and already == 0:
        sys.exit("指定的模块一个都没匹配到，检查名字")


def drop_vendor(pkg: Path) -> None:
    """把 Binaries/ 下的厂商模块全部注释掉，只留 Mu 自己编的驱动。

    厂商那套 DXE 是原厂 UEFI 的一部分，它们靠设备树/平台配置找自己的区域。
    内存启动时那套配置可能根本不存在，于是一个驱动接一个驱动地断言。摘掉整层
    可以直接回答「BDS / UniXNU 到底需不需要它们」。
    """
    total = 0
    for inc in ("DXE.inc", "APRIORI.inc"):
        p = pkg / "Include" / inc
        if not p.is_file():
            continue
        out = []
        for line in p.read_text(encoding="utf-8").splitlines(True):
            s = line.strip()
            if s.startswith("#") or not s.startswith("INF "):
                out.append(line)
                continue
            if "Binaries/" in s:
                out.append("  #NOVENDOR " + s + "\n")
                total += 1
                continue
            out.append(line)
        p.write_text("".join(out), encoding="utf-8")
    print("vendor = none（共注释 %d 个厂商模块）" % total)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--pkg", type=Path, required=True)
    ap.add_argument("--display", choices=("custom", "simple"), default="custom")
    ap.add_argument("--skip", nargs="*", default=[])
    ap.add_argument("--vendor", choices=("full", "none"), default="full")
    a = ap.parse_args()

    if not (a.pkg / "myron.dsc").is_file():
        sys.exit("不是 myronPkg 目录: %s" % a.pkg)

    set_display(a.pkg, a.display)
    skip_modules(a.pkg, a.skip)
    if a.vendor == "none":
        drop_vendor(a.pkg)

    print("\n--- 当前生效的显示相关行 ---")
    for line in (a.pkg / "myron.dsc").read_text(encoding="utf-8").splitlines():
        if "USE_CUSTOM_DISPLAY_DRIVER" in line:
            print("dsc:", line.strip())
    for inc in ("DXE.inc", "APRIORI.inc"):
        p = a.pkg / "Include" / inc
        if p.is_file():
            for line in p.read_text(encoding="utf-8").splitlines():
                if "SKIPPED" in line:
                    print("%s: %s" % (inc, line.strip()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
