"""Generate the Doom flash cache from repeatable E1M1 opening traces."""

import struct
import subprocess
import sys
from collections import Counter
from pathlib import Path

import tyro
from pydantic import BaseModel


class FlashCache(BaseModel, frozen=True):
    profiler: Path
    image: Path
    output: Path


def main() -> None:
    options = tyro.cli(FlashCache)
    try:
        build(options.profiler, options.image, options.output)
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        sys.exit(str(error))


def build(profiler: Path, image: Path, output: Path) -> None:
    assets = image.read_bytes()
    if len(assets) != BLOCK_SIZE * BLOCK_COUNT:
        raise ValueError('expected a 4 MiB Doom E1M1 asset image')
    traces = {
        n: trace_opening(profiler, image, actions) for n, actions in OPENINGS.items()
    }
    misses: Counter[int] = Counter()
    for trace in traces.values():
        misses.update(profile_reads(trace, set())[0])
    hot = sorted(misses, key=lambda b: (-misses[b], b))[:MAX_FLASH_BLOCKS]
    if not hot:
        raise ValueError('Doom profile produced no cacheable reads')

    index = bytearray(BLOCK_COUNT)
    for slot, block in enumerate(hot, start=1):
        index[block] = slot
    data = bytes(index) + b''.join(
        assets[b * BLOCK_SIZE : (b + 1) * BLOCK_SIZE] for b in hot
    )
    write_flash_source(output, data)
    print(f'Doom flash cache: {len(hot)} blocks, {len(data):,} bytes')
    for name, trace in traces.items():
        before = profile_reads(trace, set())[1]
        after = profile_reads(trace, set(hot))[1]
        print(
            f'{name}: {sum(before):,} to {sum(after):,} reads, '
            f'p95 {sorted(before)[95 * len(before) // 100]} to '
            f'{sorted(after)[95 * len(after) // 100]} reads/frame'
        )


def trace_opening(
    profiler: Path, image: Path, actions: list[tuple[int, int, int]]
) -> bytes:
    # One button mask per 35 Hz game tic, indexed by the host's game clock.
    buttons = bytearray(30 * 35 + 1)
    for start, end, mask in actions:
        for i in range(start * 35, end * 35):
            buttons[i] |= mask
    result = subprocess.run(
        [str(profiler), str(image)],
        input=buttons,
        capture_output=True,
        check=False,
        timeout=120,
    )
    if result.returncode:
        raise ValueError(result.stderr.decode(errors='replace').strip())
    return result.stdout


def profile_reads(trace: bytes, hot: set[int]) -> tuple[Counter[int], list[int]]:
    slots = [-1] * RAM_SLOTS
    misses: Counter[int] = Counter()
    frames: list[int] = []
    reads = 0
    for (block,) in struct.iter_unpack('<H', trace):
        if block == FRAME:
            frames.append(reads)
            reads = 0
        elif block == BULK_READ:
            reads += 1
        elif block >= BLOCK_COUNT:
            raise ValueError('invalid block in Doom asset trace')
        elif block not in hot and slots[block % RAM_SLOTS] != block:
            slots[block % RAM_SLOTS] = block
            misses[block] += 1
            reads += 1
    if not frames or reads:
        raise ValueError('incomplete Doom asset trace')
    return misses, frames


def write_flash_source(output: Path, data: bytes) -> None:
    with output.open('w') as stream:
        stream.write(
            '#include <stdint.h>\n\n'
            '#ifdef __IMXRT1062__\n'
            '__attribute__((section(".progmem.doom_flash_cache")))\n'
            '#endif\n'
            'const uint8_t doom_flash_cache[] = {\n'
        )
        for start in range(0, len(data), 16):
            stream.write(
                '    '
                + ', '.join(f'0x{x:02x}' for x in data[start : start + 16])
                + ',\n'
            )
        stream.write('};\n')


BLOCK_SIZE = 4096
BLOCK_COUNT = 1024
RAM_SLOTS = 16
MAX_FLASH_BLOCKS = (256 * 1024 - BLOCK_COUNT) // BLOCK_SIZE
FRAME = 65535
BULK_READ = FRAME - 1

# Physical bits for right, forward, left, fire, backward and use.
RIGHT, FORWARD, LEFT, FIRE, BACK, USE = (1 << i for i in range(6))
OPENINGS = {
    'idle': [],
    'look around': [(2, 5, LEFT), (8, 11, RIGHT), (16, 20, RIGHT), (25, 28, LEFT)],
    'advance and fire': [
        (1, 5, FORWARD),
        (4, 10, FIRE),
        (10, 12, LEFT),
        (12, 17, FORWARD),
        (19, 26, FIRE),
        (20, 23, BACK),
    ],
    'explore and use': [
        (1, 6, FORWARD),
        (6, 7, USE),
        (8, 9, RIGHT),
        (9, 14, FORWARD),
        (14, 15, USE),
        (15, 17, FIRE),
        (18, 19, LEFT),
        (20, 25, FORWARD),
        (26, 27, USE),
    ],
}


if __name__ == '__main__':
    main()
