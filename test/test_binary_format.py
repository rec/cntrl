"""Persistent envelopes preserve opaque data and reject unsupported semantics."""

import struct
from pathlib import Path
from zlib import crc32

import pytest

from cntrl.patch_switcher import binary_format, compiler, models, resolved
from scripts import native_tests


def test_envelope_has_deterministic_bytes_and_covers_its_header() -> None:
    data = binary_format.wrap(b'core')
    assert (
        data.hex() == '434e42462e800100140000000c0000008628ef440100010004000000636f7265'
    )
    for i in (0, 4, 6, 8, 10, 12, 16, 20, len(data) - 1):
        damaged = bytearray(data)
        damaged[i] ^= 1
        with pytest.raises(ValueError):
            binary_format.unpack(bytes(damaged), binary_format.Kind.setup)


def test_unknown_optional_records_survive_core_changes_and_nested_metadata() -> None:
    nested = binary_format.RECORD.pack(301, 0, 3) + b'new'
    document = binary_format.Document(
        kind=binary_format.Kind.setup,
        records=[
            binary_format.Record(tag=1, required=True, data=b'original'),
            binary_format.Record(tag=300, data=nested),
        ],
    )
    updated = binary_format.replace_core(document.pack(), b'changed')
    decoded = binary_format.unpack(updated, binary_format.Kind.setup)
    assert decoded.records[0].data == b'changed'
    assert decoded.records[1:] == document.records[1:]
    with pytest.raises(ValueError, match='opaque metadata'):
        binary_format.require_structural_edit(updated)


@pytest.mark.parametrize(
    'records',
    [
        b'',
        struct.pack('<HHI', 1, 0, 0),
        struct.pack('<HHI', 1, 1, 100),
        struct.pack('<HHI', 1, 1, 0) * 2,
        struct.pack('<HHI', 1, 1, 0) + struct.pack('<HHI', 2, 1, 0),
        struct.pack('<HHI', 1, 1, 0) + struct.pack('<HHI', 2, 2, 0),
    ],
)
def test_invalid_or_unknown_required_records_are_rejected(records: bytes) -> None:
    header = binary_format.HEADER.pack(
        binary_format.MAGIC, binary_format.DEVELOPMENT_FORMAT, 1, 20, 0, len(records), 0
    )
    data = header + records
    data = data[:16] + struct.pack('<I', crc32(data)) + data[20:]
    with pytest.raises(ValueError):
        binary_format.unpack(data, binary_format.Kind.setup)


def test_legacy_import_and_memory_refresh_preserve_optional_records() -> None:
    source = bytes.fromhex(
        (Path(__file__).parents[1] / 'firmware/test/show.golden').read_text()
    )
    document = binary_format.Document(
        kind=binary_format.Kind.setup,
        records=[
            binary_format.Record(tag=1, required=True, data=source),
            binary_format.Record(tag=200, data=b'future metadata'),
        ],
    )
    data = document.pack()
    assert resolved.export(data) == resolved.export(source)
    assert resolved.upgrade(data) == data
    target = models.Target.model_validate(
        compiler.read_document(Path(__file__).parents[1] / 'firmware/test/target.json')
    )
    refreshed = resolved.refresh_memory(data, {}, target=target)
    assert (
        binary_format.unpack(refreshed, binary_format.Kind.setup).records[1:]
        == document.records[1:]
    )
    assert resolved.export(refreshed) == resolved.export(source)


def test_format_names_distinguish_development_and_release() -> None:
    assert binary_format.format_name(binary_format.DEVELOPMENT_FORMAT) == 'd46'
    assert binary_format.format_name(0) == 'f0'
    assert binary_format.format_name(3) == 'f3'


def test_interrupted_activation_keeps_a_complete_generation(
    tmp_path: Path, native_programs: native_tests.NativePrograms
) -> None:
    program = native_programs.build('test/test_deployment.cpp')
    native_tests.run_native([str(program), str(tmp_path)], check=True)


def test_python_and_cpp_share_all_artifact_envelopes(
    tmp_path: Path, native_programs: native_tests.NativePrograms
) -> None:
    root = Path(__file__).parents[1] / 'firmware/test'
    paths = []
    for name, kind in [
        ('show', binary_format.Kind.setup),
        ('bundle', binary_format.Kind.bundle),
        ('songs', binary_format.Kind.songs),
    ]:
        original = bytes.fromhex((root / f'{name}.golden').read_text())
        wrapped = binary_format.wrap(original, kind)
        document = binary_format.unpack(wrapped, kind)
        annotated = binary_format.Document(
            kind=kind,
            records=[
                *document.records,
                binary_format.Record(tag=399, data=b'nested metadata'),
            ],
        ).pack()
        for suffix, data in [('bin', wrapped), ('metadata', annotated)]:
            path = tmp_path / f'{name}.{suffix}'
            path.write_bytes(data)
            paths.append(path)
        bad = bytearray(annotated)
        tail = (
            binary_format.HEADER.size
            + binary_format.RECORD.size
            + len(document.records[0].data)
        )
        struct.pack_into('<H', bad, tail + 2, 1)
        struct.pack_into('<I', bad, 16, 0)
        struct.pack_into('<I', bad, 16, crc32(bad))
        with pytest.raises(ValueError, match='required'):
            binary_format.unpack(bytes(bad), kind)
        path = tmp_path / f'{name}.bad'
        path.write_bytes(bad)
        paths.append(path)
    program = native_programs.build('test/test_binary_format.cpp')
    native_tests.run_native([str(program), *(str(p) for p in paths)], check=True)
