"""Build the boot payload and put it where the firmware will find it.

Two jobs, both done without external tools:

  fat     lay out a FAT12 volume holding the UniXNU configuration and its
          payloads, because the menu reads /EFI/UniXNU/unixnu.toml from a
          file-system volume and nothing else. FAT12, not FAT16: a payload this
          small works out to a few hundred clusters, and the FAT type is
          decided by cluster count, so a volume labelled FAT16 with 502
          clusters is read with 12-bit entries by any conforming driver and
          fails to mount.

  sparse  pack a large FAT volume for embedding in the FD. The tail of such a
          volume is entirely zero, so store only the used prefix plus a 12-byte
          header; DXE clears the XNU_Payload window and copies the prefix back.

  refd    replace the firmware volume embedded in the boot image's gzip kernel.
          The workflow builds twice: the first build emits myron-boot.img, the
          second (after the payload volume exists) only regenerates the FV.
          Without this step the image keeps the first build's stale FD, so any
          change to the payload volume never reaches the device.

Names are kept to 8.3 on purpose: no long-file-name entries means no chance of
getting the checksum wrong, and FAT is case-insensitive, so UNIXNU.TOM still
answers to unixnu.toml.
"""

import argparse
import gzip
import struct
import sys
import zlib
from pathlib import Path

SECTOR = 512
CLUSTER_SECTORS = 1
RESERVED = 1
NUM_FATS = 2
ROOT_ENTRIES = 512
ROOT_SECTORS = ROOT_ENTRIES * 32 // SECTOR
CLUSTER = CLUSTER_SECTORS * SECTOR

ATTR_DIR = 0x10
ATTR_ARCHIVE = 0x20
ATTR_VOLUME = 0x08
EOC = 0xFFF
LABEL = b"UNIXNUPAYLD"


def short_name(name: str) -> bytes:
    """8.3 name, space padded, upper case."""
    name = name.upper()
    base, _, ext = name.rpartition(".") if "." in name else (name, "", "")
    if not base or len(base) > 8 or len(ext) > 3:
        raise SystemExit("名字不是 8.3 形式: %s" % name)
    return (base.ljust(8) + ext.ljust(3)).encode("ascii")


def dir_entry(name: bytes, attr: int, cluster: int, size: int) -> bytes:
    entry = bytearray(32)
    entry[0:11] = name
    entry[11] = attr
    struct.pack_into("<H", entry, 26, cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size)
    return bytes(entry)


