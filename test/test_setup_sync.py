"""Three-way semantics, source provenance and interrupted publication recovery."""

from collections.abc import Mapping
from pathlib import Path

import pytest
import tomlkit
import tyro

from cntrl.patch_switcher import binary_format, data_files, setup_sync


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
    setup_sync.advance(root, snapshot, ['Show'])
    original = setup_sync.base_record(root)
    publish = setup_sync.write_record

    def write(path: Path, value: Mapping[str, object]) -> None:
        if interrupted and path == root / 'base.toml':
            raise OSError('interrupted baseline publication')
        publish(path, value)

    monkeypatch.setattr(setup_sync, 'write_record', write)
    if interrupted:
        with pytest.raises(OSError, match='interrupted baseline'):
            setup_sync.migrate_baselines(root)
        with pytest.raises(ValueError, match='interrupted apply'):
            setup_sync.check_pending(root)
        setup_sync.recover(root / 'pending.toml')
        assert setup_sync.base_record(root) == original
        assert setup_sync.baseline(root) == {'Show': data}
    else:
        setup_sync.migrate_baselines(root)
        assert setup_sync.baseline(root) == {'Show': binary_format.wrap(data)}
        entry = tomlkit.parse(setup_sync.base_record(root).decode())['setups']['Show']
        assert (root / entry['file']).parent == snapshot
        assert setup_sync.base_record(root) != original
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
        'A', *[setup_sync.Value(present=True, value=v) for v in (base, host, pedal)]
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
        'A', *[setup_sync.Value(present=True, value=v) for v in (base, host, pedal)]
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
    setup_sync.write_record(
        root / 'pending.toml',
        {
            'review': str(folder / 'review.toml'),
            'sources': [{'file': str(source), 'before': 'before', 'after': 'after'}],
        },
    )
    with pytest.raises(ValueError, match='interrupted apply'):
        setup_sync.check_pending(root)
    setup_sync.recover(root / 'pending.toml')
    assert source.read_bytes() == b'brightness=10\n'
    assert (root / 'base.toml').read_bytes() == b'old-base'
    setup_sync.check_pending(root)


def test_targeted_toml_change_keeps_comments_and_relative_paths() -> None:
    original = '# setup\nbrightness = 10 # dim\n[fonts]\nsmall = "fonts/source.ttf"\n'
    document = tomlkit.parse(original)
    setup_sync.assign(
        document, ['brightness'], setup_sync.Value(present=True, value=20)
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
    assert not (folder / 'complete.toml').exists()
