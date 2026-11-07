"""Implied message types use the same validated patches and wire encoding."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved, songs


@pytest.mark.parametrize(
    ('kind', 'fields'),
    [
        ('program-change', {'program': 7}),
        ('control-change', {'controller': 7, 'value': 100}),
        ('note-on', {'note': 64, 'velocity': 64}),
        ('pitch-bend', {'value': -300}),
        ('nrpn', {'parameter': 130, 'value': 300}),
        ('polyphonic-key-pressure', {'note': 64, 'pressure': 80}),
        ('channel-pressure', {'pressure': 80}),
        ('time-code-quarter-frame', {'piece': 3, 'value': 4}),
        ('song-position-pointer', {'position': 500}),
        ('song-select', {'song': 3}),
        ('sysex', {'data': [125, 1]}),
    ],
)
def test_implied_type_compiles_and_round_trips_like_explicit_type(
    kind: str, fields: dict[str, object]
) -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    route: dict[str, object] = {'output': 'din'}
    if kind not in (
        'time-code-quarter-frame',
        'song-position-pointer',
        'song-select',
        'sysex',
    ):
        route['channel'] = 2
    implied = fields | route | {'name': 'Example'}
    explicit = implied | {'type': kind}
    assert compiler.normalize_patch(implied) == compiler.normalize_patch(explicit)

    def setup(patch: dict[str, object]) -> dict[str, object]:
        return {
            'patches': {'named': patch},
            'patch_cycles': {'Live': [patch]},
            'switches': {'play': {'button': 'A', 'on': {'patches': [patch]}}},
        }

    data = compiler.compile_show(setup(implied), {}, [], target)
    assert data == compiler.compile_show(setup(explicit), {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert compiler.compile_show(exported, {}, [], target) == data
    library = songs.Library(songs=[songs.Song(name='Example', patches=[implied])])
    encoded = songs.encode(library, target)
    assert songs.encode(songs.decode(encoded), target) == encoded

    if kind == 'program-change':
        return  # Instrument Program Change has separate catalogue semantics.
    instruments = {'piano': {'output': 'din', 'channel': 2}}
    inferred_data = compiler.compile_show(
        setup(fields) | {'instruments': instruments}, {}, [], target
    )
    assert inferred_data == compiler.compile_show(
        setup(fields | route) | {'instruments': instruments}, {}, [], target
    )
    exported = tomlkit.parse(resolved.export(inferred_data)).unwrap()
    assert compiler.compile_show(exported, {}, [], target) == inferred_data
    inferred_library = songs.Library(
        instruments=instruments,
        songs=[songs.Song(name='Example', patches=[fields])],
    )
    explicit_library = inferred_library.model_copy(
        update={'songs': [songs.Song(name='Example', patches=[fields | route])]}
    )
    assert songs.encode(inferred_library, target) == songs.encode(
        explicit_library, target
    )


def test_instrument_program_and_derived_overrides_keep_their_meaning() -> None:
    assert isinstance(
        compiler.normalize_patch({'instrument': 'piano', 'program': 7}),
        models.ProgramChange,
    )
    assert isinstance(
        compiler.normalize_patch({'parent': 'A', 'program': 8, 'value': 1}),
        models.Derived,
    )
    assert (
        compiler.normalize_patch(
            {
                'type': 'note-off',
                'output': 'din',
                'channel': 1,
                'note': 64,
                'velocity': 64,
            }
        ).type
        == 'note-off'
    )


@pytest.mark.parametrize(
    'fields',
    [
        {'controller': 7, 'value': 128},
        {'note': 64, 'velocity': -1},
        {'controller': 7, 'note': 64, 'velocity': 64, 'value': 10},
        {'program': 7, 'data': [1]},
        {},
    ],
)
def test_invalid_or_ambiguous_implied_message_is_rejected(
    fields: dict[str, object],
) -> None:
    with pytest.raises(ValueError):
        compiler.normalize_patch(fields | {'output': 'din', 'channel': 1})


def test_sole_instrument_is_inferred_everywhere_programs_are_used() -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    instruments = {'piano': {'output': 'din', 'channel': 2}}

    def setup(patch: dict[str, object]) -> dict[str, object]:
        return {
            'instruments': instruments,
            'patches': {'A': patch, 'quiet': {'parent': 'A', 'volume': 60}},
            'patch_cycles': {'Live': [patch, {'patch': 'quiet'}]},
            'switches': {'play': {'button': 'A', 'on': {'patches': [patch]}}},
        }

    implied = {'program': 7, 'volume': 100}
    explicit = implied | {'instrument': 'piano'}
    data = compiler.compile_show(setup(implied), {}, [], target)
    assert data == compiler.compile_show(setup(explicit), {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert compiler.compile_show(exported, {}, [], target) == data

    def library(patch: dict[str, object]) -> songs.Library:
        return songs.Library(
            instruments=instruments,
            songs=[songs.Song(name='Example', patches=[patch])],
        )

    encoded = songs.encode(library(implied), target)
    assert encoded == songs.encode(library(explicit), target)
    assert songs.encode(songs.decode(encoded), target) == encoded


@pytest.mark.parametrize('count', [0, 2])
def test_program_requires_an_instrument_when_no_unique_choice_exists(
    count: int,
) -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    setup = {
        'instruments': {
            str(i): {'output': 'din', 'channel': i + 1} for i in range(count)
        },
        'patches': {'A': {'program': 7}},
    }
    with pytest.raises(compiler.CompilationError, match='exactly one instrument'):
        compiler.compile_show(setup, {}, [], target)


def test_explicit_program_routing_takes_precedence_over_the_sole_instrument() -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    setup = {'instruments': {'piano': {'output': 'din', 'channel': 2}}}
    explicit = {'program': 7, 'output': 'din', 'channel': 3}
    data = compiler.compile_show(setup | {'patches': {'A': explicit}}, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['patches']['A']['channel'] == 3
    with pytest.raises(
        compiler.CompilationError, match='unknown or invalid instrument'
    ):
        compiler.compile_show(
            setup | {'patches': {'A': {'program': 7, 'instrument': 'missing'}}},
            {},
            [],
            target,
        )


@pytest.mark.parametrize('patch', [{'program': 7}, {'controller': 7, 'value': 100}])
def test_companion_patch_uses_its_own_sole_instrument(patch: dict[str, object]) -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    local = {
        'instruments': {'local': {'output': 'din', 'channel': 1}},
        'switches': {
            'play': {'button': 'A', 'on': {'document': 'other', 'patch': 'A'}}
        },
    }
    other = {
        'instruments': {'remote': {'output': 'din', 'channel': 2}},
        'patches': {'A': patch},
    }
    data = compiler.compile_show(local, {}, [], target, documents={'other': other})
    route = (
        {'instrument': 'remote'}
        if 'program' in patch
        else {'output': 'din', 'channel': 2}
    )
    explicit = other | {'patches': {'A': patch | route}}
    assert data == compiler.compile_show(
        local, {}, [], target, documents={'other': explicit}
    )


@pytest.mark.parametrize('route', [{}, {'output': 'din'}, {'channel': 3}])
def test_midi_inference_preserves_explicit_routing_fields(
    route: dict[str, object],
) -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    patch = {'controller': 7, 'value': 100} | route
    setup = {
        'instruments': {'piano': {'output': 'trs', 'channel': 2}},
        'patches': {'A': patch, 'variation': {'parent': 'A', 'value': 80}},
    }
    explicit = {'output': 'trs', 'channel': 2} | patch
    assert compiler.compile_show(setup, {}, [], target) == compiler.compile_show(
        setup | {'patches': {'A': explicit, 'variation': {'parent': 'A', 'value': 80}}},
        {},
        [],
        target,
    )


@pytest.mark.parametrize('count', [0, 2])
@pytest.mark.parametrize('patch', [{'controller': 7, 'value': 100}, {'type': 'start'}])
def test_midi_routing_requires_a_unique_instrument(
    count: int, patch: dict[str, object]
) -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    setup = {
        'instruments': {
            str(i): {'output': 'din', 'channel': i + 1} for i in range(count)
        },
        'patches': {'A': patch},
    }
    with pytest.raises(compiler.CompilationError, match='exactly one instrument'):
        compiler.compile_show(setup, {}, [], target)


@pytest.mark.parametrize(
    'kind',
    [
        'tune-request',
        'timing-clock',
        'start',
        'continue',
        'stop',
        'active-sensing',
        'system-reset',
        'note-off',
    ],
)
def test_explicit_midi_types_also_inherit_routing(kind: str) -> None:
    target = models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    )
    patch: dict[str, object] = {'type': kind}
    route: dict[str, object] = {'output': 'din'}
    if kind == 'note-off':
        patch.update(note=64, velocity=64)
        route['channel'] = 2
    setup = {'instruments': {'piano': {'output': 'din', 'channel': 2}}}
    assert compiler.compile_show(
        setup | {'patches': {'A': patch}}, {}, [], target
    ) == compiler.compile_show(
        setup | {'patches': {'A': patch | route}}, {}, [], target
    )
