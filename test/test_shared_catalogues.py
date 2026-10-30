"""One MIDI action can describe the aligned sounds on several instruments."""

import struct
import subprocess
from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, display, models, resolved
from test.test_display_format import lines


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )


def test_dual_internal_setup_has_one_program_action_and_both_names(
    target: models.Target,
    tmp_path: Path,
) -> None:
    document = compiler.read_document(
        Path('examples/patch-switcher/dual-internal.toml')
    )
    tags = compiler.read_document(Path('instruments/tags.toml'))
    data = compiler.compile_show(document, {}, [tags], target)
    header = compiler.HEADER.unpack_from(data)
    assert (header[4], header[6]) == (1, 64)
    assert data[76 + 64] == 0  # One route, MIDI channel 1.
    graph = resolved.graph_offset(data)
    assert compiler.GRAPH.unpack_from(data, graph)[2] == 64
    source = display.read_source(data, graph)
    for metadata in source['patch_metadata'].values():
        sounds = metadata['_program']['names']
        expected = list(
            dict.fromkeys(
                t
                for i, c in tags['instruments'].items()
                for t, v in c['tags'].items()
                if sounds.get(i) in v
            )
        )
        assert metadata['_program']['tags'] == expected
    program = source['patch_metadata']['1']['_program']
    assert program['names'] == {'vl70m': 'Igneous', 'patchman': '!XTREME!'}
    assert program['tags'] == ['synth']
    assert source['patch_metadata']['20']['_program']['tags'] == ['synth', 'reed']
    block = data[graph + compiler.GRAPH.unpack_from(data, graph)[1] :]
    # Second browser patch's right window uses its two distinct sound labels.
    reference = struct.unpack_from('<H', data, 76 + 68 + 68 + 82 + 80)[0]
    assert [v[3] for v in lines(block, reference)] == [
        'Internal',
        'Igneous',
        '!XTREME!',
    ]
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert compiler.compile_show(exported, {}, [], target) == data
    image = tmp_path / 'shared.bin'
    image.write_bytes(data)
    root = Path(__file__).resolve().parents[1]
    executable = tmp_path / 'shared-catalogues'
    subprocess.run(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_shared_catalogues.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable), str(image)], check=True)


def test_named_sound_fields_render_and_reject_unknown_names(
    target: models.Target,
) -> None:
    show = models.Show.model_validate(
        {
            'pages': {'patch': {'fields': ['names.stock', 'names.modified']}},
        }
    )
    patch = display.DisplayPatch(
        values={'names.stock': 'Flute', 'names.modified': 'Trumpet'}
    )
    _, references, block = display.compile_display(show, [patch], target)
    assert [v[3] for v in lines(block, references[0][0])] == ['Flute', 'Trumpet']
    with pytest.raises(ValueError, match='unknown fields'):
        display.compile_display(
            show.model_copy(
                update={'pages': {'patch': models.Page(fields=['names.typo'])}}
            ),
            [patch],
            target,
        )


@pytest.mark.parametrize(
    'second,error',
    [
        ("[instruments.b]\nprograms = ['Other', 'Extra']\n", 'same program count'),
        (
            "[instruments.b.programs]\n0 = ['Other']\n1 = ['Extra']\n",
            'single nonempty bank',
        ),
        ("[instruments.a]\nprograms = ['Other']\n", 'must be distinct'),
        ('[instruments.b]\nprograms = [{}]\n', 'needs a name'),
    ],
)
def test_shared_catalogues_reject_misalignment(
    tmp_path: Path, second: str, error: str
) -> None:
    (tmp_path / 'first.toml').write_text("[instruments.a.programs]\n3 = ['Original']\n")
    (tmp_path / 'second.toml').write_text(second)
    source = tmp_path / 'show.toml'
    source.write_text(
        "[instruments.both]\ncatalogues = ['first.toml', 'second.toml']\n"
    )
    with pytest.raises(ValueError, match=error):
        compiler.read_document(source)


def test_catalogue_paths_are_relative_and_cycles_are_rejected(tmp_path: Path) -> None:
    source = tmp_path / 'show.toml'
    source.write_text("[instruments.both]\ncatalogues = ['missing.toml']\n")
    with pytest.raises(FileNotFoundError):
        compiler.read_document(source)
    source.write_text("[instruments.both]\ncatalogues = ['show.toml']\n")
    with pytest.raises(ValueError, match='reference cycle'):
        compiler.read_document(source)


def test_numeric_cycle_entries_require_one_route(target: models.Target) -> None:
    show = {
        'instruments': {
            'a': {'output': 'din', 'channel': 1},
            'b': {'output': 'trs', 'channel': 1},
        },
        'patch_cycles': {'All': [1]},
    }
    with pytest.raises(compiler.CompilationError, match='exactly one instrument'):
        compiler.compile_show(show, {}, [], target)
