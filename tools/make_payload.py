"""Build the boot payload and put it where the firmware will find it.

Two jobs, both done without external tools:

  fat     lay out a FAT16 volume holding the UniXNU configuration and its
          payloads, because the menu reads /EFI/UniXNU/unixnu.toml from a
          file-system volume and nothing else.

  repack  rewrite an Android boot image (header v0) with that volume as its
          ramdisk. build_uefi.py packs whatever sits in Resources/ramdisk, but
          the payload it should carry is produced by that same run, so the
          image is rebuilt afterwards instead of racing the build.

Names are kept to 8.3 on purpose: no long-file-name entries means no chance of
getting the checksum wrong, and FAT is case-insensitive, so UNIXNU.TOM still
answers to unixnu.toml.
"""

import argparse
import struct
import sys
from pathlib import Path

SECTOR = 512
CLUSTER_SECTORS = 4
RESERVED = 1
NUM_FATS = 2
ROOT_ENTRIES = 512
ROOT_SECTORS = ROOT_ENTRIES * 32 // SECTOR
CLUSTER = CLUSTER_SECTORS * SECTOR

ATTR_DIR = 0x10
ATTR_ARCHIVE = 0x20
ATTR_VOLUME = 0x08
EOC = 0xFFFF
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


class Fat16:
    def __init__(self, total_sectors: int):
        self.total = total_sectors
        spf = 1
        while True:
            clusters = (total_sectors - RESERVED - ROOT_SECTORS - NUM_FATS * spf) // CLUSTER_SECTORS
            need = ((clusters + 2) * 2 + SECTOR - 1) // SECTOR
            if need <= spf:
                break
            spf = need
        self.spf = spf
        self.clusters = clusters
        if clusters >= 0xFFF0:
            raise SystemExit("卷太大，FAT16 装不下")
        self.data_start = RESERVED + NUM_FATS * spf + ROOT_SECTORS
        self.fat = [0] * (clusters + 2)
        self.fat[0] = 0xFFF8
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
        for i in range(0, len(blob), CLUSTER):
            cur = first + i // CLUSTER
            off = (self.data_start + (cur - 2) * CLUSTER_SECTORS) * SECTOR
            self.data[off:off + len(blob[i:i + CLUSTER])] = blob[i:i + CLUSTER]

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
        bs[13] = CLUSTER_SECTORS
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
        bs[54:62] = b"FAT16   "
        bs[510:512] = b"\x55\xAA"
        self.data[0:SECTOR] = bs

        fat = bytearray(self.spf * SECTOR)
        for i, value in enumerate(self.fat):
            struct.pack_into("<H", fat, i * 2, value)
        for n in range(NUM_FATS):
            off = (RESERVED + n * self.spf) * SECTOR
            self.data[off:off + len(fat)] = fat
        return bytes(self.data)


def build_fat(files: dict, total_sectors: int) -> bytes:
    fs = Fat16(total_sectors)

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
            count = max(1, (len(blob) + CLUSTER - 1) // CLUSTER)
            cluster = fs.alloc(count) if blob else 0
            if blob:
                fs.write_chain(cluster, blob)
            entries.append(dir_entry(short_name(name), ATTR_ARCHIVE, cluster, len(blob)))
        if parent is None:
            entries.append(dir_entry(LABEL.ljust(11), ATTR_VOLUME, 0, 0))
            fs.write_root(entries)
        else:
            blob = b"".join(entries)
            if len(blob) > CLUSTER:
                raise SystemExit("目录 %s 太大" % node["name"])
            fs.write_chain(node["cluster"], blob)
        for name in sorted(node["dirs"]):
            write_dir(node["dirs"][name], node)

    alloc_dirs(root)
    write_dir(root, None)
    return fs.finalize()


def repack(boot: Path, ramdisk: Path, out: Path) -> None:
    data = boot.read_bytes()
    if data[:8] != b"ANDROID!":
        raise SystemExit("不是 Android 启动镜像")
    kernel_size, _ka, old_rd, _ra = struct.unpack_from("<4I", data, 8)
    second_size = struct.unpack_from("<I", data, 24)[0]
    page = struct.unpack_from("<I", data, 36)[0]
    if struct.unpack_from("<I", data, 1640)[0] != 0:
        raise SystemExit("只处理 header v0")

    header = bytearray(data[:page])
    kernel = data[page:page + kernel_size]
    ramdisk_blob = ramdisk.read_bytes()
    rd_off = page + ((kernel_size + page - 1) // page) * page
    second = data[rd_off + ((old_rd + page - 1) // page) * page:
                  rd_off + ((old_rd + page - 1) // page) * page + second_size]

    struct.pack_into("<I", header, 16, len(ramdisk_blob))
    blob = bytearray(header)
    for part in (kernel, ramdisk_blob, second):
        blob += part
        blob += b"\0" * ((-len(part)) % page)
    out.write_bytes(bytes(blob))
    print("boot.img 重打包: kernel %d + ramdisk %d + second %d -> %d 字节"
          % (len(kernel), len(ramdisk_blob), len(second), len(blob)))


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    f = sub.add_parser("fat", help="造 FAT16 卷")
    f.add_argument("--out", type=Path, required=True)
    f.add_argument("--add", action="append", default=[],
                   help="卷内路径=本地文件，例如 EFI/UNIXNU/UNIXNU.TOM=cfg.toml")
    f.add_argument("--size-kib", type=int, default=8192)

    r = sub.add_parser("repack", help="把 ramdisk 换进 boot.img")
    r.add_argument("--boot", type=Path, required=True)
    r.add_argument("--ramdisk", type=Path, required=True)
    r.add_argument("--out", type=Path, required=True)

    a = ap.parse_args()
    if a.cmd == "fat":
        files = {}
        for spec in a.add:
            if "=" not in spec:
                raise SystemExit("--add 需要 卷内路径=本地文件")
            dest, src = spec.split("=", 1)
            files[dest.strip("/")] = Path(src).read_bytes()
        image = build_fat(files, a.size_kib * 1024 // SECTOR)
        a.out.write_bytes(image)
        print("FAT16 镜像 %s: %d 字节" % (a.out, len(image)))
        for name in sorted(files):
            print("   %-28s %d 字节" % (name, len(files[name])))
    else:
        repack(a.boot, a.ramdisk, a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
