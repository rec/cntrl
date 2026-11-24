import struct
from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_chooser import (
    autocycles,
    binary_format,
    cli,
    compiler,
    compiler_errors,
    compiler_inputs,
    data_files,
    global_config,
    models,
    resolved,
    setup_sync,
    setup_sync_sources,
    setup_sync_store,
)
from scripts import native_tests


def test_pedal_adds_and_reloads_previously_unused_programs(
    tmp_path: Path, native_programs: native_tests.NativePrograms
) -> None:
    source = tmp_path / 'show.toml'
    source.write_text(
        "[instruments.synth]\nchannel = 2\noutput = 'din'\n"
        "programs = { 1 = [{}, 'Keys', 'Brass'], 3 = ['Brass'] }\n"
        "tags = { brass = ['Brass'] }\n"
        "[[definitions.profiles]]\ninstrument = 'synth'\nname = 'default'\n"
        'banks = { 1 = { lsb = 1 }, 3 = { lsb = 3 } }\n'
        '[patch_cycles]\nOriginal = [{ program = 2, bank = 1 }]\n'
    )
    target = tmp_path / 'target.toml'
    description = compiler_inputs.read_document(Path('firmware/test/target.json'))
    assert isinstance(description, dict)
    target.write_text(tomlkit.dumps(description | {'max_patches': 6}))
    inputs = data_files.Sources(document=[f'Show={source}'], target=[str(target)])
    trace = data_files.Trace()
    original = cli.compile_inputs(inputs, trace=trace).setups['Show']
    raw, saved = tmp_path / 'before.bin', tmp_path / 'after.bin'
    raw.write_bytes(original)
    native_tests.run_native(
        [
            str(native_programs.build('test/test_chooser.cpp')),
            '--automatic-cycle',
            str(raw),
            str(saved),
        ]
    )
    host = setup_sync.canonical(original)
    pedal = setup_sync.canonical(saved.read_bytes())
    assert pedal['patch_cycles'] == {
        **host['patch_cycles'],
        'synth brass': {'instrument': 'synth', 'tag': 'brass'},
    }
    authored = tomlkit.parse(source.read_text())
    for edit in setup_sync.merge_changes(
        'Show',
        *[setup_sync_store.Value(present=True, value=v) for v in (host, host, pedal)],
    ):
        change = setup_sync_sources.destination(edit, trace, host, pedal)
        assert change.choice == 'pedal', change
        setup_sync_sources.apply_change(authored, change)
    source.write_text(tomlkit.dumps(authored))
    recompiled = cli.compile_inputs(inputs).setups['Show']
    assert setup_sync.canonical(recompiled) == pedal
    source.write_text(
        source.read_text().replace("brass = ['Brass']", "brass = ['Keys']")
    )
    updated = binary_format.core(cli.compile_inputs(inputs).setups['Show'])
    graph = resolved.graph_offset(updated)
    _, _, nodes, _, roots, _ = resolved.GRAPH.unpack_from(updated, graph)
    assert roots == 2
    selection = struct.unpack_from(
        '<H', updated, graph + resolved.GRAPH.size + 4 * nodes + 2
    )[0]
    assert resolved.patch(updated, selection)['program'] == 2


def test_catalogue_cycles_preserve_program_addresses_and_export_selectors() -> None:
    target = models.Target.model_validate(
        compiler_inputs.read_document(Path('firmware/test/target.json'))
    )
    source = {
        'instruments': {
            'a': {'instrument': 'synth', 'channel': 1, 'output': 'din'},
            'b': {
                'instrument': 'synth',
                'channel': 2,
                'output': 'din',
                'programs': {'3': ['Brass'], '1': [{}, 'Keys', 'Brass']},
                'tags': {'brass': ['Brass']},
            },
        },
        'definitions': {
            'profiles': [
                {
                    'instrument': 'synth',
                    'name': 'default',
                    'banks': {1: {'lsb': 1}, 3: {'lsb': 3}},
                }
            ]
        },
        'patch_cycles': {
            'All': {'instrument': 'a'},
            'Brass': {'instrument': 'b', 'tag': 'brass'},
        },
    }
    image = compiler.compile_show(source, {}, [], target)
    restored = tomlkit.parse(resolved.export(image)).unwrap()
    assert restored['patch_cycles'] == source['patch_cycles']
    raw = binary_format.core(image)
    graph = resolved.graph_offset(raw)
    _, _, nodes, _, roots, _ = resolved.GRAPH.unpack_from(raw, graph)
    assert roots == 5
    indexes = struct.unpack_from('<5H', raw, graph + resolved.GRAPH.size + 4 * nodes)
    assert [
        (p['instrument'], p['bank'], p['program'])
        for i in indexes
        if (p := resolved.patch(raw, i))
    ] == [('a', 1, 2), ('a', 1, 3), ('a', 3, 1), ('b', 1, 3), ('b', 3, 1)]
    source['patch_cycles']['Brass']['tag'] = 'missing'
    with pytest.raises(compiler_errors.CompilationError, match='patch cycle is empty'):
        compiler.compile_show(source, {}, [], target)


def test_add_cycle_accepts_default_selection_without_typing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    setup = tmp_path / 'show.toml'
    instruments = tmp_path / 'synth.toml'
    instruments.write_text(
        "[instruments.synth]\noutput = 'din'\nchannel = 1\nprograms = ['Piano']\n"
    )
    setup.write_text("instrument_files = ['synth.toml']\n# Keep this comment\n")
    monkeypatch.setattr(global_config, 'user_path', lambda: tmp_path / 'global.toml')
    replies = iter(['', '', ''])
    monkeypatch.setattr('builtins.input', lambda _: next(replies))
    autocycles.run(autocycles.AddCycle(setup=setup))
    result = setup.read_text()
    assert '# Keep this comment' in result
    assert tomlkit.parse(result)['patch_cycles'] == {'synth': {'instrument': 'synth'}}
