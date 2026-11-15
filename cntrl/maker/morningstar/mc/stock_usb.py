"""Read-only evidence for stock USB software bootloader entry."""

import struct
import subprocess
from pathlib import Path

import tyro
from pydantic import BaseModel

from ....firmware.intel_hex import read_hex
from . import boot_switch, toolchain


class Inspect(BaseModel, frozen=True):
    stock: Path
    output: Path


def inspect(command: Inspect) -> None:
    image = read_hex(command.stock)
    if image.sha256 != boot_switch.STOCK_SHA256:
        raise ValueError('expected the pinned MC6 Pro stock image')
    command.output.mkdir(parents=True)
    binary = command.output / 'stock.bin'
    binary.write_bytes(image.data)
    result = subprocess.run(
        [
            str(toolchain.arm_tool(Path(__file__).with_name('firmware'), 'objdump')),
            '-D',
            '-b',
            'binary',
            '-m',
            'arm',
            '-M',
            'force-thumb',
            '--adjust-vma=0x60000000',
            str(binary),
        ],
        capture_output=True,
        check=True,
    )
    (command.output / 'stock.disassembly.txt').write_bytes(result.stdout)
    # Teensy's SEREMU HID usage page and its reboot feature-report magic.
    # Presence is static evidence only; TyTools checks live reboot capability.
    report = ['sha256 = ' + repr(image.sha256)]
    for offset in range(len(image.data) - 18):
        if image.data[offset : offset + 4] == bytes.fromhex('12010002'):
            report.append(
                f'device_{offset:x} = {list(image.data[offset : offset + 18])}'
            )
        if image.data[offset : offset + 2] == b'\x09\x02':
            length = struct.unpack_from('<H', image.data, offset + 2)[0]
            if not 9 <= length <= 4096 or offset + length > len(image.data):
                continue
            position = offset
            interfaces: list[list[int]] = []
            while position < offset + length:
                size = image.data[position]
                if size < 2 or position + size > offset + length:
                    break
                if image.data[position + 1] == 4 and size == 9:
                    interfaces.append(list(image.data[position : position + 9]))
                position += size
            if position == offset + length and interfaces:
                report.append(f'configuration_{offset:x} = {interfaces}')
    for name, pattern in (
        ('seremu_usage', bytes.fromhex('06c9ff')),
        ('reboot_magic', bytes.fromhex('a945c26b')),
    ):
        offsets: list[int] = []
        start = 0
        while (offset := image.data.find(pattern, start)) >= 0:
            offsets.append(offset)
            start = offset + 1
        report.append(f'{name}_offsets = {offsets}')
    (command.output / 'evidence.toml').write_text('\n'.join(report) + '\n')
    print('\n'.join(report))


if __name__ == '__main__':
    inspect(tyro.cli(Inspect))
