"""Global loop-stop policy survives compilation and editable export."""

from pathlib import Path

import pytest
import tomlkit
from pydantic import ValidationError

from cntrl.patch_chooser import compiler, compiler_inputs, models, resolved


def test_loop_reset_policy_round_trips_all_neutral_parameter_types() -> None:
    target = models.Target.model_validate(
        compiler_inputs.read_document(
            Path(__file__).parents[1] / 'firmware/test/target.json'
        )
    )
    source = {
        'loop_stop_reset_controls': True,
        'loop_stop_neutral_values': [
            dict(
                type='control-change', output='din', channel=1, controller=10, value=64
            ),
            dict(
                type='control-change',
                output='din',
                channel=2,
                controller=1,
                value=8192,
                resolution=14,
            ),
            dict(
                type='nrpn',
                output='din',
                channel=3,
                parameter=42,
                value=90,
                resolution=7,
            ),
            dict(type='nrpn', output='din', channel=4, parameter=43, value=8192),
            dict(type='rpn', output='din', channel=5, parameter=1, value=8192),
        ],
    }
    compiled = compiler.compile_show(source, {}, [], target)
    exported = tomlkit.parse(resolved.export(compiled)).unwrap()
    settings = models.Setup.model_validate(exported)
    expected = models.Setup.model_validate(source)
    assert settings.loop_stop_reset_controls
    assert settings.loop_stop_neutral_values == expected.loop_stop_neutral_values
    assert compiler.compile_show(exported, {}, [], target) == compiled


@pytest.mark.parametrize(
    'values',
    [
        [dict(type='control-change', channel=1, controller=10, value=64)],
        [dict(type='control-change', output='din', controller=10, value=64)],
        [dict(type='control-change', output='din', channel=1, controller=64, value=0)],
        [dict(type='control-change', output='din', channel=1, controller=0, value=0)],
        [
            dict(
                type='nrpn',
                output='din',
                channel=1,
                parameter=42,
                value=128,
                resolution=7,
            )
        ],
        [dict(type='control-change', output='din', channel=1, controller=10, value=64)]
        * 2,
        [
            dict(
                type='control-change',
                output='din',
                channel=1,
                controller=1,
                value=8192,
                resolution=14,
            ),
            dict(
                type='control-change', output='din', channel=1, controller=33, value=64
            ),
        ],
    ],
)
def test_loop_neutral_values_require_unambiguous_continuous_addresses(
    values: list[dict[str, object]],
) -> None:
    with pytest.raises(ValidationError):
        models.Setup(loop_stop_neutral_values=values)
