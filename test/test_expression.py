"""Expression assignments in editable and compiled setups."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved


@pytest.fixture
def target() -> models.Target:
    data = compiler.read_document(Path('firmware/test/target.json'))
    return models.Target.model_validate(data).model_copy(
        update={'trs_ports': {'configurable-2': ['expression']}}
    )


def setup(**fields: object) -> dict[str, object]:
    return {
        'trs_ports': {'configurable-2': 'expression'},
        'utility_pages': {
            'expression': {
                'configurable-2': {
                    'output': 'din',
                    'channel': 16,
                    'controller': 11,
                    'minimum': 20,
                    'maximum': 96,
                    'reverse': True,
                    'curve': 'slow',
                    **fields,
                }
            }
        },
    }


@pytest.mark.parametrize('clock_outputs', [[], ['din']])
def test_expression_assignment_survives_download(
    target: models.Target, clock_outputs: list[str]
) -> None:
    source = setup()
    pages = source['utility_pages']
    assert isinstance(pages, dict)
    pages['clock_outputs'] = clock_outputs
    compiled = compiler.compile_show(source, {}, [], target)
    exported = tomlkit.parse(resolved.export(compiled)).unwrap()
    assert (
        models.Setup.model_validate(exported).utility_pages.expression
        == models.Setup.model_validate(source).utility_pages.expression
    )
    assert (
        models.Setup.model_validate(exported).utility_pages.clock_outputs
        == clock_outputs
    )
    assert compiler.compile_show(exported, {}, [], target) == compiled


def test_expression_requires_the_configured_input_role(target: models.Target) -> None:
    source = setup()
    source['trs_ports'] = {}
    with pytest.raises(
        compiler.CompilationError, match='requires the expression TRS role'
    ):
        compiler.compile_show(source, {}, [], target)


@pytest.mark.parametrize(
    'fields',
    [
        {'channel': 0},
        {'controller': 128},
        {'minimum': 110},
        {'curve': 'unknown'},
        {'output': 'unavailable'},
    ],
)
def test_invalid_expression_assignment_is_rejected(
    target: models.Target, fields: dict[str, object]
) -> None:
    source = setup(**fields)
    with pytest.raises(compiler.CompilationError):
        compiler.compile_show(source, {}, [], target)
