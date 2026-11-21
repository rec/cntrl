"""Live defaults retain explicit authoring presence and portable patch actions."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_chooser import (
    binary_format,
    compiler,
    compiler_errors,
    compiler_inputs,
    data_files,
    device_settings,
    global_config,
    models,
    resolved,
)


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler_inputs.read_document(ROOT / 'firmware/test/target.json')
    )


def test_local_presence_is_distinct_from_inherited_and_builtin_values(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, target: models.Target
) -> None:
    defaults = tmp_path / 'global.toml'
    defaults.write_text(
        'brightness=160\npickup=false\n[navigation]\nhold_delay=0.3\nrepeat_interval=0.4\n'
    )
    monkeypatch.setattr(global_config, 'user_path', lambda: defaults)
    source = tmp_path / 'Live.toml'
    source.write_text(
        'brightness=192\nframe_buffers=0\nuse_animations=false\n[navigation]\nhold_delay=0.2\n'
    )
    trace = data_files.Trace()
    errors: list[str] = []
    raw = global_config.load_setup(
        data_files.DataFile(name='Live', paths=[source]), True, errors, trace=trace
    )
    assert not errors
    data = compiler.compile_show(
        raw, {}, [], target, setting_overrides=trace.setting_overrides['Live']
    )
    assert device_settings.override_mask(data) == (1 << 0) | (1 << 7) | (1 << 8) | (
        1 << 17
    )
    assert tomlkit.parse(resolved.export(data))['brightness'] == 192
    assert not binary_format.has_opaque(data)


def test_explicit_removals_protect_builtin_values_from_device_defaults(
    target: models.Target,
) -> None:
    local = {'inheritance': {'remove': [['brightness'], ['navigation']]}}
    assert device_settings.explicit_mask(local, target) == (1 << 0) | (1 << 17) | (
        1 << 18
    )


@pytest.mark.parametrize('value', [None, 'none', 'None', False, True])
def test_pickup_none_inherits_live_defaults(
    value: object, target: models.Target
) -> None:
    mask = device_settings.explicit_mask({'pickup': value}, target)
    assert bool(mask & (1 << 10)) == isinstance(value, bool)


def test_overrides_map_target_port_names_and_individual_nested_fields(
    target: models.Target,
) -> None:
    local = {
        'trs_ports': {'configurable-2': 'off'},
        'auxiliary_switches': {'configurable-4': 'independent'},
        'target_pulse': {'minimum': 64},
    }
    assert device_settings.explicit_mask(local, target) == (1 << 20) | (1 << 26) | (
        1 << 30
    )
    assert device_settings.field_id('trs_ports.configurable-2', target) == 20
    assert device_settings.field_path(20, target) == ['trs_ports', 'configurable-2']


@pytest.mark.parametrize(
    ('setting', 'value', 'expected'),
    [
        ('brightness', 140, 140),
        ('use_animations', False, False),
        ('scrolling.horizontal_duration', '50 ms', 0.05),
        ('scrolling.horizontal_duration', '0.075', 0.075),
        ('screensaver_timeout', '5 seconds', 5),
        ('trs_ports.1', 'expression', 'expression'),
        ('brightness', None, None),
    ],
)
def test_global_setting_actions_export_and_recompile(
    setting: str, value: object, expected: object, target: models.Target
) -> None:
    action = {'operation': 'global-setting', 'setting': setting}
    if value is not None:
        action['value'] = value
    data = compiler.compile_show({'patch_cycles': {'Live': [action]}}, {}, [], target)
    decoded = resolved.patch(data, 0)
    assert decoded['operation'] == 'global-setting' and decoded['setting'] == setting
    if expected is None:
        assert 'value' not in decoded
    else:
        assert decoded['value'] == expected
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert compiler.compile_show(exported, {}, [], target) == data


def test_legacy_documents_remain_explicit_and_override_metadata_is_checked(
    target: models.Target,
) -> None:
    bare = compiler.compile_show({}, {}, [], target)
    assert device_settings.override_mask(bare) == (1 << 64) - 1
    for payload in (
        bytes(7),
        (1 << 34).to_bytes(8, 'little'),
        (1 << 27).to_bytes(8, 'little'),
    ):
        with pytest.raises(ValueError, match='setting overrides'):
            binary_format.Document(
                kind=binary_format.Kind.setup,
                records=[
                    binary_format.Record(tag=1, required=True, data=bare),
                    binary_format.Record(tag=3, data=payload),
                ],
            ).pack()


@pytest.mark.parametrize(
    ('setting', 'value'),
    [
        ('brightness', True),
        ('frame_buffers', 3),
        ('pickup', 1),
        ('scrolling.horizontal_duration', '0 ms'),
        ('target_pulse.maximum', 0),
    ],
)
def test_global_setting_actions_reject_invalid_values(
    setting: str, value: object, target: models.Target
) -> None:
    with pytest.raises(compiler_errors.CompilationError):
        compiler.compile_show(
            {
                'patch_cycles': {
                    'Live': [
                        {
                            'operation': 'global-setting',
                            'setting': setting,
                            'value': value,
                        }
                    ]
                }
            },
            {},
            [],
            target,
        )


ROOT = Path(__file__).resolve().parents[1]
