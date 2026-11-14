"""Build firmware with a configurable switch to cntrl and persistent boot choice."""

import struct
import subprocess
import sys
from pathlib import Path
from typing import Annotated
from urllib.request import urlopen

import tyro
from pydantic import AfterValidator, BaseModel, Field
from reccy.configuration import units

from ....firmware.intel_hex import FirmwareImage, encode_hex, read_hex
from ....patch_switcher.models import millisecond_resolution
from . import toolchain
from .upload_helper import validate_firmware


class Build(BaseModel, frozen=True):
    diagnostics: bool = False
    switch_window: Annotated[
        float,
        Field(strict=True, ge=0.001, le=0.112),
        units.unit_validator('second'),
        AfterValidator(millisecond_resolution),
    ] = 0.05
    stock: Path = Path(__file__).parent / (
        '2026-10-16_MC6_PRO_Firmware_v_3_14_0_261016501_0.hex'
    )


def combine(
    stock: FirmwareImage, custom: FirmwareImage, hook: bytes, reader: int
) -> bytes:
    if stock.sha256 != STOCK_SHA256:
        raise ValueError('stock firmware does not match the traced v3.14.0 image')
    if (
        custom.origin != 0x60100000
        or custom.origin + len(custom.data) > 0x607C0000
        or len(custom.regions) != 1
        or len(custom.data) % 1024
        or not 0 < len(hook) <= 4096
        or not reader & 1
        or not 0x60083000 <= reader - 1 < 0x60083000 + len(hook)
    ):
        raise ValueError('invalid relocated image or stock hook layout')
    result = bytearray(b'\xff' * (0x100000 + len(custom.data)))
    result[: len(stock.data)] = stock.data
    result[0x100000:] = custom.data
    patches = [
        (0x18E0, bytes.fromhex('704700bf'), bytes.fromhex('81f08ebb')),
        (0x7C424, struct.pack('<I', 0x2649), struct.pack('<I', reader)),
        (0x15F0, struct.pack('<I', 0x20013CA0), struct.pack('<I', 0x20013DA0)),
        (0x1990, struct.pack('<I', 0x20013CA0), struct.pack('<I', 0x20013DA0)),
        (0x1024, struct.pack('<I', 0x83000), struct.pack('<I', len(result))),
        (
            0x1018,
            struct.pack('<I', 0x60082400),
            struct.pack('<I', stock.origin + len(result) - 3072),
        ),
    ]
    for offset, expected, replacement in patches:
        if result[offset : offset + len(expected)] != expected:
            raise ValueError(f'unexpected stock patch bytes at {offset:#x}')
        result[offset : offset + len(expected)] = replacement
    result[0x83000 : 0x83000 + len(hook)] = hook
    # This sector belongs to boot selection, not either application's image.
    struct.pack_into('<2I', result, 0x84000, 0x434E4254, 0xBCB1BDAB)
    return bytes(result)


def linker_script(original: str, *, diagnostics: bool = False) -> str:
    region = 'FLASH (rx): ORIGIN = 0x60000000, LENGTH = 7936K'
    flash = '*(.flashmem*)'
    if original.count(region) != 1 or original.count(flash) != 1:
        raise ValueError('unexpected Teensy linker script')
    if diagnostics:
        heap = '_heap_end = ORIGIN(RAM) + LENGTH(RAM);'
        if original.count(heap) != 1:
            raise ValueError('unexpected Teensy heap boundary')
        # The SDK preserves its crash record in the top 128 bytes of OCRAM.
        original = original.replace(
            heap, '_heap_end = ORIGIN(RAM) + LENGTH(RAM) - 128;'
        )
    # Allocator preparation/reclamation and finite reversal are cold. Keep
    # insertion, erasure and iteration in ITCM. Template COMDAT sections cannot
    # safely share an explicit section attribute across local and public types.
    cold = '\n\t\t'.join(
        (
            flash,
            '*(.text._ZN5cntrl11CheckedListI*7reserveEj*)',
            '*(.text._ZN5cntrl11CheckedListI*4trimEj*)',
            '*(.text._ZN5cntrl11CheckedListI*7releaseEv*)',
            '*(.text._ZN5cntrl12CheckedArrayI*7reserveEj*)',
            '*(.text._ZN5cntrl4midi16reverse_sequenceI*)',
            '*(.text._ZN5cntrl4midi7RoutingI*EC[12]E*)',
            '*(.text._ZN5cntrl4midi7RoutingI*ED[12]Ev*)',
            # Graph preparation only: never the live owner lookup, MIDI
            # admission, queue publication or note/pedal release paths.
            '*(.text._ZN5cntrl4midi7RoutingI*E12build_ownersI*)',
            '*(.text._ZN5cntrl4midi7RoutingI*E20prepare_output_stateE*)',
            '*(.text._ZN5cntrl4midi7RoutingI*E6Reader5matchE*)',
            # These policy templates were cold methods of Streams before the
            # routing/processing split. Keep their original placement.
            '*(.text._ZN5cntrl4midi18AdvancedProcessing10lfo_statusI*)',
            '*(.text._ZN5cntrl4midi18AdvancedProcessing18parameter_bindingsI*)',
            '*(.text._ZN5cntrl4midi18AdvancedProcessing13validate_lfosI*)',
            '*(.text._ZN5cntrl4midi18AdvancedProcessing8bind_lfoI*)',
            '*(.text._ZN5cntrl4midi18AdvancedProcessing12service_lfosI*)',
            '*(.text._ZN5cntrl4midi18AdvancedProcessing18service_parametersI*)',
            '*(.text._ZN5cntrl4midi18AdvancedProcessing14numeric_fieldsI*)',
            '*(.text._ZN5cntrl4midi18AdvancedProcessing13reverse_groupI*)',
            '*(.text._ZN5cntrl4midi18AdvancedProcessing12process_stepI*)',
            '*(.text._ZN5cntrl4midi18AdvancedProcessing18service_generatorsI*)',
            '*(.text._ZN5cntrl4midi18AdvancedProcessing17finish_parametersI*)',
            '*(.text._ZN5cntrl4midi18AdvancedProcessing5StateI*EC[12]E*)',
            '*(.text._ZN5cntrl4midi18AdvancedProcessing5StateI*ED[12]Ev*)',
            '*(.text._ZN5cntrl4midi7RuntimeC[12]E*)',
            '*(.text._ZN5cntrl5SongsC[12]E*)',
            # Management screens, browsing and saved-return decoding are cold.
            # Leave the MIDI preparation/queue iteration paths in ITCM.
            '*(.text._ZN5cntrl7SongsUi6renderEv*)',
            '*(.text._ZN5cntrl7SongsUi13handle_buttonEjb*)',
            '*(.text._ZN5cntrl10ModuleMenu6renderEv*)',
            '*(.text._ZN5cntrl14StorageListing4nextE*)',
            '*(.text._ZN5cntrl2ui19FirmwareVisitRecord6decodeE*)',
            # target.cpp's static Board/runtime construction runs only at boot.
            '*(.text.startup._GLOBAL__sub_I__Z15switch_scan_duev)',
        )
    )
    return original.replace(
        region, 'FLASH (rx): ORIGIN = 0x60100000, LENGTH = 6912K'
    ).replace(flash, cold)


