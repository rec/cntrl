"""Legacy conversion preserves addresses, metadata and category membership."""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
from types import ModuleType

import pytest

from cntrl.patch_switcher import compiler, models


@pytest.fixture
def importer() -> ModuleType:
    spec = spec_from_file_location('import_old_instruments', SCRIPT)
    assert spec is not None and spec.loader is not None
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_converts_two_instruments_and_joins_categories_by_address(
    importer: ModuleType,
) -> None:
    tables = importer.convert_instruments(
        {
            'stock': {
                'A': {'001': ['Flute', 'C1 - C6', 'A description']},
                'B': {'128': ['Other flute', '***', 'Unknown range']},
            },
            'patchman': {'A': {'001': ['Trumpet']}, 'B': {'128': ['Bass']}},
        },
        {
            'wind': ['stock:A:001: Old display name', 'patchman:A:001: Trumpet'],
            'flute': ['stock:A:001: Flute'],
        },
    )
    assert list(tables) == ['vl70', 'pman']
    assert tables['vl70'][0] == models.TablePatch(
        instrument='vl70',
        setup='default',
        program=1,
        bank=0,
        name='Flute',
        range='C1 - C6',
        description='A description',
        tags=['wind', 'flute'],
    )
    assert tables['vl70'][1].program == 128 and tables['vl70'][1].bank == 1
    assert tables['vl70'][1].range == '***'
    assert tables['pman'][0].description is None
    assert tables['pman'][0].tags == ['wind']
    assert tables['pman'][1].tags == []


def test_rejects_dangling_category_references(importer: ModuleType) -> None:
    with pytest.raises(KeyError):
        importer.convert_instruments(
            {'stock': {'A': {}}}, {'wind': ['stock:A:001: Flute']}
        )


def test_emits_valid_files_without_inventing_routing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, importer: ModuleType
) -> None:
    instruments = tmp_path / 'legacy.json'
    instruments.write_text(
        '{"stock":{"A":{"001":["Flute"]}},"patchman":{"B":{"128":["Bass"]}}}'
    )
    categories = tmp_path / 'categories.json'
    categories.write_text('{"wind":["stock:A:001: Flute"]}')
    monkeypatch.setattr(importer, 'INSTRUMENTS_PATH', instruments)
    monkeypatch.setattr(importer, 'CATEGORIES_PATH', categories)
    output = tmp_path / 'output'
    importer.import_instruments(output)
    definitions = models.Definitions.model_validate(
        compiler.read_document(output / 'definitions.toml')
    )
    assert [(s.instrument, s.name) for s in definitions.setups] == [
        ('vl70', 'default'),
        ('pman', 'default'),
    ]
    assert definitions.setups[0].banks[0] == models.Bank(msb=33, lsb=0)
    table = compiler.read_document(output / 'pman.toml')
    assert isinstance(table, dict)
    assert models.TablePatch.model_validate(table['patches'][0]).program == 128
    assert sorted(p.name for p in output.iterdir()) == [
        'definitions.toml',
        'pman.toml',
        'vl70.toml',
    ]


SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/import-old-instruments.py'
