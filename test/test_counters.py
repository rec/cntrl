"""Named PC/CC counters compile, export and reject invalid assignments."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(compiler.read_document(ROOT / 'target.json'))


def test_counter_setup_round_trips_and_matches_native_fixture(
    target: models.Target,
) -> None:
    setup = compiler.read_document(ROOT / 'counters.toml')
    data = compiler.compile_show(setup, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert (
        models.Setup.model_validate(exported).counters
        == models.Setup.model_validate(setup).counters
    )
    assert compiler.compile_show(exported, {}, [], target) == data
    assert data.hex() == (ROOT / 'counters.golden').read_text().strip()


@pytest.mark.parametrize(
    'changes',
    [
        {'initial': -1},
        {'minimum': 96},
        {'maximum': 128},
        {'step': 0},
        {'step': True},
        {'step': 32768},
        {'channel': 17},
        {'controller': None},
        {'type': 'program-change'},
        {'boundary': 'clamp'},
    ],
)
def test_invalid_counter_definition_is_rejected(changes: dict[str, object]) -> None:
    counter = models.Setup.model_validate(
        compiler.read_document(ROOT / 'counters.toml')
    ).counters['volume']
    with pytest.raises(ValueError):
        models.Counter.model_validate(counter.model_dump() | changes)


@pytest.mark.parametrize('name', ['', 'x' * 64, 'bad\nname'])
def test_invalid_counter_name_is_rejected(target: models.Target, name: str) -> None:
    counter = models.Setup.model_validate(
        compiler.read_document(ROOT / 'counters.toml')
    ).counters['volume']
    with pytest.raises(compiler.CompilationError, match='counter names'):
        compiler.compile_show({'counters': {name: counter}}, {}, [], target)


def test_counter_output_respects_enabled_destinations(target: models.Target) -> None:
    counter = models.Setup.model_validate(
        compiler.read_document(ROOT / 'counters.toml')
    ).counters['volume']
    with pytest.raises(compiler.CompilationError, match='unavailable MIDI output'):
        compiler.compile_show(
            {'counters': {'volume': counter.model_copy(update={'output': 'missing'})}},
            {},
            [],
            target,
        )


def test_unknown_counter_action_and_display_field_are_rejected(
    target: models.Target,
) -> None:
    for setup in (
        {'patches': {'bad': {'operation': 'scroll-counter', 'counter': 'missing'}}},
        {'patch_views': {'patch': {'fields': ['counters.missing']}}},
    ):
        with pytest.raises(compiler.CompilationError, match='unknown'):
            compiler.compile_show(setup, {}, [], target)


def test_inlined_counter_actions_keep_shared_values_and_separate_local_counters(
    target: models.Target,
) -> None:
    source = models.Setup.model_validate(compiler.read_document(ROOT / 'counters.toml'))
    local = source.model_copy(
        update={
            'patches': {
                'up': {'patch': 'up', 'document': 'other'},
                'down': {'patch': 'down', 'document': 'other'},
            },
            'switches': {},
            'patch_cycles': {'counter': ['up', 'down']},
        }
    )
    data = compiler.compile_show(
        local, {}, [], target, documents={'other': source.model_dump()}
    )
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    restored = models.Setup.model_validate(exported)
    assert len(restored.counters) == len(source.counters) + 1
    up = compiler.normalize_patch(restored.patches['up'])
    down = compiler.normalize_patch(restored.patches['down'])
    assert isinstance(up, models.Sequence) and isinstance(down, models.Sequence)
    assert isinstance(up.patches[0], models.ScrollCounter)
    assert isinstance(down.patches[0], models.ScrollCounter)
    assert up.patches[0].counter == down.patches[0].counter != 'volume'
    rebuilt = compiler.compile_show(exported, {}, [], target)
    assert (
        models.Setup.model_validate(
            tomlkit.parse(resolved.export(rebuilt)).unwrap()
        ).counters
        == restored.counters
    )


ROOT = Path(__file__).parents[1] / 'firmware/test'
