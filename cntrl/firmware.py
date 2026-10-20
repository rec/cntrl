"""Read-only Intel HEX inspection and i.MX RT firmware analysis artifacts."""

import hashlib
import math
import re
import struct
from collections import Counter
from pathlib import Path

from pydantic import BaseModel


class AddressRange(BaseModel, frozen=True):
    """A populated address interval, with an exclusive end."""

    start: int
    end: int


class FirmwareImage(BaseModel, frozen=True):
    origin: int
    entry: int | None
    data: bytes
    regions: list[AddressRange]
    record_counts: dict[int, int]
    sha256: str


def read_hex(path: Path) -> FirmwareImage:
    """Validate Intel HEX records and decode addresses, filling gaps with 0xff."""
    source = path.read_bytes()
    memory: dict[int, int] = {}
    records: Counter[int] = Counter()
    base = 0
    entry: int | None = None
    finished = False
    for number, line in enumerate(source.decode('ascii').splitlines(), 1):
        if finished or not line.startswith(':'):
            raise ValueError(f'line {number}: invalid record or data after EOF')
        record = bytes.fromhex(line[1:])
        if len(record) < 5 or len(record) != record[0] + 5:
            raise ValueError(f'line {number}: incorrect record length')
        if sum(record) % 256:
            raise ValueError(f'line {number}: invalid checksum')
        address = int.from_bytes(record[1:3], 'big')
        kind = record[3]
        payload = record[4:-1]
        records[kind] += 1
        if kind == 0:
            for index, byte in enumerate(payload):
                location = base + address + index
                if location in memory:
                    raise ValueError(
                        f'line {number}: overlapping data at {location:#x}'
                    )
                memory[location] = byte
        elif kind in (2, 4) and len(payload) == 2 and address == 0:
            base = int.from_bytes(payload, 'big') << (4 if kind == 2 else 16)
        elif kind == 5 and len(payload) == 4 and address == 0:
            entry = int.from_bytes(payload, 'big')
        elif kind == 1 and not payload and address == 0:
            finished = True
        else:
            raise ValueError(f'line {number}: unsupported or malformed record {kind}')
    if not finished or not memory:
        raise ValueError('image must contain data and an EOF record')

    addresses = sorted(memory)
    regions: list[AddressRange] = []
    start = previous = addresses[0]
    for address in addresses[1:]:
        if address != previous + 1:
            regions.append(AddressRange(start=start, end=previous + 1))
            start = address
        previous = address
    regions.append(AddressRange(start=start, end=previous + 1))
    origin = addresses[0]
    return FirmwareImage(
        origin=origin,
        entry=entry,
        data=bytes(memory.get(a, 0xFF) for a in range(origin, addresses[-1] + 1)),
        regions=regions,
        record_counts=dict(records),
        sha256=hashlib.sha256(source).hexdigest(),
    )


def inspect_firmware(path: Path, output_directory: Path) -> FirmwareImage:
    """Print i.MX RT image details and write derived analysis files.

    Output files are created exclusively so an existing artifact is not overwritten.
    The ELF is a flash-address wrapper for disassembly, not a flashable executable
    or a reconstruction of the runtime RAM mappings.
    """
    image = read_hex(path)
    if image.data[:4] != b'FCFB' or len(image.data) < 0x1060:
        raise ValueError('expected an i.MX RT FlexSPI flash image')
    ivt = struct.unpack_from('<8I', image.data, 0x1000)
    if ivt[0] != 0x432000D1:
        raise ValueError('unexpected i.MX RT image vector table')
    print('SHA256:', image.sha256)
    print('Record counts:', image.record_counts, '(all checksums valid)')
    print('HEX entry:', hex(image.entry) if image.entry is not None else None)
    print('Reset vector:', hex(ivt[1]))
    print('Regions:', [(hex(r.start), hex(r.end)) for r in image.regions])
    print('Populated bytes:', sum(r.end - r.start for r in image.regions))
    print('Image span:', len(image.data))
    print('Configured flash bytes:', struct.unpack_from('<I', image.data, 0x50)[0])
    for offset in (0x1000, 0x1020, 0x1040):
        print(
            f'Words at {image.origin + offset:#010x}:',
            [hex(w) for w in struct.unpack_from('<8I', image.data, offset)],
        )
    signature_offset = ivt[6] - image.origin
    if 0 <= signature_offset < len(image.data):
        signature = image.data[signature_offset:]
        print('CSF pointer:', hex(ivt[6]))
        print('Bytes from CSF to image end:', len(signature))
        print('CSF remainder all zero:', not any(signature))
    for offset in range(0, len(image.data), 65536):
        chunk = image.data[offset : offset + 65536]
        entropy = -sum(
            (n / len(chunk)) * math.log2(n / len(chunk))
            for n in Counter(chunk).values()
        )
        print(f'Entropy at {image.origin + offset:#010x}: {entropy:.3f}')

    strings = [
        (image.origin + m.start(), m.group().decode('ascii'))
        for m in re.finditer(rb'[\x20-\x7e]{6,}', image.data)
    ]
    print('ASCII strings of at least six characters:', len(strings))
    pattern = re.compile(
        r'teensy|arduino|morningstar|mc6|firmware|ILI9|ST77|GC9|SPI|i2c|uart|'
        r'relay|GPIO|sdFat|\.cpp|\.c\b|\.h\b|malloc|boot|usb|EEPROM|display|lcd|tft|imxrt',
        re.IGNORECASE,
    )
    for address, text in strings:
        if pattern.search(text):
            print(f'{address:#010x}: {text}')
        if 'test' in text.lower():
            locations: list[int] = []
            needle = struct.pack('<I', address)
            start = 0
            while (offset := image.data.find(needle, start)) >= 0:
                locations.append(image.origin + offset)
                start = offset + 1
            print(f'Raw flash pointers to {text!r}:', [hex(a) for a in locations])

    output_directory.mkdir(parents=True, exist_ok=True)
    artifacts = {
        'firmware.bin': image.data,
        'firmware.elf': _elf(image, ivt[1]),
        'strings.txt': ''.join(f'{a:#010x}: {s}\n' for a, s in strings).encode(),
    }
    for name, content in artifacts.items():
        with output_directory.joinpath(name).open('xb') as stream:
            stream.write(content)
    return image


def _elf(image: FirmwareImage, reset_vector: int) -> bytes:
    names = b'\0.text\0.shstrtab\0'
    name_offset = 52 + len(image.data)
    section_offset = (name_offset + len(names) + 3) & ~3
    ident = b'\x7fELF\x01\x01\x01' + bytes(9)
    header = ident + struct.pack(
        '<HHIIIIIHHHHHH',
        2,
        40,
        1,
        reset_vector,
        0,
        section_offset,
        0x05000000,
        52,
        0,
        0,
        40,
        3,
        2,
    )
    sections = bytes(40)
    sections += struct.pack(
        '<10I', 1, 1, 6, image.origin, 52, len(image.data), 0, 0, 4, 0
    )
    sections += struct.pack('<10I', 7, 3, 0, 0, name_offset, len(names), 0, 0, 1, 0)
    wrapped = header + image.data + names
    return wrapped + bytes(section_offset - len(wrapped)) + sections
