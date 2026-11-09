"""Regenerate the shared Python/C++ setup fixtures from the repository root.

Run with uv run scripts/regenerate_fixtures.py.
"""

import sys
from pathlib import Path

import tyro
from pydantic import BaseModel

from cntrl.patch_switcher import compiler, models


class RegenerateFixtures(BaseModel, frozen=True):
    """Rebuild show.golden, display.golden and actions.golden."""


def compile_switches(assignments: dict[str, object], capacity: int = 5) -> bytes:
    show = compiler.read_document(ROOT / 'show.json')
    assert isinstance(show, dict)
    patch_cycles = show['patch_cycles']
    assert isinstance(patch_cycles, dict)
    show['patches'] = {p['name']: p for v in patch_cycles.values() for p in v}
    show['switches'] = assignments
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'target.json')
    ).model_copy(update={'max_switches': capacity})
    return compiler.compile_show(
        show, compiler.read_document(ROOT / 'definitions.json'), [], target
    )


def assignment(mode: str = 'momentary') -> dict[str, object]:
    return {
        'button': 'A',
        'mode': mode,
        'on': {
            'type': 'control-change',
            'output': 'din',
            'channel': 1,
            'controller': 64,
            'value': 127,
        },
        'off': {
            'type': 'control-change',
            'output': 'din',
            'channel': 1,
            'controller': 64,
            'value': 0,
        },
    }


def fixture_assignments() -> dict[str, object]:
    solo = assignment('latched')
    solo['button'] = 'B'
    solo['on'] = {
        'type': 'control-change',
        'output': 'din',
        'channel': 1,
        'controller': 7,
        'value': 110,
    }
    solo['off'] = {
        'type': 'control-change',
        'output': 'din',
        'channel': 1,
        'controller': 7,
        'value': 85,
    }
    return {
        'sustain': assignment(),
        'solo': solo,
        'alternate': {
            'button': 'C',
            'mode': 'momentary',
            'on': {'patch': 'Strings'},
            'off': {'patch': 'Pad'},
        },
    }


def regenerate_fixtures() -> None:
    target = models.Target.model_validate(compiler.read_document(ROOT / 'target.json'))
    definitions = compiler.read_document(ROOT / 'definitions.json')
    fixtures = {
        n: compiler.compile_show(
            compiler.read_document(ROOT / f'{n}.{s}'), definitions, [], target
        )
        for n, s in [
            ('show', 'json'),
            ('display', 'toml'),
            ('pages', 'toml'),
            ('counters', 'toml'),
        ]
    }
    fixtures['actions'] = compile_switches(fixture_assignments())
    for name, data in fixtures.items():
        path = ROOT / f'{name}.golden'
        path.write_text(data.hex() + '\n')
        print(f'{path}: {len(data)} bytes')


def main() -> None:
    tyro.cli(RegenerateFixtures)
    try:
        regenerate_fixtures()
    except (OSError, ValueError) as error:
        sys.exit(str(error))


ROOT = Path(__file__).resolve().parents[1] / 'firmware/test'


if __name__ == '__main__':
    main()
