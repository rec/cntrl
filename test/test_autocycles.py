import struct
from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_chooser import (
    autocycles,
    binary_format,
    compiler,
    compiler_errors,
    compiler_inputs,
    global_config,
    models,
    resolved,
)


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
