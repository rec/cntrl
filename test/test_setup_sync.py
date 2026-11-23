"""Three-way semantics, source provenance and interrupted publication recovery."""

import sys
from collections.abc import Mapping
from contextlib import nullcontext
from pathlib import Path
from unittest.mock import Mock

import pytest
import tomlkit
import tyro

from cntrl.file_lock import writer_lock
from cntrl.patch_chooser import (
    backups,
    binary_format,
    cli,
    compiler_inputs,
    data_files,
    global_config,
    models,
    resolved,
    setup_sync,
    setup_sync_sources,
    setup_sync_store,
    transport,
)
from scripts import native_tests


@pytest.mark.parametrize('binding_map', [False, True])
def test_pedal_patch_reference_edit_syncs_to_the_selected_occurrence(
    tmp_path: Path, native_programs: native_tests.NativePrograms, binding_map: bool
) -> None:
    source = tmp_path / 'show.toml'
    source.write_text(
        '[instruments.synth]\noutput="din"\nchannel=1\n'
        '[patches.clean]\nprogram=7\nvolume=80\n'
        '[patch_cycles]\nLive=[{patch="clean",display_name="Solo"},"clean","clean"]\n'
        + ('[patches.clean.bindings]\nF={operation="back"}\n' if binding_map else '')
    )
    target = Path(__file__).parents[1] / 'firmware/test/target.json'
    inputs = data_files.Sources(document=[f'Show={source}'], target=[str(target)])
    trace = data_files.Trace()
    base = cli.compile_inputs(inputs, trace=trace).setups['Show']
    raw, saved = tmp_path / 'before.bin', tmp_path / 'after.bin'
    raw.write_bytes(base)
    native_tests.run_native(
        [
            str(native_programs.build('test/test_chooser.cpp')),
            '--patch-edit',
            str(raw),
            str(saved),
        ]
    )
    host = setup_sync.canonical(base)
    pedal = setup_sync.canonical(saved.read_bytes())
    local = pedal['patch_cycles']['Live'][0]
    assert local['display_name'] == 'Solo'
    assert local['volume'] == 85 and 'patch' not in local
    if binding_map:
        assert local['bindings'] == pedal['patches']['clean']['bindings']
    assert pedal['patches']['clean']['volume'] == 82
    other_local = pedal['patch_cycles']['Live'][1]
    assert other_local['program'] == 8 and other_local['pan'] == 0
    assert other_local['volume'] == 82 and 'patch' not in other_local
    assert pedal['patch_cycles']['Live'][2]['patch'] == 'clean'
    assert (
        setup_sync.canonical(
            resolved.refresh_memory(
                saved.read_bytes(),
                {},
                target=models.Target.model_validate(
                    compiler_inputs.read_document(target)
                ),
            )
        )
        == pedal
    )
    changes = setup_sync.merge_changes(
        'Show',
        *[setup_sync_store.Value(present=True, value=v) for v in (host, host, pedal)],
    )
    document = tomlkit.parse(source.read_text())
    for edit in changes:
        change = setup_sync_sources.destination(edit, trace, host, pedal)
        assert change.choice == 'pedal', change
        setup_sync_sources.apply_change(document, change)
    source.write_text(tomlkit.dumps(document))
    result = cli.compile_inputs(inputs).setups['Show']
    assert setup_sync.canonical(result) == pedal


