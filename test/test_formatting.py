"""Seeded, optional cycle styles and their compiled presentation."""

from collections import Counter
from pathlib import Path
from random import getstate

import pytest
import tomlkit
from pydantic import ValidationError
from test_display_format import lines

from cntrl.patch_switcher import (
    compiler,
    display,
    formatting,
    graphics,
    models,
    resolved,
)


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(FIXTURES / 'target.json')
    )


def test_custom_foreground_brightness_limit(target: models.Target) -> None:
    styles = formatting.cycle_styles(
        models.Show(foreground_brightness_limit=0.3), ['a', 'b', 'c'], target
    )
    for s in styles.values():
        assert s.foreground_color is not None
        red, green, blue = graphics.color(s.foreground_color)
        assert 299 * red + 587 * green + 114 * blue < 0.3 * 255000
    with pytest.raises(ValueError, match='too few'):
        formatting.cycle_styles(
            models.Show(foreground_brightness_limit=0.0001), ['a', 'b'], target
        )


def test_seeded_cycles_balance_fonts_and_keep_unique_colors(
    target: models.Target,
) -> None:
    names = [str(i) for i in range(12)]
    state = getstate()
    show = models.Show()
    styles = formatting.cycle_styles(show, names, target)
    assert formatting.cycle_styles(show, names, target) == styles
    assert getstate() == state
    counts = Counter(s.font_name for s in styles.values())
    assert max(counts.values()) - min(counts.values()) <= 1
    if len(target.fonts) > 1:
        assert all(
            a.font_name != b.font_name
            for a, b in zip(
                list(styles.values()), list(styles.values())[1:], strict=False
            )
        )
    assert len({s.foreground_color for s in styles.values()}) == len(names)
    assert all(
        s.background_color == show.background_color == '0xFFF8D8'
        for s in styles.values()
    )
    assert styles != formatting.cycle_styles(models.Show(seed=1), names, target)


def test_background_anchor_excludes_its_color(
    target: models.Target,
) -> None:
    styles = formatting.cycle_styles(
        models.Show(background_color='black'), ['a', 'b', 'c'], target
    )
    assert all(s.background_color == 'black' for s in styles.values())
    assert all(s.foreground_color != '0x000000' for s in styles.values())


@pytest.mark.parametrize('seed', [0, 1, 42])
def test_generated_foregrounds_stay_below_half_brightness(
    seed: int,
    target: models.Target,
) -> None:
    styles = formatting.cycle_styles(
        models.Show(seed=seed), [str(i) for i in range(64)], target
    )
    for style in styles.values():
        assert style.foreground_color is not None
        red, green, blue = graphics.color(style.foreground_color)
        assert (0.299 * red + 0.587 * green + 0.114 * blue) / 255 < 0.5


def test_disabled_filling_does_not_generate_styles(target: models.Target) -> None:
    assert (
        formatting.cycle_styles(models.Show(fill_in_styles=False), ['a'], target) == {}
    )
    assert formatting.cycle_styles(models.Show(), [], target) == {}


@pytest.mark.parametrize(
    'values',
    [
        {'background_color': []},
        {'seed': True},
        {'fill_in_styles': 1},
        *[
            {'style': {'horizontal_scale': v}}
            for v in [0, -1, float('nan'), float('inf'), True]
        ],
    ],
)
def test_formatting_settings_are_validated(values: dict[str, object]) -> None:
    with pytest.raises(ValidationError):
        models.Show.model_validate(values)


def test_invalid_background_is_reported(target: models.Target) -> None:
    with pytest.raises(ValueError, match='unknown color'):
        formatting.cycle_styles(models.Show(background_color='wrong'), ['a'], target)


@pytest.mark.parametrize('seed', [0, None])
def test_generated_formatting_survives_export(
    seed: int | None, target: models.Target
) -> None:
    show = compiler.read_document(FIXTURES / 'show.json')
    assert isinstance(show, dict)
    show.update(fill_in_styles=True, seed=seed)
    data = compiler.compile_show(
        show, compiler.read_document(FIXTURES / 'definitions.json'), [], target
    )
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert isinstance(exported['seed'], int)
    if seed is not None:
        assert exported['seed'] == seed
    assert compiler.compile_show(exported, {}, [], target) == data


def test_cycle_formatting_is_shared_without_overriding_authored_properties(
    target: models.Target,
) -> None:
    show = models.Show(style=models.Style(foreground_color='red'))
    patches = [
        display.DisplayPatch(values={'patch_cycle.name': 'wind', 'display_name': n})
        for n in ['Flute', 'Oboe']
    ]
    patches[1] = patches[1].model_copy(
        update={'program_style': models.Style(font_name='classic')}
    )
    _, references, block = display.compile_display(show, patches, target)
    # Equal cycle headings share a page style, while program overrides remain local.
    first = lines(block, references[0][0])
    second = lines(block, references[1][0])
    assert first[0][1] == second[0][1]
    assert all(r[1][4] == graphics.rgb565((255, 0, 0)) for r in first + second)
    assert {r[1][5] for r in first + second} == {first[0][1][5]}
    assert second[1][1][0] == next(f.id for f in target.fonts if f.name == 'classic')


FIXTURES = Path(__file__).resolve().parents[1] / 'firmware/test'
