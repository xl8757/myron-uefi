#!/usr/bin/env python3
"""Extract the pinned myron vendor DXE/ACPI/panel inputs outside Git.

The source is the public HyperOS firmware package for myron (Redmi K90 Pro Max /
Poco F8 Ultra).  The script verifies the package, ``firmware-update/uefi.img``,
and every file against ``Resources/vendor-inputs.json``, then writes the
``Binaries/myron`` tree the myronPkg DSC/FDF references:

    Binaries/myron/<name>/<name>.efi
    Binaries/myron/<name>/<name>.depex   (when the stock file has a DXE depex)
    Binaries/myron/<name>/<name>.inf
    Binaries/myron/RawFiles/<name>       (panel XML, config and other raw files)
"""
from pathlib import Path
import argparse
import hashlib
import re
import struct
import tempfile
import urllib.request
import zipfile
from uuid import UUID

from uefi_firmware.uefi import FirmwareFile, FirmwareVolume

ROOT = Path(__file__).resolve().parent
URL = ('https://github.com/XiaomiFirmwareUpdaterReleases/firmware_xiaomi_myron/releases/download/'
       'stable-31.10.2025/fw_myron_myron-ota_full-OS3.0.13.0.WPMCNXM-user-16.0-18cc0bfb5e.zip')
ZIP_SHA256 = '7c015caaff578399408b0f580ca0a8170ec964f3f69596399d36a1666e3edf73'
IMAGE_SHA256 = 'e5b65a67fe6777d6fe6868cad23d842fd8486cbc5cef3a497b17bc9256ffc702'


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def elf_volume(data):
    if data[:4] != b'\x7fELF' or data[4:6] != b'\x02\x01':
        raise ValueError('expected a little-endian ELF64 UEFI image')
    phoff, = struct.unpack_from('<Q', data, 32)
    stride, count = struct.unpack_from('<HH', data, 54)
    segments = [struct.unpack_from('<IIQQQQQQ', data, phoff + i * stride) for i in range(count)]
    segment, = [p for p in segments if p[0] == 1]
    fv = FirmwareVolume(data[segment[2]:segment[2] + segment[5]])
    if not fv.valid_header or not fv.process():
        raise ValueError('cannot parse the myron firmware volume')
    return fv


def extract(data, output, expected):
    if sha256(data) != IMAGE_SHA256:
        raise ValueError('myron uefi.img SHA-256 mismatch')
    fv = elf_volume(data)
    found = {}

    def walk(obj):
        if obj is None:
            return
        if isinstance(obj, FirmwareFile):
            names = [s.name for s in obj.sections if s.type == 0x15]
            if names:
                name = names[0]
                if name in expected and name not in found:
                    pe32 = [s.data for s in obj.sections if s.type == 0x10]
                    raw = [s.data for s in obj.sections if s.type == 0x19]
                    depex = [s.data for s in obj.sections if s.type == 0x13]
                    info = expected[name]
                    guid = str(UUID(bytes_le=obj.guid))
                    payload, = pe32 or raw
                    if (len(payload), sha256(payload), guid) != (info['size'], info['sha256'], info['guid']):
                        raise ValueError('vendor input mismatch: ' + name)
                    found[name] = True
                    if info['kind'] == 'pe32':
                        directory = output / name
                        directory.mkdir(parents=True, exist_ok=True)
                        (directory / f'{name}.efi').write_bytes(payload)
                        binary = f'  PE32|{name}.efi|*\n'
                        if depex:
                            dep, = depex
                            if sha256(dep) != info['depex_sha256']:
                                raise ValueError('depex mismatch: ' + name)
                            (directory / f'{name}.depex').write_bytes(dep)
                            binary += f'  DXE_DEPEX|{name}.depex|*\n'
                        elif info.get('depex_sha256'):
                            raise ValueError('missing depex: ' + name)
                        (directory / f'{name}.inf').write_text(
                            '[Defines]\n  INF_VERSION = 0x0001001B\n'
                            f'  BASE_NAME = {name}\n  FILE_GUID = {guid}\n'
                            f'  MODULE_TYPE = {info["module_type"]}\n  VERSION_STRING = 1.0\n'
                            '  ENTRY_POINT = EfiEntry\n\n[Binaries.AARCH64]\n' + binary + '\n[Depex]\n  TRUE\n')
                    else:
                        directory = output / 'RawFiles'
                        directory.mkdir(parents=True, exist_ok=True)
                        (directory / name).write_bytes(payload)
        for child in getattr(obj, 'objects', []):
            walk(child)

    walk(fv)
    missing = set(expected) - set(found)
    if missing:
        raise ValueError('missing vendor inputs: ' + ', '.join(sorted(missing)))
    print(f'Verified {len(found)} myron vendor inputs from the pinned public UEFI image')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mu-root', type=Path, required=True, help='Mu-Silicium checkout (--uefi)')
    parser.add_argument('--image', type=Path, help='local uefi.img; otherwise download the pinned package')
    args = parser.parse_args()
    expected = __import__('json').loads((ROOT / 'Resources/vendor-inputs.json').read_text())
    output = args.mu_root / 'Binaries/myron'
    if args.image:
        extract(args.image.read_bytes(), output, expected)
        return
    with tempfile.TemporaryDirectory(prefix='myron-firmware-') as name:
        archive = Path(name) / 'firmware.zip'
        urllib.request.urlretrieve(URL, archive)
        data = archive.read_bytes()
        if sha256(data) != ZIP_SHA256:
            raise ValueError('myron firmware package SHA-256 mismatch')
        with zipfile.ZipFile(archive) as firmware:
            extract(firmware.read('firmware-update/uefi.img'), output, expected)


if __name__ == '__main__':
    main()