@pytest.mark.parametrize('interrupted', [False, True])
def test_baseline_conversion_preserves_provenance_and_recovers_publication(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, interrupted: bool
) -> None:
    root = tmp_path / 'setup-sync'
    snapshot = root / 'snapshot'
    snapshot.mkdir(parents=True)
    data = bytes.fromhex(
        (Path(__file__).parents[1] / 'firmware/test/show.golden').read_text()
    )
    (snapshot / 'Show.bin').write_bytes(data)
    provenance = b'compiler_checksum="original"\nsources=[]\n'
    (snapshot / 'snapshot.toml').write_bytes(provenance)
    (snapshot / 'source-0.original').write_bytes(b'brightness=192\n')
    setup_sync_store.advance(root, snapshot, ['Show'])
    original = setup_sync_store.base_record(root)
    publish = setup_sync_store.write_record

    def write(path: Path, value: Mapping[str, object]) -> None:
        if interrupted and path == root / 'base.toml':
            raise OSError('interrupted baseline publication')
        publish(path, value)

    monkeypatch.setattr(setup_sync_store, 'write_record', write)
    if interrupted:
        with pytest.raises(OSError, match='interrupted baseline'):
            setup_sync_store.migrate_baselines(root)
        with pytest.raises(ValueError, match='interrupted apply'):
            setup_sync_store.check_pending(root)
        setup_sync_store.recover(root / 'pending.toml')
        assert setup_sync_store.base_record(root) == original
        assert setup_sync_store.baseline(root) == {'Show': data}
    else:
        setup_sync_store.migrate_baselines(root)
        assert setup_sync_store.baseline(root) == {'Show': binary_format.wrap(data)}
        entry = tomlkit.parse(setup_sync_store.base_record(root).decode())['setups'][
            'Show'
        ]
        assert (root / entry['file']).parent == snapshot
        assert setup_sync_store.base_record(root) != original
        assert not (root / 'pending.toml').exists()
    assert (snapshot / 'Show.bin').read_bytes() == data
    assert (snapshot / 'snapshot.toml').read_bytes() == provenance
    assert (snapshot / 'source-0.original').read_bytes() == b'brightness=192\n'


@pytest.mark.parametrize(
    ('base', 'host', 'pedal', 'choices'),
    [(1, 1, 2, ['pedal']), (1, 2, 1, []), (1, 2, 2, []), (1, 2, 3, ['unresolved'])],
)
def test_three_way_choices(
    base: int, host: int, pedal: int, choices: list[str]
) -> None:
    changes = setup_sync.merge_changes(
        'A',
        *[setup_sync_store.Value(present=True, value=v) for v in (base, host, pedal)],
    )
    assert [c.choice for c in changes] == choices


def test_anonymous_reorder_requires_review_but_named_patches_merge() -> None:
    base = {
        'patch_cycles': {'one': [1, 2]},
        'patches': {'A': {'volume': 80}, 'B': {'volume': 40}},
    }
    host = {
        'patch_cycles': {'one': [2, 1]},
        'patches': {'A': {'volume': 90}, 'B': {'volume': 40}},
    }
    pedal = {
        'patch_cycles': {'one': [1, 3]},
        'patches': {'A': {'volume': 80}, 'B': {'volume': 50}},
    }
    changes = setup_sync.merge_changes(
        'A',
        *[setup_sync_store.Value(present=True, value=v) for v in (base, host, pedal)],
    )
    assert [(c.path, c.choice) for c in changes] == [
        (['patch_cycles', 'one'], 'unresolved'),
        (['patches', 'B', 'volume'], 'pedal'),
    ]


def test_provenance_retains_nested_composed_list_indices_and_last_scalar(
    tmp_path: Path,
) -> None:
    first, second = tmp_path / 'first.toml', tmp_path / 'second.toml'
    first.write_text('brightness=10\n[patch_cycles]\na=[[1,2]]\n')
    second.write_text('brightness=20\n[patch_cycles]\na=[[3,4],[5,6]]\n')
    trace = data_files.Trace()
    errors: list[str] = []
    result = data_files.load(
        data_files.DataFile(name='Show', paths=[first, second]),
        True,
        errors,
        trace=trace,
    )
    assert not errors
    assert result['patch_cycles'] == {'a': [[1, 2], [3, 4], [5, 6]]}
    location = trace.locations['Show'][('patch_cycles', 'a', 2, 1)]
    assert location.file == second
    assert location.path == ['patch_cycles', 'a', 1, 1]
    assert trace.locations['Show'][('brightness',)].file == second
    assert trace.files[first] == first.read_bytes()
    staged = data_files.load(
        data_files.DataFile(name='Show', paths=[first]),
        True,
        errors,
        overrides={first: b'brightness=30\n'},
    )
    assert staged == {'brightness': 30}
    assert first.read_text().startswith('brightness=10')