def build(command: Build, *, source_root: Path | None = None) -> None:
    root = Path(__file__).parent / 'firmware'
    work = root / 'build/boot-switch'
    work.mkdir(parents=True, exist_ok=True)
    if not command.stock.exists() and command.stock == Build().stock:
        with urlopen(
            'https://github.com/Morningstar-Engineering/MC6-PRO-MIDI-Controller/releases/download/v3.14.0/'
            + command.stock.name,
            timeout=60,
        ) as response:
            data = response.read()
        import hashlib

        if hashlib.sha256(data).hexdigest() != STOCK_SHA256:
            raise ValueError('downloaded stock firmware checksum mismatch')
        command.stock.write_bytes(data)
    stock = read_hex(command.stock)
    if stock.sha256 != STOCK_SHA256:
        raise ValueError('stock firmware does not match the traced v3.14.0 image')
    core = root / '.tools/data/packages/teensy/hardware/avr/1.62.0'
    toolchain.ensure(root)
    original = (core / 'cores/teensy4/imxrt1062_t41.ld').read_text()
    linker = work / 'cntrl.ld'
    linker.write_text(linker_script(original, diagnostics=command.diagnostics))
    toolchain.build(
        root,
        source_root or Path(__file__).resolve().parents[4],
        work,
        linker,
        diagnostics=command.diagnostics,
    )
    custom = read_hex(work / 'cntrl.hex')
    entry = struct.unpack_from('<I', custom.data, 0x1004)[0]
    source = root / 'boot_switch'
    elf = work / 'hook.elf'
    subprocess.run(
        [
            str(toolchain.arm_tool(root, 'g++')),
            '-mcpu=cortex-m7',
            '-mthumb',
            '-mfloat-abi=hard',
            '-mfpu=fpv5-d16',
            '-std=c++17',
            '-Os',
            '-ffreestanding',
            '-fno-exceptions',
            '-fno-rtti',
            '-fno-unwind-tables',
            '-fno-asynchronous-unwind-tables',
            '-nostdlib',
            '-Wall',
            '-Wextra',
            '-Werror',
            f'-DCNTRL_SWITCH_WINDOW_MS={round(command.switch_window * 1000)}',
            f'-Wl,-T,{source / "hook.ld"},--defsym,custom_entry={entry}',
            str(source / 'gate.S'),
            str(source / 'hook.cpp'),
            '-o',
            str(elf),
        ],
        check=True,
    )
    binary = work / 'hook.bin'
    subprocess.run(
        [
            str(toolchain.arm_tool(root, 'objcopy')),
            '-O',
            'binary',
            str(elf),
            str(binary),
        ],
        check=True,
    )
    symbols = subprocess.run(
        [str(toolchain.arm_tool(root, 'nm')), '--defined-only', str(elf)],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    addresses = {
        p[2]: int(p[0], 16) for s in symbols.splitlines() if len(p := s.split()) == 3
    }
    combined = combine(
        stock, custom, binary.read_bytes(), addresses['stock_reader'] | 1
    )
    output = work / 'combined.hex'
    output.write_text(encode_hex(combined, 0x60000000, 0x60001000))
    image = read_hex(output)
    validate_firmware(image)
    if image.data != combined:
        raise ValueError('combined HEX readback differs')
    print(
        f'{output}: patch mode by default; '
        f'stock exit pair is configured per visit, '
        f'{command.switch_window} seconds, no hold'
    )
    print(f'SHA256: {image.sha256}')


def main() -> None:
    command = tyro.cli(Build)
    try:
        build(command)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))


STOCK_SHA256 = '228d73e768fad383bc205c8fc1ef52f3a49de08c754aca11d4a069ca1aab9fc8'


if __name__ == '__main__':
    main()
