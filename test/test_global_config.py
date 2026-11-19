"""Whole-rig defaults preserve presence and local source ownership."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import cli, compiler, data_files, global_config, resolved


def test_defaults_merge_named_fields_and_replace_lists_without_mutation() -> None:
    defaults = {
        'brightness': 160,
        'pickup': True,
        'instruments': {'synth': {'output': 'din', 'channel': 1}},
        'patches': {'sound': {'instrument': 'synth', 'program': 10}},
        'page_settings': {'clock_outputs': ['din']},
    }
    resolved = global_config.overlay(
        defaults,
        {
            'brightness': 192,
            'pickup': False,
            'instruments': {'synth': {'channel': 2}},
            'patches': {'sound': {'program': 12}},
            'page_settings': {'clock_outputs': []},
        },
    )
    assert resolved == {
        'brightness': 192,
        'pickup': False,
        'instruments': {'synth': {'output': 'din', 'channel': 2}},
        'patches': {'sound': {'instrument': 'synth', 'program': 12}},
        'page_settings': {'clock_outputs': []},
    }
    assert global_config.overlay(defaults, {}) == defaults
    assert defaults['instruments'] == {'synth': {'output': 'din', 'channel': 1}}


def test_binding_overrides_replace_whole_assignments_by_control() -> None:
    inherited = {
        'bindings': {
            'A': {'on': 'hold', 'mode': 'momentary', 'off': 'release'},
            'B': 'next',
        }
    }
    assert global_config.overlay(inherited, {'bindings': {'A': 'boost'}}) == {
        'bindings': {'A': 'boost', 'B': 'next'}
    }
    assert inherited['bindings']['A']['mode'] == 'momentary'


@pytest.mark.parametrize('value', [False, 0, '', [], 'None', None])
def test_explicit_values_are_not_absence(value: object) -> None:
    assert global_config.overlay({'value': 'inherited'}, {'value': value}) == {
        'value': value
    }


def test_removal_supports_literal_keys_and_complete_definition_replacement() -> None:
    assert global_config.overlay(
        {
            'patches': {
                'a.b': {'type': 'program-change', 'program': 10},
                'unused': {'program': 2},
            },
            'brightness': 160,
        },
        {
            'inheritance': {
                'remove': [['patches', 'a.b'], ['patches', 'unused'], ['brightness']]
            },
            'patches': {'a.b': {'type': 'control-change', 'controller': 7, 'value': 0}},
        },
    ) == {'patches': {'a.b': {'type': 'control-change', 'controller': 7, 'value': 0}}}


@pytest.mark.parametrize(
    'removals',
    [
        [['missing']],
        [['brightness', 'nested']],
        [['patches'], ['patches', 'sound']],
        [['patches', 'sound'], ['patches']],
        [['patches'], ['patches']],
        [[]],
        [['patches', 0]],
        'patches.sound',
    ],
)
def test_invalid_or_stale_removals_fail(removals: object) -> None:
    with pytest.raises(ValueError, match='inheritance.remove'):
        global_config.overlay(
            {'brightness': 160, 'patches': {'sound': {'program': 1}}},
            {'inheritance': {'remove': removals}},
        )


def test_setup_composition_precedes_global_overlay_and_preserves_origins(
    tmp_path: Path,
) -> None:
    defaults = global_config.user_path()
    defaults.parent.mkdir()
    defaults.write_text(
        'brightness = 160\n'
        '[instruments.synth]\noutput = "din"\nchannel = 1\n'
        '[page_settings]\nclock_outputs = ["din"]\n'
    )
    first, second = tmp_path / 'first.toml', tmp_path / 'second.toml'
    first.write_text('[page_settings]\nclock_outputs = ["usb-device"]\n')
    second.write_text(
        '[instruments.synth]\nchannel = 2\n'
        '[page_settings]\nclock_outputs = ["usb-host"]\n'
    )
    errors: list[str] = []
    trace = data_files.Trace()
    expression = data_files.DataFile(name='Live', paths=[first, second])
    result = global_config.load_setup(expression, True, errors, trace=trace)
    assert not errors
    assert result == {
        'brightness': 160,
        'instruments': {'synth': {'output': 'din', 'channel': 2}},
        'page_settings': {'clock_outputs': ['usb-device', 'usb-host']},
    }
    assert trace.defaults == {'Live': defaults.resolve()}
    assert trace.roots == {'Live': first.resolve()}
    assert trace.locations['Live']['instruments', 'synth', 'channel'].file == second
    assert trace.locations['Live']['instruments', 'synth', 'output'].file == defaults
    assert trace.inherited['Live']['instruments', 'synth', 'channel'].file == defaults
    assert trace.consumers[defaults] == ['Live']


def test_bound_absence_does_not_rediscover_defaults(tmp_path: Path) -> None:
    path = tmp_path / 'live.toml'
    path.write_text('brightness = 192\n')
    defaults = global_config.user_path()
    defaults.parent.mkdir()
    defaults.write_text('pickup = false\n')
    trace = data_files.Trace()
    errors: list[str] = []
    result = global_config.load_setup(
        data_files.DataFile(name='Live', paths=[path]),
        True,
        errors,
        defaults={'Live': None},
        trace=trace,
    )
    assert result == {'brightness': 192} and not errors
    assert trace.defaults == {'Live': None}


@pytest.mark.parametrize(
    ('initial', 'replacement'), [(160, 40), (160, None), (None, 160)]
)
def test_one_compilation_uses_one_captured_defaults_snapshot(
    tmp_path: Path, initial: int | None, replacement: int | None
) -> None:
    defaults = global_config.user_path()
    defaults.parent.mkdir()
    if initial is not None:
        defaults.write_text(f'brightness = {initial}\n')
    path = tmp_path / 'live.toml'
    path.write_text('')
    trace = data_files.Trace()
    errors: list[str] = []
    first = global_config.load_setup(
        data_files.DataFile(name='First', paths=[path]), True, errors, trace=trace
    )
    if replacement is None:
        defaults.unlink()
    else:
        defaults.write_text(f'brightness = {replacement}\n')
    second = global_config.load_setup(
        data_files.DataFile(name='Second', paths=[path]), True, errors, trace=trace
    )
    assert not errors and first == second == (
        {'brightness': initial} if initial is not None else {}
    )
    if initial is not None:
        assert trace.files[defaults] == f'brightness = {initial}\n'.encode()


def test_explicitly_bound_missing_defaults_is_an_error(tmp_path: Path) -> None:
    path = tmp_path / 'live.toml'
    path.write_text('brightness = 192\n')
    errors: list[str] = []
    missing = tmp_path / 'missing.toml'
    global_config.load_setup(
        data_files.DataFile(name='Live', paths=[path]),
        True,
        errors,
        defaults={'Live': missing},
    )
    assert len(errors) == 1 and str(missing) in errors[0]


def test_ordinary_setup_named_session_is_not_a_recovery_manifest(
    tmp_path: Path,
) -> None:
    path = tmp_path / 'session.toml'
    path.write_text('brightness = 160\n')
    errors: list[str] = []
    result = global_config.load_setup(
        data_files.DataFile(name='Session', paths=[path]), True, errors
    )
    assert result == {'brightness': 160} and not errors


def test_unused_catalogue_is_not_expanded_before_setup_removal(tmp_path: Path) -> None:
    defaults = global_config.user_path()
    defaults.parent.mkdir()
    defaults.write_text(
        '[instruments.synth]\noutput = "din"\nchannel = 1\n'
        'catalogues = ["missing.toml"]\n'
    )
    path = tmp_path / 'live.toml'
    path.write_text(
        '[inheritance]\nremove = [["instruments", "synth", "catalogues"]]\n'
        '[instruments.synth]\nprograms = ["Piano"]\n'
    )
    errors: list[str] = []
    result = global_config.load_setup(
        data_files.DataFile(name='Live', paths=[path]), True, errors
    )
    assert not errors
    assert result == {
        'instruments': {'synth': {'output': 'din', 'channel': 1, 'programs': ['Piano']}}
    }


def test_recorded_defaults_recipe_round_trips_toml(tmp_path: Path) -> None:
    sources = data_files.Sources(
        document=['live.toml'],
        target=['target.toml'],
        global_defaults={'Live': None, 'Other': tmp_path / 'global.toml'},
    )
    data = tomlkit.dumps(sources.model_dump(mode='json'))
    assert data_files.Sources.model_validate(tomlkit.parse(data).unwrap()) == sources


@pytest.mark.parametrize(
    'paths',
    [
        [Path('/rig=a/live.toml'), Path('/rig=b/part.toml')],
        [Path(r'C:\rig\=live.toml'), Path(r'C:\rig\=part.toml')],
    ],
)
def test_source_recipe_names_and_paths_with_equals_round_trip(
    paths: list[Path],
) -> None:
    expression = data_files.DataFile(name='A=B', paths=paths)
    assert data_files.expressions(global_config.expression_arguments(expression)) == [
        expression
    ]


@pytest.mark.parametrize(
    ('origin', 'catalogue_origin'),
    [
        (r'C:\rig', r'C:\catalogue.toml'),
        (r'\Users\tom\rig', r'\Users\tom\catalogue.toml'),
    ],
)
def test_recovered_foreign_references_use_archived_files(
    tmp_path: Path, origin: str, catalogue_origin: str
) -> None:
    root, catalogue, font = (
        tmp_path / 'global.toml',
        tmp_path / 'catalogue.toml',
        tmp_path / 'font.cfont',
    )
    root.write_text(
        '[fonts]\nmain = "fonts/font.cfont"\n'
        '[instruments.synth]\ncatalogues = ["../catalogue.toml"]\n'
    )
    catalogue.write_text('[instruments.synth]\nprograms = ["Piano"]\n')
    font.write_bytes(b'font')
    relocations = {
        Path(origin + r'\global.toml'): root,
        Path(catalogue_origin): catalogue,
        Path(origin + r'\FONTS\Font.cfont'): font,
    }
    document = compiler.read_document(root, relocations=relocations)
    assert document == {
        'fonts': {'main': str(font)},
        'instruments': {
            'synth': {'programs': [{'name': 'Piano', 'names': {'synth': 'Piano'}}]}
        },
    }


@pytest.mark.parametrize('remove', [True, False])
def test_recovered_unused_references_do_not_access_original_files(
    tmp_path: Path, remove: bool
) -> None:
    global_file, root = tmp_path / 'global.toml', tmp_path / 'live.toml'
    global_file.write_text(
        '[instruments.synth]\noutput = "din"\nchannel = 1\n'
        'catalogues = ["missing.toml"]\n'
    )
    root.write_text('')
    errors: list[str] = []
    result = global_config.resolve_setup(
        {'inheritance': {'remove': [['instruments', 'synth']]}} if remove else {},
        data_files.DataFile(name='Live', paths=[root]),
        errors,
        defaults={'Live': global_file},
        relocations={
            Path('/old/rig/global.toml'): global_file,
            Path('/old/rig/live.toml'): root,
        },
    )
    if remove:
        assert result == {'instruments': {}} and not errors
    else:
        assert len(errors) == 1 and '.cntrl-unavailable-source' in errors[0]


def test_unused_recovered_font_is_confined_without_reading_it(tmp_path: Path) -> None:
    global_file, root = tmp_path / 'global.toml', tmp_path / 'live.toml'
    global_file.write_text('[fonts]\nunused = "missing.ttf"\n')
    root.write_text('')
    errors: list[str] = []
    result = global_config.resolve_setup(
        {},
        data_files.DataFile(name='Live', paths=[root]),
        errors,
        defaults={'Live': global_file},
        relocations={
            Path('/old/rig/global.toml'): global_file,
            Path('/old/rig/live.toml'): root,
        },
    )
    assert not errors
    reference = Path(result['fonts']['unused'])
    assert reference.is_relative_to(tmp_path / '.cntrl-unavailable-source')
    assert not reference.exists()


def test_compilation_inherits_a_rig_and_keeps_explicit_builtin_values(
    tmp_path: Path,
) -> None:
    defaults = global_config.user_path()
    defaults.parent.mkdir()
    defaults.write_text(
        'brightness = 160\npickup = true\n'
        '[instruments.synth]\noutput = "din"\nchannel = 1\n'
        '[patches.sound]\ninstrument = "synth"\nprogram = 10\n'
        '[patch_cycles]\nLive = [{patch = "sound"}]\n'
    )
    path = tmp_path / 'live.toml'
    path.write_text(
        'brightness = 192\npickup = false\n'
        '[instruments.synth]\nchannel = 2\n'
        '[patches.sound]\nprogram = 12\n'
    )
    bundle = cli.compile_inputs(
        data_files.Sources(
            document=[f'Live={path}'], target=['firmware/test/target.json']
        )
    )
    effective = tomlkit.parse(resolved.export(bundle.setups['Live'])).unwrap()
    assert effective['brightness'] == 192 and effective['pickup'] is False
    assert effective['instruments']['synth']['output'] == 'din'
    assert effective['instruments']['synth']['channel'] == 2
    assert effective['patches']['sound']['program'] == 12