def test_recovery_restores_sources_and_base_after_partial_publication(
    tmp_path: Path,
) -> None:
    root = tmp_path / 'setup-sync'
    folder = root / 'review-one'
    folder.mkdir(parents=True)
    source = tmp_path / 'setup.toml'
    source.write_bytes(b'brightness=20\n')
    (folder / 'before').write_bytes(b'brightness=10\n')
    (folder / 'after').write_bytes(source.read_bytes())
    (folder / 'base.before').write_bytes(b'old-base')
    (root / 'base.toml').write_bytes(b'new-base')
    setup_sync_store.write_record(
        root / 'pending.toml',
        {
            'review': str(folder / 'review.toml'),
            'sources': [{'file': str(source), 'before': 'before', 'after': 'after'}],
        },
    )
    with pytest.raises(ValueError, match='interrupted apply'):
        setup_sync_store.check_pending(root)
    with writer_lock(root, operation='setup sync operation'):
        with pytest.raises(ValueError, match='another setup sync operation'):
            setup_sync_store.recover(root / 'pending.toml')
        assert source.read_bytes() == b'brightness=20\n'
        assert (root / 'pending.toml').exists()
    setup_sync_store.recover(root / 'pending.toml')
    assert source.read_bytes() == b'brightness=10\n'
    assert (root / 'base.toml').read_bytes() == b'old-base'
    setup_sync_store.check_pending(root)


def test_targeted_toml_change_keeps_comments_and_relative_paths() -> None:
    original = '# setup\nbrightness = 10 # dim\n[fonts]\nsmall = "fonts/source.ttf"\n'
    document = tomlkit.parse(original)
    setup_sync_sources.assign(
        document, ['brightness'], setup_sync_store.Value(present=True, value=20)
    )
    assert tomlkit.dumps(document) == original.replace('10 # dim', '20 # dim')


def test_sync_cli_uses_one_preview_or_stored_review() -> None:
    preview = tyro.cli(
        setup_sync.Sync,
        args=[
            'Show=show.toml',
            '+',
            'shared.toml',
            '--target',
            'target.toml',
            '--port',
            '/unused',
        ],
    )
    assert preview.document == ['Show=show.toml', '+', 'shared.toml']
    apply = tyro.cli(
        setup_sync.Sync, args=['--apply', 'setup-sync/review-one/review.toml']
    )
    assert apply.apply == Path('setup-sync/review-one/review.toml')
    assert apply.document == [] and apply.port is None


@pytest.mark.parametrize(
    'option,content',
    [('--apply', None), ('--apply', '[invalid'), ('--recover', 'sources = 17')],
)
def test_sync_cli_reports_invalid_saved_records_concisely(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, option: str, content: str | None
) -> None:
    record = tmp_path / 'record.toml'
    if content is not None:
        record.write_text(content)
    monkeypatch.setattr(sys, 'argv', ['cntrl', 'setup', 'sync', option, str(record)])
    with pytest.raises(SystemExit) as caught:
        cli.main()
    assert str(record.resolve()) in str(caught.value)


@pytest.mark.parametrize('error_type', [ValueError, OSError])
def test_sync_apply_preserves_unexpected_compilation_failures(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, error_type: type[Exception]
) -> None:
    root = tmp_path / 'setup-sync'
    folder = root / 'review-one'
    folder.mkdir(parents=True)
    review = setup_sync_store.Review(
        root=root,
        recipe=data_files.Sources(document=[str(tmp_path / 'setup.toml')]),
        baseline=setup_sync_store.digest(b''),
        sources=[],
        host={},
        pedal={},
        changes=[],
    ).model_dump(mode='json', exclude_none=True)
    path = folder / 'review.toml'
    setup_sync_store.write_record(path, review)
    setup_sync_store.write_record(folder / 'proposal.toml', review)
    failure = error_type('unexpected compilation failure')

    def fail(*args: object, **kwargs: object) -> None:
        raise failure

    monkeypatch.setattr(setup_sync, 'compile_inputs', fail)
    monkeypatch.setattr(sys, 'argv', ['cntrl', 'setup', 'sync', '--apply', str(path)])
    with pytest.raises(error_type) as caught:
        cli.main()
    assert caught.value is failure
    assert not (root / 'pending.toml').exists()