class FatVolume:
    def __init__(self, total_sectors: int, cluster_sectors: int = 1):
        self.total = total_sectors
        self.cs = cluster_sectors
        self.cluster = cluster_sectors * SECTOR
        spf = 1
        while True:
            clusters = (total_sectors - RESERVED - ROOT_SECTORS - NUM_FATS * spf) // self.cs
            need = ((clusters + 2) * 3 // 2 + SECTOR - 1) // SECTOR
            if need <= spf:
                break
            spf = need
        self.spf = spf
        self.clusters = clusters
        if clusters >= 0xFF5:
            raise SystemExit("卷太大，FAT12 装不下（>4084 簇）：%d，请加大簇" % clusters)
        self.data_start = RESERVED + NUM_FATS * spf + ROOT_SECTORS
        self.fat = [0] * (clusters + 2)
        self.fat[0] = 0xFF8
        self.fat[1] = EOC
        self.next_free = 2
        self.data = bytearray(total_sectors * SECTOR)

    def alloc(self, count: int = 1) -> int:
        first = self.next_free
        for i in range(count):
            cur = first + i
            if cur > self.clusters + 1:
                raise SystemExit("卷不够大，需要更多簇")
            self.fat[cur] = cur + 1 if i + 1 < count else EOC
        self.next_free += count
        return first

    def write_chain(self, first: int, blob: bytes) -> None:
        for i in range(0, len(blob), self.cluster):
            cur = first + i // self.cluster
            off = (self.data_start + (cur - 2) * self.cs) * SECTOR
            self.data[off:off + len(blob[i:i + self.cluster])] = blob[i:i + self.cluster]

    def write_root(self, entries) -> None:
        base = (RESERVED + NUM_FATS * self.spf) * SECTOR
        blob = b"".join(entries)
        if len(blob) > ROOT_SECTORS * SECTOR:
            raise SystemExit("根目录条目太多")
        self.data[base:base + len(blob)] = blob

    def finalize(self) -> bytes:
        bs = bytearray(SECTOR)
        bs[0:3] = b"\xEB\x3C\x90"
        bs[3:11] = b"MSWIN4.1"
        struct.pack_into("<H", bs, 11, SECTOR)
        bs[13] = self.cs
        struct.pack_into("<H", bs, 14, RESERVED)
        bs[16] = NUM_FATS
        struct.pack_into("<H", bs, 17, ROOT_ENTRIES)
        if self.total < 0x10000:
            struct.pack_into("<H", bs, 19, self.total)
        else:
            struct.pack_into("<I", bs, 32, self.total)
        bs[21] = 0xF8
        struct.pack_into("<H", bs, 22, self.spf)
        struct.pack_into("<H", bs, 24, 32)
        struct.pack_into("<H", bs, 26, 64)
        bs[36] = 0x80
        bs[38] = 0x29
        struct.pack_into("<I", bs, 39, 0x4D59524E)
        bs[43:54] = LABEL
        bs[54:62] = b"FAT12   "
        bs[510:512] = b"\x55\xAA"
        self.data[0:SECTOR] = bs

        fat = bytearray(self.spf * SECTOR)
        for i, value in enumerate(self.fat):
            off = i + i // 2                      # 每两项占 3 字节
            if i & 1:
                fat[off] = (fat[off] & 0x0F) | ((value << 4) & 0xF0)
                fat[off + 1] = (value >> 4) & 0xFF
            else:
                fat[off] = value & 0xFF
                fat[off + 1] = (fat[off + 1] & 0xF0) | ((value >> 8) & 0x0F)
        for n in range(NUM_FATS):
            off = (RESERVED + n * self.spf) * SECTOR
            self.data[off:off + len(fat)] = fat
        return bytes(self.data)


def build_fat(files: dict, total_sectors: int, cluster_sectors: int = 1) -> bytes:
    fs = FatVolume(total_sectors, cluster_sectors)

    root = {"dirs": {}, "files": [], "cluster": 0, "name": ""}
    for path, blob in files.items():
        node = root
        parts = path.split("/")
        for part in parts[:-1]:
            node = node["dirs"].setdefault(part, {"dirs": {}, "files": [], "cluster": 0, "name": part})
        node["files"].append((parts[-1], blob))

    def alloc_dirs(node):
        for name in sorted(node["dirs"]):
            child = node["dirs"][name]
            child["cluster"] = fs.alloc(1)
            alloc_dirs(child)

    def write_dir(node, parent):
        entries = []
        if parent is not None:
            entries.append(dir_entry(b".          ", ATTR_DIR, node["cluster"], 0))
            entries.append(dir_entry(b"..         ", ATTR_DIR, parent["cluster"], 0))
        for name in sorted(node["dirs"]):
            child = node["dirs"][name]
            entries.append(dir_entry(short_name(name), ATTR_DIR, child["cluster"], 0))
        for name, blob in sorted(node["files"]):
            count = max(1, (len(blob) + fs.cluster - 1) // fs.cluster)
            cluster = fs.alloc(count) if blob else 0
            if blob:
                fs.write_chain(cluster, blob)
            entries.append(dir_entry(short_name(name), ATTR_ARCHIVE, cluster, len(blob)))
        if parent is None:
            entries.append(dir_entry(LABEL.ljust(11), ATTR_VOLUME, 0, 0))
            fs.write_root(entries)
        else:
            blob = b"".join(entries)
            if len(blob) > fs.cluster:
                raise SystemExit("目录 %s 太大" % node["name"])
            fs.write_chain(node["cluster"], blob)
        for name in sorted(node["dirs"]):
            write_dir(node["dirs"][name], node)

    alloc_dirs(root)
    write_dir(root, None)
    return fs.finalize()


def make_sparse(out: Path, src: Path | None, total: int, placeholder: bool) -> None:
    """UXSP magic + total length + used length + used prefix of the volume."""
    if placeholder:
        used, prefix = 0, b""
    else:
        if src is None:
            raise SystemExit("sparse 需要 --in（或用 --placeholder）")
        volume = src.read_bytes()
        used = len(volume)
        while used > 0 and volume[used - 1] == 0:
            used -= 1
        total = len(volume)
        prefix = volume[:used]
    blob = b"UXSP" + struct.pack("<II", total, used) + prefix
    out.write_bytes(blob)
    if placeholder:
        print("占位稀疏镜像 %s: %d 字节 (total %d)" % (out, len(blob), total))
    else:
        print("稀疏镜像 %s: %d -> %d 字节 (%.2f%%)，还原到 %d 字节"
              % (out, total, len(blob), 100.0 * len(blob) / total, total))


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    f = sub.add_parser("fat", help="造 FAT12 卷")
    f.add_argument("--out", type=Path, required=True)
    f.add_argument("--add", action="append", default=[],
                   help="卷内路径=本地文件，例如 EFI/UNIXNU/UNIXNU.TOM=cfg.toml")
    f.add_argument("--size-kib", type=int, default=1024)
    f.add_argument("--cluster-sectors", type=int, default=1,
                   help="每簇扇区数，放大卷时用它把簇数压在 FAT12 上限内")

    s = sub.add_parser("sparse", help="把大 FAT 卷压成稀疏镜像")
    s.add_argument("--out", type=Path, required=True)
    s.add_argument("--in", dest="src", type=Path, default=None)
    s.add_argument("--total", type=lambda x: int(x, 0), default=48 * 1024 * 1024)
    s.add_argument("--placeholder", action="store_true",
                   help="只写头部，供第一次构建时 FDF 引用")

    d = sub.add_parser("refd", help="把新固件卷换进 boot.img 的 kernel")
    d.add_argument("--boot", type=Path, required=True)
    d.add_argument("--fd", type=Path, required=True)
    d.add_argument("--out", type=Path, required=True)

    a = ap.parse_args()
    if a.cmd == "fat":
        files = {}
        for spec in a.add:
            if "=" not in spec:
                raise SystemExit("--add 需要 卷内路径=本地文件")
            dest, src = spec.split("=", 1)
            files[dest.strip("/")] = Path(src).read_bytes()
        image = build_fat(files, a.size_kib * 1024 // SECTOR, a.cluster_sectors)
        a.out.write_bytes(image)
        print("FAT12 镜像 %s: %d 字节" % (a.out, len(image)))
        for name in sorted(files):
            print("   %-28s %d 字节" % (name, len(files[name])))
    elif a.cmd == "sparse":
        make_sparse(a.out, a.src, a.total, a.placeholder)
    else:
        repack_fd(a.boot, a.fd, a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
