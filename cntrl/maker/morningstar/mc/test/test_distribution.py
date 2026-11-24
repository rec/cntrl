"""First-install gates and device-free preparation of packaged firmware."""

from pathlib import Path
from unittest.mock import Mock
from uuid import uuid4

import pytest

from cntrl.firmware import distribution
from cntrl.maker.morningstar.mc import flash_target, release, upload_helper
from cntrl.patch_chooser import compiler_inputs, transport


@pytest.mark.parametrize(
    'options',
    [
        {'first_install': True},
        {'first_install': True, 'serial': '12345', 'stock_version': 'old'},
        {'stock_version': '3.14.0'},
    ],
)
def test_first_install_requires_explicit_supported_stock_and_backup(
    options: dict[str, object], tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    prepare = Mock()
    monkeypatch.setattr(release, 'prepare', prepare)
    with pytest.raises(ValueError):
        flash_target.run(
            flash_target.Flash.model_validate(
                {'directory': tmp_path / 'install', **options}
            )
        )
    prepare.assert_not_called()


def test_installation_refuses_to_overwrite_previous_evidence(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match='preserve previous evidence'):
        flash_target.run(flash_target.Flash(directory=tmp_path, prepare_only=True))


@pytest.mark.parametrize(
    'reported_commit, usb_description, outcome',
    [
        ('123abcd', '', 'Expected build running (`123abcd`)'),
        ('234abcd', '', 'Different firmware running (`234abcd`)'),
        ('', '', 'CNTRL reconnected, firmware unverified (UsbReplyError)'),
        (None, '', 'CNTRL did not reconnect before timeout'),
        (None, 'HalfKay', 'CNTRL did not reconnect before timeout (still in HalfKay)'),
        (None, 'Morningstar MC6 Pro', 'Different firmware running (Morningstar stock)'),
    ],
)
def test_failed_first_upload_records_what_booted_before_starter_installation(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    reported_commit: str | None,
    usb_description: str,
    outcome: str,
) -> None:
    destination = tmp_path / 'install'
    history = tmp_path / 'flash-record.md'
    editor_backup = tmp_path / 'banks'
    editor_backup.write_bytes(b'editor backup')
    (tmp_path / 'authentication.key').touch()
    attempt = upload_helper.FlashAttempt(id=uuid4(), record=history)
    result = upload_helper.UploadResult(returncode=1, output='', attempt=attempt)
    manifest = Mock(
        controller='mc6-pro',
        source_commit='123abcd' + '0' * 33,
        stock=release.STOCK,
        target='target.toml',
        files={},
    )

    def prepare(resources: Path, directory: Path) -> distribution.Manifest:
        directory.mkdir()
        return manifest

    target_file = Path(release.__file__).with_name('patch-chooser-target.json')
    monkeypatch.setattr(release, 'prepare', prepare)
    monkeypatch.setattr(
        flash_target, 'read_hex', Mock(return_value=Mock(sha256='a' * 64))
    )
    monkeypatch.setattr(
        flash_target.data_files,
        'load_target',
        Mock(return_value=compiler_inputs.read_document(target_file)),
    )
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', tmp_path)
    monkeypatch.setattr(upload_helper, 'updater_path', Mock())
    monkeypatch.setattr(upload_helper, 'stock_board', Mock())
    monkeypatch.setattr(
        upload_helper, 'request_stock_bootloader', Mock(return_value=True)
    )
    monkeypatch.setattr(flash_target, 'wait_halfkay', Mock(return_value=True))
    monkeypatch.setattr(flash_target, 'check_helper', Mock(return_value=result))
    monkeypatch.setattr(upload_helper, 'request_upload', Mock(return_value=result))
    boards = (
        [Mock(serial='12345', description=usb_description)] if usb_description else []
    )
    monkeypatch.setattr(upload_helper, 'list_boards', Mock(return_value=boards))
    wait = Mock(return_value=Path('pedal-port'))
    if reported_commit is None:
        wait.side_effect = TimeoutError('pedal did not return')
    monkeypatch.setattr(flash_target, 'wait_connected', wait)
    status = Mock(
        return_value=f'CNTRL READY 48 10 deadbeef model=mc6-pro git={reported_commit}'
    )
    if reported_commit == '':
        status.side_effect = transport.UsbReplyError('CNTRL ERROR deployment-recovery')
    monkeypatch.setattr(transport, 'device_status', status)
    starter = Mock()
    monkeypatch.setattr(flash_target, 'install_starter', starter)

    with pytest.raises(
        (ValueError, TimeoutError), match='starter installation|did not return'
    ):
        flash_target.run(
            flash_target.Flash(
                directory=destination,
                first_install=True,
                serial='12345',
                stock_version=release.STOCK.version,
                editor_backup=editor_backup,
            )
        )

    starter.assert_not_called()
    entries = history.read_text()
    assert f'(#attempt-{attempt.id.hex}), uploader exit 1' in entries
    assert 'Expected build `123abcd`' in entries
    assert outcome in entries


def test_manifest_paths_and_hashes_match_committed_payload() -> None:
    resources = Path(release.__file__).with_name('distribution')
    if not resources.exists():
        pytest.skip('compiled resource commit follows implementation commit')
    manifest = distribution.read_manifest(resources)
    assert manifest.stock == release.STOCK
    assert manifest.controller == 'mc6-pro'
    assert not any(n.endswith('.hex') for n in manifest.files)


def test_starter_has_no_musical_actions() -> None:
    from cntrl.patch_chooser import compiler, data_files, models

    root = Path(release.__file__).parent
    errors: list[str] = []
    target = models.Target.model_validate(
        data_files.load_target(
            data_files.DataFile(
                name='target', paths=[root / 'patch-chooser-target.json']
            ),
            True,
            errors,
        )
    )
    assert not errors
    source = compiler_inputs.read_document(root / 'starter.toml')
    data = compiler.compile_show(source, {'profiles': []}, [], target)
    result = models.Setup.model_validate(
        compiler_inputs.read_document(root / 'starter.toml')
    )
    assert not result.patches and not result.instruments
    assert result.midi == {}
    assert not result.usb_device_midi and not result.usb_host_midi
    assert data