def test_snapshot_retains_unsupported_bytes_without_claiming_completion(
    tmp_path: Path,
) -> None:
    source = tmp_path / 'setup.toml'
    source.write_text('brightness=100\n')
    inputs = data_files.Sources(document=[str(source)], target=['target.toml'])
    folder = setup_sync.snapshot(
        setup_sync.store(inputs),
        'review-',
        {'Old': b'unsupported binary'},
        data_files.Trace(files={source: source.read_bytes()}),
        inputs,
    )
    assert (folder / 'Old.bin').read_bytes() == b'unsupported binary'
    assert (folder / 'Old.decode-error.txt').read_text()
    with pytest.raises(
        setup_sync_store.SyncError, match='incomplete snapshot retained'
    ):
        setup_sync.capture_assets(
            Mock(spec=transport.SetupClient), {'Old': b'unsupported binary'}, folder
        )
    assert not (folder / 'complete.toml').exists()


@pytest.mark.parametrize('error_type', [ValueError, OSError])
def test_sync_preview_preserves_unexpected_failures(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, error_type: type[Exception]
) -> None:
    failure = error_type('unexpected preview failure')
    monkeypatch.setattr(transport, 'SerialConnection', Mock(return_value=nullcontext()))
    monkeypatch.setattr(setup_sync, 'preview', Mock(side_effect=failure))
    monkeypatch.setattr(
        sys,
        'argv',
        ['cntrl', 'setup', 'sync', str(tmp_path / 'show.toml'), '--port', '/unused'],
    )
    with pytest.raises(error_type) as caught:
        cli.main()
    assert caught.value is failure


@pytest.mark.parametrize('error_type', [ValueError, OSError])
def test_snapshot_capture_preserves_unexpected_host_failures(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, error_type: type[Exception]
) -> None:
    failure = error_type('unexpected asset capture failure')
    monkeypatch.setattr(backups, 'backup_assets', Mock(side_effect=failure))
    with pytest.raises(error_type) as caught:
        setup_sync.capture_assets(
            Mock(spec=transport.SetupClient), {'Show': b'saved'}, tmp_path
        )
    assert caught.value is failure
    assert not (tmp_path / 'complete.toml').exists()


@pytest.mark.parametrize('pedal', [[1, 3], []])
def test_inherited_page_cycle_edit_writes_a_complete_local_list(
    tmp_path: Path, pedal: list[int]
) -> None:
    global_file, local = tmp_path / 'global.toml', tmp_path / 'show.toml'
    global_file.write_text('[patch_cycles]\nmain=[1,2]\n')
    local.write_text('# local changes only\n')
    locations = {
        ('patch_cycles',): data_files.Location(file=global_file, path=['patch_cycles']),
        ('patch_cycles', 'main'): data_files.Location(
            file=global_file, path=['patch_cycles', 'main']
        ),
        ('patch_cycles', 'main', 1): data_files.Location(
            file=global_file, path=['patch_cycles', 'main', 1]
        ),
    }
    trace = data_files.Trace(
        files={global_file: global_file.read_bytes(), local: local.read_bytes()},
        locations={'Show': locations},
        inherited={'Show': locations},
        roots={'Show': local},
        consumers={global_file: ['Show', 'Other'], local: ['Show']},
    )
    host = {'patch_cycles': {'main': [1, 2]}}
    after = {'patch_cycles': {'main': pedal}}
    changes = setup_sync.merge_changes(
        'Show',
        *[setup_sync_store.Value(present=True, value=v) for v in (host, host, after)],
    )
    change = setup_sync_sources.destination(changes[0], trace, host, after)
    assert change.path == ['patch_cycles', 'main']
    assert change.choice == 'pedal' and change.affects == ['Show']
    document = tomlkit.parse(local.read_text())
    setup_sync_sources.apply_change(document, change)
    assert document.unwrap() == {'patch_cycles': {'main': pedal}}
    assert tomlkit.dumps(document).startswith('# local changes only')
    assert global_file.read_text() == '[patch_cycles]\nmain=[1,2]\n'


