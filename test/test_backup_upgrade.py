"""Offline backup conversion preserves originals and saved history identity."""

from pathlib import Path

import tomlkit

from cntrl.patch_switcher import backup_upgrade, compiler, models, resolved, songs


def test_backup_conversion_preserves_originals_and_history_metadata(
    tmp_path: Path,
) -> None:
    root = Path(__file__).resolve().parents[1]
    source = tmp_path / 'before'
    setups = source / 'setups'
    history = source / 'history/example'
    setups.mkdir(parents=True)
    history.mkdir(parents=True)
    current = bytes.fromhex((root / 'test/data/legacy-34.golden').read_text())
    older = bytes.fromhex((root / 'test/data/legacy-20.golden').read_text())
    (setups / 'example.bin').write_bytes(current)
    (setups / 'example.toml').write_text('splash_movie = []\n')
    (history / '123.bin').write_bytes(older)
    metadata = {
        'number': '123',
        'modified': 12345,
        'length': len(older),
        'label': 'Pedal edit',
        'name': 'example',
        'version': 20,
    }
    (source / 'history.toml').write_text(tomlkit.dumps({'revisions': [metadata]}))
    (source / 'selected.txt').write_text('example')
    library = songs.encode(songs.Library())
    (source / 'songs.bin').write_bytes(library)
    output = tmp_path / 'after'
    backup_upgrade.run(
        backup_upgrade.Upgrade(
            directory=source, output=output, target=root / 'firmware/test/target.json'
        )
    )
    assert (setups / 'example.bin').read_bytes() == current
    assert (history / '123.bin').read_bytes() == older
    converted = (output / 'history/example/123.bin').read_bytes()
    assert converted != older
    models.Setup.model_validate(tomlkit.parse(resolved.export(converted)).unwrap())
    assert (output / 'selected.txt').read_text() == 'example'
    assert (output / 'songs.bin').read_bytes() == library
    revision = tomlkit.parse((output / 'history.toml').read_text())['revisions'][0]
    assert revision['number'] == '123'
    assert revision['label'] == 'Pedal edit'
    assert revision['modified'] == 12345
    assert revision['version'] == compiler.DOCUMENT_VERSION
    assert revision['length'] == len(converted)
