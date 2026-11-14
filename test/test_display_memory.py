"""Display memory controls survive compilation and explicit format conversion."""

import struct
from pathlib import Path
from zlib import crc32

import pytest
import tomlkit
from pydantic import ValidationError

from cntrl.patch_switcher import binary_format, compiler, models, resolved, setup_bundle


@pytest.mark.parametrize('frame_buffers', [0, 1, 2])
@pytest.mark.parametrize('use_animations', [False, True])
def test_display_memory_options_round_trip_and_refresh_the_same_budget(
    frame_buffers: int, use_animations: bool
) -> None:
    target = models.Target.model_validate(compiler.read_document(ROOT / 'target.json'))
    source = {
        'frame_buffers': frame_buffers,
        'use_animations': use_animations,
        'fill_in_styles': False,
    }
    data = compiler.compile_show(source, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['frame_buffers'] == frame_buffers
    assert exported['use_animations'] is use_animations
    assert compiler.compile_show(exported, {}, [], target) == data
    assert resolved.refresh_memory(data, {}, target=target) == data


@pytest.mark.parametrize(
    'field,value',
    [
        ('frame_buffers', -1),
        ('frame_buffers', 3),
        ('frame_buffers', True),
        ('frame_buffers', 1.0),
        ('frame_buffers', '1'),
        ('use_animations', 0),
        ('use_animations', 'false'),
    ],
)
def test_display_memory_options_require_the_declared_types_and_range(
    field: str, value: object
) -> None:
    with pytest.raises(ValidationError):
        models.Setup.model_validate({field: value})


def test_fewer_frame_buffers_leave_more_capacity_for_midi_storage() -> None:
    target = models.Target.model_validate(compiler.read_document(ROOT / 'target.json'))
    free = []
    for count in range(3):
        source = {'frame_buffers': count, 'fill_in_styles': False}
        data = compiler.compile_show(source, {}, [], target)
        planned = setup_bundle.build({'live': data}, {'live': source}, target.memory)
        free.append(planned.total_free_ram2())
    assert free[0] > free[1] > free[2]
    assert (
        free[0] - free[2] == 4 * sum(d.width * d.height for d in target.displays) + 16
    )


@pytest.mark.parametrize('version', [44, 45])
def test_previous_development_setup_requires_upgrade_and_keeps_defaults(
    version: int,
) -> None:
    core = bytes.fromhex((ROOT / f'setup{version}.golden').read_text())
    previous = bytearray(binary_format.wrap(core))
    struct.pack_into('<H', previous, 4, 0x8000 | version)
    struct.pack_into('<I', previous, 16, 0)
    struct.pack_into('<I', previous, 16, crc32(previous))
    assert (
        binary_format.unpack(bytes(previous), binary_format.Kind.setup).records[0].data
        == core
    )
    assert struct.unpack_from('<H', core, 4)[0] == version
    upgraded = resolved.upgrade(bytes(previous))
    exported = tomlkit.parse(resolved.export(upgraded)).unwrap()
    assert exported['frame_buffers'] == 2
    assert exported['use_animations'] is True
    assert binary_format.core(upgraded) == resolved.upgrade(core)
    assert (
        struct.unpack_from('<H', binary_format.core(upgraded), 4)[0]
        == compiler.DOCUMENT_VERSION
    )
    previous[-1] ^= 1
    with pytest.raises(ValueError, match='checksum mismatch'):
        resolved.upgrade(bytes(previous))


ROOT = Path(__file__).parents[1] / 'firmware/test'