@pytest.mark.parametrize('local_override', [False, True])
def test_deleting_an_inherited_value_records_a_local_removal(
    tmp_path: Path, local_override: bool
) -> None:
    global_file, local = tmp_path / 'global.toml', tmp_path / 'show.toml'
    global_file.write_text('[patches.sound]\nprogram=10\nvolume=80\n')
    local.write_text('[patches.sound]\nvolume=90\n' if local_override else '')
    path = ['patches', 'sound', 'volume']
    inherited = data_files.Location(file=global_file, path=path)
    trace = data_files.Trace(
        files={global_file: global_file.read_bytes(), local: local.read_bytes()},
        locations={
            'Show': {
                tuple(path): data_files.Location(file=local, path=path)
                if local_override
                else inherited
            }
        },
        inherited={'Show': {tuple(path): inherited}},
        roots={'Show': local},
        consumers={global_file: ['Show', 'Other'], local: ['Show']},
    )
    host = {
        'patches': {'sound': {'program': 10, 'volume': 90 if local_override else 80}}
    }
    pedal = {'patches': {'sound': {'program': 10}}}
    change = setup_sync_sources.destination(
        setup_sync.merge_changes(
            'Show',
            *[
                setup_sync_store.Value(present=True, value=v)
                for v in (host, host, pedal)
            ],
        )[0],
        trace,
        host,
        pedal,
    )
    assert change.remove_inherited and change.destination.file == local
    document = tomlkit.parse(local.read_text())
    setup_sync_sources.apply_change(document, change)
    assert document['inheritance']['remove'] == [path]
    assert 'volume' not in document['patches']['sound']
    assert global_file.read_text().endswith('volume=80\n')


def test_sync_recipe_records_explicit_absence_of_defaults(tmp_path: Path) -> None:
    source = tmp_path / 'show.toml'
    inputs = data_files.Sources(document=[f'Show={source}'], target=['target.toml'])
    trace = data_files.Trace(defaults={'Show': None})
    recipe = setup_sync.recipe(inputs, trace)
    encoded = tomlkit.dumps(recipe.model_dump(mode='json', exclude_none=True))
    restored = data_files.Sources.model_validate(tomlkit.parse(encoded).unwrap())
    assert restored.global_defaults == {'Show': None}
    assert restored.document == [f'Show={source}']


@pytest.mark.parametrize('local_override', [False, True])
def test_materializing_an_inherited_patch_removes_its_derived_parent(
    tmp_path: Path, local_override: bool
) -> None:
    defaults, local = tmp_path / 'global.toml', tmp_path / 'show.toml'
    defaults.write_text('[patches.sound]\nparent="base"\nvolume=80\n')
    local.write_text('[patches.sound]\nvolume=90\n' if local_override else '')
    path = ['patches', 'sound']
    inherited = data_files.Location(file=defaults, path=path)
    trace = data_files.Trace(
        files={defaults: defaults.read_bytes(), local: local.read_bytes()},
        locations={
            'Show': {
                tuple(path): data_files.Location(file=local, path=path)
                if local_override
                else inherited
            }
        },
        inherited={'Show': {tuple(path): inherited}},
        roots={'Show': local},
        consumers={local: ['Show'], defaults: ['Show', 'Other']},
    )
    program = {'type': 'program-change', 'instrument': 'synth', 'program': 7}
    change = setup_sync_sources.destination(
        setup_sync_store.Change(
            setup='Show',
            path=path,
            base=setup_sync_store.Value(
                present=True, value={'parent': 'base', 'volume': 80}
            ),
            host=setup_sync_store.Value(
                present=True, value={'parent': 'base', 'volume': 80}
            ),
            pedal=setup_sync_store.Value(present=True, value=program),
            choice=setup_sync_store.Choice.pedal,
            note='Materialize the complete program selection.',
        ),
        trace,
        {},
        {},
    )
    document = tomlkit.parse(local.read_text())
    setup_sync_sources.apply_change(document, change)
    resolved = global_config.overlay(
        tomlkit.parse(defaults.read_text()).unwrap(), document.unwrap()
    )
    assert resolved['patches']['sound'] == program
    assert defaults.read_text().startswith('[patches.sound]\nparent="base"')
