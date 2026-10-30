"""Keep the repository's authored setups and song library ready to compile."""

from pathlib import Path

import pytest

from cntrl.patch_switcher import compiler, models, resolved, songs
from cntrl.patch_switcher.cli import save_font


@pytest.mark.parametrize(
    ('path', 'definitions', 'tables'),
    [
        (
            'examples/patch-switcher/patches.toml',
            'examples/patch-switcher/definitions.toml',
            [
                'examples/patch-switcher/table.toml',
                'examples/patch-switcher/tags.toml',
            ],
        ),
        (
            'examples/patch-switcher/pman.toml',
            'instruments/definitions.toml',
            ['instruments/patchman.toml', 'instruments/tags.toml'],
        ),
        ('cntrl/maker/morningstar/mc/font-demo.toml', None, []),
        ('cntrl/maker/morningstar/mc/patch-functions-demo.toml', None, []),
        ('cntrl/maker/morningstar/mc/setups/pman.toml', None, []),
        ('cntrl/maker/morningstar/mc/setups/font-demo.toml', None, []),
        ('cntrl/maker/morningstar/mc/setups/sd-font-demo.toml', None, []),
    ],
)
def test_authored_setups_compile_and_round_trip(
    path: str, definitions: str | None, tables: list[str], tmp_path: Path
) -> None:
    target = models.Target.model_validate(compiler.read_document(TARGET))
    assets: dict[int, bytes] = {}
    data = compiler.compile_show(
        compiler.read_document(Path(path)),
        compiler.read_document(Path(definitions)) if definitions else {},
        [compiler.read_document(Path(p)) for p in tables],
        target,
        assets,
    )
    for key, font in assets.items():
        save_font(tmp_path, key, font)
    exported_path = tmp_path / 'exported.toml'
    exported_path.write_text(resolved.export(data))
    exported = compiler.read_document(exported_path)
    assert compiler.compile_show(exported, {}, [], target) == data


@pytest.mark.parametrize(
    'path',
    [
        'cntrl/maker/morningstar/mc/patch-functions-songs.toml',
        'cntrl/maker/morningstar/mc/setups/songs.toml',
    ],
)
def test_authored_song_library_compiles_and_round_trips(path: str) -> None:
    library = songs.Library.model_validate(compiler.read_document(Path(path)))
    target = models.Target.model_validate(compiler.read_document(TARGET))
    data = songs.encode(library, target)
    assert songs.encode(songs.decode(data), target) == data


@pytest.mark.parametrize('instrument', ['patchman', 'vl70m'])
def test_instrument_catalogues_match_declared_setups(instrument: str) -> None:
    definitions = models.Definitions.model_validate(
        compiler.read_document(Path('instruments/definitions.toml'))
    )
    table = compiler.read_document(Path('instruments') / f'{instrument}.toml')
    assert isinstance(table, dict)
    patches = [
        models.TablePatch.model_validate(
            (p if isinstance(p, dict) else {'name': p})
            | {
                'instrument': instrument,
                'setup': 'default',
                'bank': int(b),
                'program': n,
            }
        )
        for b, v in table['instruments'][instrument]['programs'].items()
        for n, p in enumerate(v, 1)
    ]
    assert patches
    for patch in patches:
        setup = next(
            s
            for s in definitions.setups
            if (s.instrument, s.name) == (patch.instrument, patch.setup)
        )
        assert patch.bank is None or patch.bank in setup.banks


TARGET = Path('cntrl/maker/morningstar/mc/patch-switcher-target.json')
