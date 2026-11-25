"""First-install gates and device-free preparation of packaged firmware."""

from pathlib import Path
from unittest.mock import Mock
from uuid import uuid4

import pytest

from cntrl.firmware import distribution
from cntrl.maker.morningstar.mc import flash_target, release, upload_helper
from cntrl.patch_chooser import compiler_inputs, transport
from cntrl.patch_chooser.setup_sync_store import write_record


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


@pytest.mark.parametrize('use_helper', [False, True])
@pytest.mark.parametrize(
    'first_install, success_at, stock_at, recovery_status, error, recover',
    [
        (True, 1, None, '', '', False),
        (True, 6, None, '', '', False),
        (True, None, 2, '', 'Morningstar stock 3.14.0 was restored', False),
        (True, None, None, '', 'internal PROGRAM button', False),
        (False, 6, None, '', '', False),
        (False, 1, None, ' model=mc6-pro git=123abcd', '', False),
        (False, 1, None, 'legacy', '', False),
        (False, None, None, '', 'could not verify the firmware update', False),
        (False, 1, None, '', '', True),
    ],
)
def test_flash_recovers_silently_and_rolls_back_only_first_installs(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    first_install: bool,
    success_at: int | None,
    stock_at: int | None,
    recovery_status: str,
    error: str,
    use_helper: bool,
    recover: bool,
) -> None:
    destination = tmp_path / 'install'
    history = tmp_path / 'flash-record.md'
    editor_backup = tmp_path / 'banks'
    editor_backup.write_bytes(b'editor backup')
    if use_helper:
        (tmp_path / 'authentication.key').touch()
    stock = tmp_path / release.STOCK.filename
    uploads: list[upload_helper.ManagedUpload] = []
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
    stock_image = Mock(return_value=stock)
    monkeypatch.setattr(release, 'stock_image', stock_image)
    monkeypatch.setattr(
        flash_target, 'read_hex', Mock(return_value=Mock(sha256='a' * 64))
    )
    monkeypatch.setattr(
        flash_target.data_files,
        'load_target',
        Mock(return_value=compiler_inputs.read_document(target_file)),
    )
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', tmp_path)
    updater_path = Mock()
    monkeypatch.setattr(upload_helper, 'updater_path', updater_path)
    monkeypatch.setattr(upload_helper, 'stock_board', Mock())
    monkeypatch.setattr(
        upload_helper, 'request_stock_bootloader', Mock(return_value=True)
    )
    monkeypatch.setattr(flash_target, 'wait_halfkay', Mock(return_value=True))
    port = Path('pedal-port')
    halfkay = upload_helper.Board(
        tag='12345-Teensy',
        serial='12345',
        description='HalfKay',
        model='Teensy 4.1',
        capabilities=['upload'],
    )
    restored_at = None if stock_at is None else 6 + stock_at

    def upload(
        request: upload_helper.ManagedUpload | upload_helper.Check,
    ) -> upload_helper.UploadResult:
        if isinstance(request, upload_helper.Check):
            assert request.serial == '12345'
            return upload_helper.UploadResult(
                returncode=0, output='', managed_recovery=True
            )
        uploads.append(request)
        return upload_helper.UploadResult(
            returncode=0
            if len(uploads) == restored_at or recovery_status == 'legacy'
            else 1,
            output='I/O error while writing',
            attempt=upload_helper.FlashAttempt(id=uuid4(), record=history),
        )

    def list_boards() -> list[upload_helper.Board]:
        if len(uploads) == restored_at:
            return [halfkay.model_copy(update={'description': 'Morningstar MC6 Pro'})]
        return [halfkay]

    selected = Mock(side_effect=upload)
    unused = Mock()
    monkeypatch.setattr(
        upload_helper, 'request_upload', selected if use_helper else unused
    )
    monkeypatch.setattr(
        upload_helper, 'perform_request', unused if use_helper else selected
    )
    monkeypatch.setattr(upload_helper, 'list_boards', list_boards)
    monkeypatch.setattr(
        flash_target.devices,
        'connected',
        lambda: (
            [flash_target.devices.Device(port=port, serial='12345', name='cntrl')]
            if len(uploads) == success_at or (not first_install and not uploads)
            else []
        ),
    )
    ready_status = 'CNTRL READY 48 10 deadbeef model=mc6-pro git=123abcd'
    status = Mock(return_value=ready_status)
    if recovery_status:
        fields = '' if recovery_status == 'legacy' else recovery_status
        status.side_effect = [
            transport.UsbReplyError('CNTRL ERROR deployment-recovery' + fields),
            ready_status,
        ]
    monkeypatch.setattr(transport, 'device_status', status)
    monkeypatch.setattr(upload_helper, 'request_bootloader', Mock())
    monkeypatch.setattr(flash_target, 'wait_disconnected', Mock())

    def backup(path: Path, port: Path, connection: object) -> None:
        path.mkdir()
        (path / 'status.txt').write_text(ready_status)

    monkeypatch.setattr(flash_target.backups, 'backup_pedal', backup)
    restore = destination / 'restore'
    prepare_restore = Mock(return_value=restore)
    monkeypatch.setattr(flash_target.backups, 'prepare_restore', prepare_restore)
    restore_setups = Mock()
    monkeypatch.setattr(flash_target.backups, 'restore_setups', restore_setups)
    verify = Mock()
    monkeypatch.setattr(flash_target.backups, 'verify_after_flashing', verify)
    sleep = Mock()
    monkeypatch.setattr(flash_target.time, 'sleep', sleep)
    starter = Mock()
    monkeypatch.setattr(flash_target, 'install_starter', starter)
    previous = tmp_path / 'previous' if recover else None
    if previous is not None:
        previous.mkdir()
        write_record(
            previous / 'installation.toml',
            {
                'result': 'failed',
                'controller': 'mc6-pro',
                'serial': '12345',
                'backup': str(previous / 'before'),
            },
        )
    command = flash_target.Flash(
        directory=destination,
        first_install=first_install,
        recover_from=previous,
        upload_helper=use_helper,
        serial='12345',
        stock_version=release.STOCK.version if first_install else None,
        editor_backup=editor_backup if first_install else None,
        port=None if first_install or recover else port,
    )
    if error:
        with pytest.raises(flash_target.FlashError, match=error):
            flash_target.run(command)
    else:
        flash_target.run(command)
        if first_install:
            starter.assert_called_once_with(destination, port)
    assert starter.call_count == int(first_install and not error)
    assert (
        restore_setups.call_count
        == verify.call_count
        == int(not first_install and not error)
    )
    assert stock_image.call_count == int(first_install)
    if previous is not None:
        prepare_restore.assert_called_once_with(
            previous / 'before',
            restore,
            flash_target.models.Target.model_validate(
                compiler_inputs.read_document(target_file)
            ),
        )
    updater_path.assert_called_once_with(provision_missing=not use_helper)
    unused.assert_not_called()
    attempts = success_at or 6
    stock_attempts = (stock_at or 5) if first_install and not success_at else 0
    assert [u.image for u in uploads] == (
        [destination / 'firmware.hex'] * attempts + [stock] * stock_attempts
    )
    assert [u.sha256 for u in uploads[attempts:]] == [
        release.STOCK.sha256
    ] * stock_attempts
    delays = [0.1, 0.2, 0.3, 1, 2]
    assert [c.args[0] for c in sleep.call_args_list] == (
        delays[: attempts - 1] + delays[:stock_attempts]
    )
    entries = history.read_text()
    assert entries.count('### Reconnect ') == len(uploads) + (
        recovery_status == 'legacy'
    )
    assert ('Expected build running' in entries) == bool(success_at)
    assert ('Morningstar stock running' in entries) == bool(stock_at)
    assert 'I/O error' not in capsys.readouterr().out


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
