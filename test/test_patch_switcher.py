"""Compiler behavior, shared binary fixture and aggregate diagnostics."""

import sys
from copy import deepcopy
from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import cli, compiler, models, resolved


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(FIXTURES / 'target.json')
    )


@pytest.fixture
def definitions() -> object:
    return compiler.read_document(FIXTURES / 'definitions.json')


@pytest.fixture
def show() -> dict[str, object]:
    value = compiler.read_document(FIXTURES / 'show.json')
    assert isinstance(value, dict)
    return value


def test_compiler_matches_shared_cpp_fixture(
    show: dict[str, object], definitions: object, target: models.Target
) -> None:
    result = compiler.compile_show(show, definitions, [], target)
    assert result.hex() == (FIXTURES / 'show.golden').read_text().strip()
    assert compiler.HEADER.unpack_from(result)[2:7] == (100, len(result), 2, 2, 4)
    assert compiler.HEADER.unpack_from(result)[1] == 8
    assert compiler.HEADER.unpack_from(result)[7] == 192 | 256


@pytest.mark.parametrize('duration', [1000, 2000, 750, 65535])
def test_transition_duration_survives_resolved_export(
    show: dict[str, object], definitions: object, target: models.Target, duration: int
) -> None:
    show['transition_ms'] = duration
    data = compiler.compile_show(show, definitions, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['transition_ms'] == duration
    assert compiler.compile_show(exported, {'setups': []}, [], target) == data


def test_patch_table_uses_filename_only_for_missing_instruments(tmp_path: Path) -> None:
    path = tmp_path / 'pman.toml'
    path.write_text(
        '[[patches]]\nprogram = 1\n'
        '[[patches]]\nprogram = 2\ninstrument = "vl70"\n'
        '[[patches]]\nprogram = 3\ninstrument = ""\n'
    )
    table = compiler.read_document(path)
    assert isinstance(table, dict)
    assert [p['instrument'] for p in table['patches']] == ['pman', 'vl70', '']


@pytest.mark.parametrize('enabled', [None, False, True])
def test_usb_midi_switch_round_trips(
    show: dict[str, object],
    definitions: object,
    target: models.Target,
    enabled: bool | None,
) -> None:
    assert models.Show.model_validate(show).usb_midi_interface is False
    if enabled is not None:
        show['usb_midi_interface'] = enabled
    data = compiler.compile_show(show, definitions, [], target)
    assert bool(data[19] & 2) is (enabled is True)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['usb_midi_interface'] is (enabled is True)
    assert compiler.compile_show(exported, {'setups': []}, [], target) == data


@pytest.mark.parametrize('value', [1, 'true', None])
def test_usb_midi_switch_rejects_non_booleans(
    show: dict[str, object], definitions: object, target: models.Target, value: object
) -> None:
    show['usb_midi_interface'] = value
    with pytest.raises(compiler.CompilationError, match='usb_midi_interface'):
        compiler.compile_show(show, definitions, [], target)


@pytest.mark.parametrize('brightness', [0, 32, 255])
def test_brightness_is_encoded_in_show(
    show: dict[str, object], definitions: object, target: models.Target, brightness: int
) -> None:
    show['brightness'] = brightness
    result = compiler.compile_show(show, definitions, [], target)
    assert compiler.HEADER.unpack_from(result)[7] == brightness | 256


@pytest.mark.parametrize('splash', [None, True, False])
def test_splash_defaults_to_enabled_unless_explicitly_disabled(
    show: dict[str, object],
    definitions: object,
    target: models.Target,
    splash: bool | None,
) -> None:
    assert models.Show.model_validate(show).splash
    if splash is not None:
        show['splash'] = splash
    result = compiler.compile_show(show, definitions, [], target)
    assert result[18:20] == bytes([192, int(splash is not False)])


@pytest.mark.parametrize('seconds', [0, 3, 2147483])
def test_screensaver_timeout_is_encoded_in_milliseconds(
    show: dict[str, object], definitions: object, target: models.Target, seconds: int
) -> None:
    assert models.Show.model_validate(show).screensaver_timeout == 0
    show['screensaver_timeout'] = seconds
    result = compiler.compile_show(show, definitions, [], target)
    assert compiler.HEADER.unpack_from(result)[9] == seconds * 1000


@pytest.mark.parametrize('seconds', [-1, 2147484, True, 0.5, '3'])
def test_invalid_screensaver_timeout_is_rejected(
    show: dict[str, object], definitions: object, target: models.Target, seconds: object
) -> None:
    show['screensaver_timeout'] = seconds
    with pytest.raises(compiler.CompilationError, match='screensaver_timeout'):
        compiler.compile_show(show, definitions, [], target)


@pytest.mark.parametrize('brightness', [-1, 256, True, 1.5, '64'])
def test_invalid_brightness_is_rejected(
    show: dict[str, object],
    definitions: object,
    target: models.Target,
    brightness: object,
) -> None:
    show['brightness'] = brightness
    with pytest.raises(compiler.CompilationError, match='brightness'):
        compiler.compile_show(show, definitions, [], target)


def test_toml_json_and_expanded_shorthand_agree(
    show: dict[str, object], definitions: object, target: models.Target
) -> None:
    import tomlkit

    toml = tomlkit.parse(tomlkit.dumps(show)).unwrap()
    assert compiler.compile_show(
        toml, definitions, [], target
    ) == compiler.compile_show(show, definitions, [], target)
    assert compiler.normalize_patch(
        {'patch': ':a:12:::0', 'volume': 100, 'name': 'Flute'}
    ) == models.Patch(instrument='a', program=12, bank=0, volume=100, name='Flute')
    assert compiler.normalize_patch({'patch': '|a|Name:.*'}).program == 'Name:.*'
    assert compiler.normalize_patch({'patch': ':a:^12$'}).program == '^12$'


@pytest.mark.parametrize(
    'value',
    [
        {'patch': ':a:12:100', 'volume': 100},
        {'patch': ':a:12:::-'},
        {'patch': ':a:129'},
        {'patch': ':a:0'},
        {'patch': ':a:1:2:3:4:5'},
        {'patch': ':'},
    ],
)
def test_invalid_shorthand_is_rejected(value: object) -> None:
    with pytest.raises(ValueError):
        compiler.normalize_patch(value)


def test_reports_errors_across_all_patch_entries(
    show: dict[str, object], definitions: object, target: models.Target
) -> None:
    show['patches'] = {
        'wind': [
            {'instrument': 'a', 'program': 0, 'volume': 128, 'pan': 64},
            {'patch': ':a:[invalid'},
            {'patch': ':missing:1'},
            {'patch': ':a:no match'},
            {'patch': ':a:12', 'name': '☃'},
            {'patch': ':a:1', 'bank': 999},
        ]
    }
    with pytest.raises(compiler.CompilationError) as failure:
        compiler.compile_show(show, definitions, [], target)
    message = str(failure.value)
    for index in range(1, 7):
        assert f'wind[{index}]' in message
    assert 'volume' in message and 'pan' in message
    assert '☃' in message and 'bank 999' in message


@pytest.mark.parametrize('take_first', [False, True])
def test_zero_matches_always_fail(
    show: dict[str, object],
    definitions: object,
    target: models.Target,
    take_first: bool,
) -> None:
    show['take_first_match'] = take_first
    show['patches'] = {'wind': [{'patch': ':a:absent'}]}
    with pytest.raises(compiler.CompilationError, match='no patch matches'):
        compiler.compile_show(show, definitions, [], target)


def test_regex_search_uses_first_seen_order_and_explicit_bank_filter(
    show: dict[str, object], definitions: object, target: models.Target
) -> None:
    table = {
        'patches': [
            {
                'instrument': 'synth',
                'setup': 'stock',
                'program': 9,
                'name': 'My Flute',
                'bank': 0,
            },
            {
                'instrument': 'synth',
                'setup': 'stock',
                'program': 2,
                'name': 'Other Flute',
            },
        ]
    }
    show['patches'] = {'wind': [{'patch': ':a:Flute'}]}
    with pytest.raises(compiler.CompilationError, match='multiple matches'):
        compiler.compile_show(show, definitions, [table], target)
    show['take_first_match'] = True
    data = compiler.compile_show(show, definitions, [table], target)
    patch_offset = 76 + 2 * 68 + 74
    assert data[patch_offset + 4 : patch_offset + 7] == bytes([8, 1, 2])
    show['take_first_match'] = False
    show['patches'] = {'wind': [{'patch': ':a:Flute', 'bank': 0}]}
    assert compiler.compile_show(show, definitions, [table], target) == data
    show['patches'] = {'wind': [{'patch': ':a:^Flute$'}]}
    with pytest.raises(compiler.CompilationError, match='no patch matches'):
        compiler.compile_show(show, definitions, [table], target)


def test_table_conflicts_are_collected_and_missing_metadata_is_filled(
    show: dict[str, object], definitions: object, target: models.Target
) -> None:
    base = {'instrument': 'synth', 'setup': 'stock', 'program': 1}
    show['patches'] = {'wind': [{'patch': ':a:Flute'}]}
    first = {'patches': [dict(base, tags=['wind'])]}
    second = {'patches': [dict(base, name='Flute', tags=['wind', 'acoustic'])]}
    compiler.compile_show(show, definitions, [first, second], target)
    conflict = {
        'patches': [dict(base, name='Sax', description='new'), dict(base, name='Oboe')]
    }
    with pytest.raises(compiler.CompilationError) as failure:
        compiler.compile_show(show, definitions, [second, conflict], target)
    assert sum('conflicting name' in e for e in failure.value.errors) == 2


def test_bankless_pc_is_not_bank_zero_and_omission_sends_no_bank(
    show: dict[str, object], definitions: object, target: models.Target
) -> None:
    show['patches'] = {'wind': [{'patch': ':a:12'}]}
    data = compiler.compile_show(show, definitions, [], target)
    offset = 76 + 2 * 68 + 74
    assert data[offset + 4 : offset + 9] == bytes([11, 255, 255, 255, 255])
    assert data[offset + 12 : offset + 17] == b'PC 12'
    show['patches'] = {'wind': [{'patch': ':a:12:::0'}]}
    data = compiler.compile_show(show, definitions, [], target)
    assert data[offset + 5 : offset + 7] == bytes([1, 2])
    assert b'Bank 0 / PC 12' in data


def test_capacity_and_all_display_fields_are_checked(
    show: dict[str, object], definitions: object, target: models.Target
) -> None:
    show['patches'] = {'bad\ncategory': [{'patch': ':a:1', 'name': 'x' * 64}]}
    small = target.model_copy(update={'max_document_bytes': 100})
    with pytest.raises(compiler.CompilationError) as failure:
        compiler.compile_show(show, definitions, [], small)
    assert len(failure.value.errors) >= 3


def test_setup_sysex_files_are_not_read_or_encoded(
    show: dict[str, object], definitions: object, target: models.Target
) -> None:
    assert isinstance(definitions, dict)
    modified = deepcopy(definitions)
    modified['setups'][0]['sysex'] = ['/does/not/exist.syx']
    assert compiler.compile_show(show, modified, [], target) == compiler.compile_show(
        show, definitions, [], target
    )


def test_bad_category_does_not_hide_errors_in_other_categories(
    show: dict[str, object], definitions: object, target: models.Target
) -> None:
    show['patches'] = {'bad': 1, 'wind': [{'patch': ':missing:12'}]}
    with pytest.raises(compiler.CompilationError) as failure:
        compiler.compile_show(show, definitions, [], target)
    assert 'patches.bad' in str(failure.value)
    assert 'patches.wind[1]' in str(failure.value)


def test_cli_compiles_and_reports_counts_and_routes(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    output = tmp_path / 'show.bin'
    monkeypatch.setattr(
        sys,
        'argv',
        [
            'cntrl',
            'compile',
            str(FIXTURES / 'show.json'),
            '--definitions',
            str(FIXTURES / 'definitions.json'),
            '--target',
            str(FIXTURES / 'target.json'),
            '--output',
            str(output),
        ],
    )
    cli.main()
    assert output.read_bytes().hex() == (FIXTURES / 'show.golden').read_text().strip()
    report = capsys.readouterr().out
    assert '2 instruments, 2 categories, 4 patches' in report
    assert 'a: channel 1 to din' in report and 'b: channel 2 to trs' in report


def test_cli_does_not_write_binary_when_patches_have_errors(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    document = tmp_path / 'invalid.toml'
    document.write_text(
        '[instruments.a]\ninstrument="synth"\nsetup="stock"\nchannel=1\noutput="din"\n'
        '[[patches.wind]]\npatch=":a:0"\n[[patches.wind]]\npatch=":a:129"\n'
    )
    output = tmp_path / 'show.bin'
    monkeypatch.setattr(
        sys,
        'argv',
        [
            'cntrl',
            'compile',
            str(document),
            '--definitions',
            str(FIXTURES / 'definitions.json'),
            '--target',
            str(FIXTURES / 'target.json'),
            '--output',
            str(output),
        ],
    )
    with pytest.raises(SystemExit) as failure:
        cli.main()
    assert 'wind[1]' in str(failure.value) and 'wind[2]' in str(failure.value)
    assert not output.exists()


def test_cli_preserves_repeated_table_inputs(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    root = FIXTURES.parents[1] / 'examples/patch-switcher'
    output = tmp_path / 'show.bin'
    monkeypatch.setattr(
        sys,
        'argv',
        [
            'cntrl',
            'compile',
            str(root / 'patches.toml'),
            '--definitions',
            str(root / 'definitions.toml'),
            '--table',
            str(root / 'table.toml'),
            '--table',
            str(root / 'tags.toml'),
            '--target',
            str(FIXTURES / 'target.json'),
            '--output',
            str(output),
        ],
    )
    cli.main()
    assert compiler.HEADER.unpack_from(output.read_bytes())[4:7] == (1, 2, 3)


FIXTURES = Path(__file__).resolve().parents[1] / 'firmware' / 'test'
