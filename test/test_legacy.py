"""Saved musical graphs survive normalization independently of their authoring."""

import struct
from pathlib import Path
from zlib import crc32

import pytest

from cntrl.patch_switcher import compiler, display, legacy, models


@pytest.mark.parametrize('version', [20, 21, 25, 27, 34])
def test_historical_graphs_and_pedal_edited_values_survive(version: int) -> None:
    root = Path(__file__).resolve().parents[1]
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    )
    saved = bytes.fromhex((root / f'test/data/legacy-{version}.golden').read_text())
    assert struct.unpack_from('<H', saved, 4)[0] == version
    # Alter an actual stored volume byte, simulating an edit on the controller.
    edited = bytearray(saved)
    _, _, _, _, ni, nc, np, *_ = compiler.HEADER.unpack_from(saved)
    assert np
    patch = 76 + ni * 68 + nc * 68
    edited[patch + 7] = 91
    old_graph = saved.index(b'CPAT')
    count = struct.unpack_from('<H', saved, old_graph + 8)[0]
    nodes = [
        old_graph + struct.unpack_from('<I', saved, old_graph + 16 + i * 4)[0]
        for i in range(count)
    ]
    program = next(n for n in nodes if saved[n] == 1)
    edited[program + 9] = 91
    struct.pack_into('<I', edited, 20, crc32(edited[28:]))
    converted = legacy.upgrade(bytes(edited), target, ['example'])
    assert struct.unpack_from('<H', converted, 4)[0] == 34
    assert struct.unpack_from('<I', converted, 8)[0] == len(converted)
    assert struct.unpack_from('<I', converted, 20)[0] == crc32(converted[28:])
    assert converted[patch + 7] == 91
    new_graph = converted.index(b'CPAT')
    graph_length = struct.unpack_from('<I', saved, old_graph + 4)[0]
    assert (
        converted[new_graph : new_graph + graph_length]
        == edited[old_graph : old_graph + graph_length]
    )
    assert (
        display.read_source(converted, new_graph)['patch_metadata']
        == display.read_source(saved, old_graph)['patch_metadata']
    )


def test_normalization_rejects_corrupt_saved_bytes() -> None:
    root = Path(__file__).resolve().parents[1]
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    )
    saved = bytes.fromhex((root / 'test/data/legacy-27.golden').read_text())
    with pytest.raises(ValueError, match='checksum'):
        legacy.upgrade(saved[:-1] + bytes([saved[-1] ^ 1]), target, ['example'])
