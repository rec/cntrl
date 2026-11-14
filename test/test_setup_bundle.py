"""Setup residency uses one indexed image and a bounded, byte-optimal prefill."""

import struct
from pathlib import Path
from tempfile import TemporaryDirectory

import pytest
import tomlkit

from cntrl.patch_switcher import (
    cli,
    compiler,
    data_files,
    models,
    resolved,
    setup_bundle,
)
from scripts import native_tests


def test_static_table_is_sorted_and_offsets_address_every_setup() -> None:
    bundle = setup_bundle.build(
        {'z': b'zzz', 'a': b'a', 'm': b'mm'},
        {'z': {}, 'a': {}, 'm': {}},
        memory=MEMORY,
    )
    assert bundle.names == ['a', 'm', 'z']
    assert bundle.setup_sizes == [1, 2, 3]
    assert bundle.total_setup_ram == 6
    assert bundle.prefill == [0, 1, 2]
    data = bundle.pack()
    table = setup_bundle.HEADER.size + len(bundle.names) * setup_bundle.ENTRY.size
    assert struct.unpack_from('<HII', data, table) == (3, 1, 3)
    assert data[table + 10 :] == b'ammzzz'
    assert setup_bundle.memory_from_bundle(data) == MEMORY


def test_prefill_maximizes_bytes_when_largest_first_would_waste_space() -> None:
    # Metadata/save workspace occupy 777 bytes. Leave a ten-byte cache.
    bundle = setup_bundle.SetupBundle(
        setups={'a': b'a' * 8, 'b': b'b' * 6, 'c': b'c' * 4},
        page_ram2=[0, 0, 0],
        memory=models.TargetMemory(capacity=400000, runtime=0),
        memory_margin=400000 - 777 - 10,
    )
    assert bundle.total_free_ram2() == 10
    assert bundle.prefill == [1, 2]


def test_active_setup_accounting_does_not_sum_inactive_setups() -> None:
    bundle = setup_bundle.build(
        {'a': b'a', 'b': b'b'},
        {
            'a': {'pages': {'x': {'type': 'patch-switcher', 'ram2_bytes': 100}}},
            'b': {'pages': {'status': {'type': 'status', 'ram2_bytes': 80}}},
        },
        memory=MEMORY,
    )
    assert bundle.total_ram2_needs() == MEMORY.runtime + 528 + 100
    assert (
        bundle.total_free_ram2() == MEMORY.capacity - 16192 - bundle.total_ram2_needs()
    )


def test_setup_fit_is_decided_by_the_linked_firmware_budget() -> None:
    bundle = setup_bundle.build({'large': bytes(300000)}, {'large': {}}, memory=MEMORY)
    assert bundle.total_setup_ram > bundle.total_free_ram2()
    assert setup_bundle.unpack(bundle.pack()) == bundle.setups


@pytest.mark.parametrize('all_resident', [True, False])
def test_native_table_and_lru_preserve_pinned_views(
    all_resident: bool, native_programs: native_tests.NativePrograms
) -> None:
    root = Path(__file__).parents[1]
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    )
    sources = {n: {'brightness': 100 + i} for i, n in enumerate(('a', 'b', 'c'))}
    setups = {n: compiler.compile_show(s, {}, [], target) for n, s in sources.items()}
    size = len(setups['a'])
    page_ram = max(setup_bundle.build(setups, sources, MEMORY).page_ram2)
    margin = (
        16192
        if all_resident
        else MEMORY.capacity - MEMORY.runtime - 777 - page_ram - size * 2
    )
    bundle = setup_bundle.build(setups, sources, MEMORY, margin)
    with TemporaryDirectory(prefix='cntrl-setup-table-test-') as directory:
        image = Path(directory) / 'setups.bin'
        image.write_bytes(bundle.pack())
        program = native_programs.build('test/test_setup_table.cpp')
        native_tests.run_native(
            [str(program), str(image), str(int(all_resident))], check=True
        )


def test_history_translates_indices_after_the_bundle_name_table_changes(
    native_programs: native_tests.NativePrograms,
) -> None:
    root = Path(__file__).parents[1]
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    )
    sources: dict[str, object] = {
        'a': {
            'inline_patches': False,
            'patches': {'borrowed': {'document': 'b', 'patch': 'p'}},
        },
        'b': {
            'patches': {
                'p': {
                    'type': 'control-change',
                    'output': 'din',
                    'channel': 1,
                    'controller': 7,
                    'value': 77,
                }
            }
        },
    }
    first = {
        n: compiler.compile_show(s, {}, [], target, documents=sources)
        for n, s in sources.items()
    }
    expanded = {'0': {}, **sources}
    second = {
        n: compiler.compile_show(s, {}, [], target, documents=expanded)
        for n, s in expanded.items()
    }
    linked = setup_bundle.reindex({'0': second['0'], **first})
    assert linked['a'] == second['a']
    with TemporaryDirectory(prefix='cntrl-bundle-history-test-') as directory:
        old, new = (Path(directory) / n for n in ('old.bin', 'new.bin'))
        old.write_bytes(setup_bundle.build(first, sources, memory=MEMORY).pack())
        new.write_bytes(setup_bundle.build(second, expanded, memory=MEMORY).pack())
        program = native_programs.build('test/test_setup_table.cpp')
        native_tests.run_native(
            [str(program), str(old), 'history', str(new)], check=True
        )


def test_build_collects_dependencies_in_sorted_index_order() -> None:
    root = Path(__file__).parents[1]
    with TemporaryDirectory(prefix='cntrl-bundle-input-test-') as directory:
        folder = Path(directory)
        (folder / 'z.toml').write_text(
            "inline_patches = false\n[patches.x]\ndocument = 'a'\npatch = 'x'\n"
        )
        (folder / 'a.toml').write_text(
            "[patches.x]\ntype = 'control-change'\noutput = 'din'\n"
            'channel = 1\ncontroller = 7\nvalue = 1\n'
        )
        result = cli.compile_inputs(
            data_files.Sources(
                document=[str(folder / 'z.toml')],
                target=[str(root / 'firmware/test/target.json')],
                memory_margin=20000,
            )
        )
        assert result.names == ['a', 'z']
        assert result.memory_margin == 20000
        assert setup_bundle.unpack(result.pack()) == result.setups
        assert (
            tomlkit.parse(resolved.export(result.setups['z'])).unwrap()['patches']['x'][
                'document'
            ]
            == 'a'
        )


MEMORY = models.TargetMemory(capacity=400000, runtime=240000)
