import hashlib
import json
import os
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from multiprocessing.connection import Listener
from multiprocessing.context import AuthenticationError
from pathlib import Path
from unittest.mock import MagicMock

import pytest
from test_firmware import hex_record

from cntrl.firmware.intel_hex import AddressRange, read_hex
from cntrl.maker.morningstar.mc import boot_switch, upload_helper
from cntrl.patch_chooser.devices import Device
from cntrl.upload import Bootloader, Serve, Upload


def test_upload_request_uses_authenticated_local_bytes(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    key = os.urandom(32)
    (tmp_path / 'authentication.key').write_bytes(key)
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', tmp_path)
    request = Upload(
        image=(tmp_path / 'firmware.hex').resolve(), sha256='a' * 64, serial='12345'
    )
    expected = upload_helper.UploadResult(returncode=0, output='validated bytes')

    def respond() -> bytes:
        with listener.accept() as connection:
            assert connection.poll(5)
            payload = connection.recv_bytes(upload_helper.REQUEST_LIMIT)
            connection.send_bytes(expected.model_dump_json().encode())
            return payload

    with Listener(family=upload_helper.IPC_FAMILY, authkey=key) as listener:
        monkeypatch.setattr(upload_helper, 'SOCKET_PATH', listener.address)
        with ThreadPoolExecutor(max_workers=1) as workers:
            pending = workers.submit(respond)
            assert upload_helper.request_upload(request) == expected
            assert Upload.model_validate_json(pending.result(timeout=5)) == request


def test_helper_keeps_listening_after_a_client_disconnect_or_bad_authentication(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    listener = MagicMock()
    listener.accept.side_effect = [
        EOFError('disconnected'),
        AuthenticationError('bad authentication'),
        KeyboardInterrupt,
    ]
    factory = MagicMock()
    factory.return_value.__enter__.return_value = listener
    monkeypatch.setattr(upload_helper, 'Listener', factory)
    monkeypatch.setattr(upload_helper, 'authentication_key', lambda: b'key')
    monkeypatch.setattr(upload_helper, 'updater_path', lambda: Path('tycmd'))
    monkeypatch.setattr(os, 'umask', MagicMock())
    with pytest.raises(KeyboardInterrupt):
        upload_helper.serve('12345')
    assert listener.accept.call_count == 3
    assert 'disconnected' in capsys.readouterr().err


@pytest.fixture
def firmware_path(tmp_path: Path) -> Path:
    data = bytearray(0x3000)
    words = {
        0x000: 0x42464346,
        0x004: 0x56010000,
        0x00C: 0x00030301,
        0x044: 0x00080401,
        0x050: 0x00800000,
        0x080: 0x0A1804EB,
        0x084: 0x26043206,
        0x090: 0x24040405,
        0x0B0: 0x00000406,
        0x0D0: 0x08180420,
        0x100: 0x081804D8,
        0x110: 0x08180402,
        0x114: 0x00002004,
        0x130: 0x00000460,
        0x1C0: 256,
        0x1C4: 4096,
        0x1C8: 1,
        0x1D0: 65536,
    }
    for a, v in words.items():
        struct.pack_into('<I', data, a, v)
    struct.pack_into(
        '<8I',
        data,
        0x1000,
        0x432000D1,
        0x60001401,
        0,
        0,
        0x60001020,
        0x60001000,
        0x60002400,
        0,
    )
    struct.pack_into('<3I', data, 0x1020, 0x60000000, len(data), 0)
    path = tmp_path / 'firmware.hex'
    records = [hex_record(4, bytes.fromhex('6000'))]
    records.extend(
        hex_record(0, bytes(data[a : a + 16]), a) for a in range(0, len(data), 16)
    )
    records.extend([hex_record(5, bytes.fromhex('60001000')), hex_record(1)])
    path.write_text('\n'.join(records))
    return path


@pytest.mark.parametrize(
    'arguments, expected',
    [
        (['serve', '--serial', '12345'], Serve(serial='12345')),
        (['bootloader', '--serial', '12345'], Bootloader(serial='12345')),
        (
            [
                'upload',
                '--image',
                '/worktree/firmware.hex',
                '--sha256',
                'a' * 64,
                '--serial',
                '12345',
            ],
            Upload(
                image=Path('/worktree/firmware.hex'), sha256='a' * 64, serial='12345'
            ),
        ),
    ],
)
def test_adapter_module_dispatches_helper_commands(
    arguments: list[str],
    expected: Serve | Upload | Bootloader,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    received: list[Serve | Upload | Bootloader] = []
    monkeypatch.setattr(upload_helper, 'run', received.append)
    monkeypatch.setattr(sys, 'argv', ['upload-helper', *arguments])
    upload_helper.main()
    assert received == [expected]


def test_adapter_module_returns_upload_output_and_exit_status(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(
        upload_helper,
        'request_upload',
        lambda request: upload_helper.UploadResult(
            returncode=7, output='upload output\n'
        ),
    )
    monkeypatch.setattr(
        sys,
        'argv',
        [
            'upload-helper',
            'upload',
            '--image',
            '/worktree/firmware.hex',
            '--sha256',
            'a' * 64,
            '--serial',
            '12345',
        ],
    )
    with pytest.raises(SystemExit) as error:
        upload_helper.main()
    assert error.value.code == 7
    assert capsys.readouterr().out == 'upload output\n'


def test_bootloader_selects_special_baud_without_serial_payload(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    if sys.platform == 'win32':
        pytest.skip('POSIX tty line-coding check')

    import pty
    import termios

    master, slave = pty.openpty()
    try:
        monkeypatch.setattr(
            upload_helper,
            'connected',
            lambda: [
                Device(
                    port=Path(os.ttyname(slave)),
                    name='cntrl',
                    serial='12345',
                )
            ],
        )
        upload_helper.request_bootloader('12345')
        after = termios.tcgetattr(slave)
        assert after[4:6] == [termios.B134, termios.B134]
        os.set_blocking(master, False)
        with pytest.raises(BlockingIOError):
            os.read(master, 1)
    finally:
        os.close(master)
        os.close(slave)


def test_bootloader_uses_the_identified_pedal_and_special_line_coding(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    backend = MagicMock()
    monkeypatch.setattr(upload_helper, 'Serial', backend)
    monkeypatch.setattr(
        upload_helper,
        'connected',
        lambda: [Device(port=Path('COM10'), name='cntrl', serial='12345')],
    )
    upload_helper.request_bootloader('12345')
    backend.assert_called_once_with('COM10', baudrate=134, timeout=5, write_timeout=5)
    backend.return_value.__enter__.return_value.write.assert_not_called()


@pytest.mark.parametrize(
    'body',
    [
        b'{}\n',
        b'not JSON\n',
        b'{"sha256":"' + b'a' * 64 + b'"}',
        b'{"sha256":"' + b'a' * 64 + b'","path":"/some/file"}\n',
        b'{"image":"/worktree/image.hex","sha256":"'
        + b'a' * 64
        + b'","command":"reset"}\n',
        b'{"image":"/worktree/image.hex","sha256":"'
        + b'a' * 64
        + b'","board":"another"}\n',
        b' ' * (upload_helper.REQUEST_LIMIT + 1) + b'\n',
    ],
)
def test_invalid_requests_never_reach_uploader(
    body: bytes, monkeypatch: pytest.MonkeyPatch
) -> None:
    def unexpected_upload(request: Upload) -> upload_helper.UploadResult:
        pytest.fail('invalid request reached the uploader')

    monkeypatch.setattr(upload_helper, 'upload_image', unexpected_upload)
    result = upload_helper.process_request(body, '12345')
    assert result.returncode == 1
    assert result.output.startswith('Upload helper:')


def test_valid_boot_image_is_accepted(firmware_path: Path) -> None:
    upload_helper.validate_firmware(read_hex(firmware_path))


def test_doom_is_kept_in_its_separately_linked_flash_slot(firmware_path: Path) -> None:
    image = read_hex(firmware_path)
    origin = boot_switch.DOOM_ORIGIN
    data = bytearray(image.data)
    for offset, value in {
        0x1004: origin + 0x1401,
        0x1010: origin + 0x1020,
        0x1014: origin + 0x1000,
        0x1018: origin + len(data) - 3072,
    }.items():
        struct.pack_into('<I', data, offset, value)
    doom = image.model_copy(
        update={
            'origin': origin,
            'entry': origin + 0x1000,
            'regions': [AddressRange(start=origin, end=origin + len(data))],
            'data': bytes(data),
        }
    )
    cntrl = image.model_copy(update={'origin': 0x60100000})
    application = boot_switch.append_doom(cntrl, doom)
    offset = origin - cntrl.origin
    assert application[: len(cntrl.data)] == cntrl.data
    assert application[len(cntrl.data) : offset] == b'\xff' * (offset - len(cntrl.data))
    assert application[offset:] == doom.data
    with pytest.raises(ValueError, match='contiguous'):
        boot_switch.append_doom(cntrl, image)
    with pytest.raises(ValueError, match='does not fit'):
        boot_switch.append_doom(
            cntrl.model_copy(update={'data': bytes(offset + 1)}), doom
        )


def test_helper_rejects_another_unit_before_upload(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    upload = MagicMock()
    monkeypatch.setattr(upload_helper, 'upload_image', upload)
    check = upload_helper.Check(serial='67890')
    result = upload_helper.process_request(
        check.model_dump_json().encode() + b'\n', '12345'
    )
    assert result.returncode == 1 and 'request serial differs' in result.output
    upload.assert_not_called()


def test_helper_can_be_checked_before_resetting_a_pedal(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    updater = tmp_path / 'tycmd'
    updater.write_bytes(b'qualified uploader')
    monkeypatch.setattr(upload_helper, 'updater_path', lambda: updater)
    check = upload_helper.Check(serial='12345')
    result = upload_helper.process_request(
        check.model_dump_json().encode() + b'\n', '12345'
    )
    assert result.returncode == 0
    assert result.uploader_sha256 == hashlib.sha256(updater.read_bytes()).hexdigest()


def test_stock_bootloader_request_checks_usb_model_and_unit(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    from types import SimpleNamespace

    backend = MagicMock()
    monkeypatch.setattr(upload_helper, 'Serial', backend)
    port = SimpleNamespace(
        vid=0x331B,
        pid=7,
        product='Morningstar MC6 Pro',
        serial_number='12345',
        device='COM7',
    )
    monkeypatch.setattr(upload_helper, 'comports', lambda: [port])
    board = upload_helper.Board(
        tag='12345-Morningstar',
        serial='12345',
        description='Morningstar MC6 Pro',
        model='Generic',
        capabilities=['serial'],
    )
    assert upload_helper.request_stock_bootloader(board)
    backend.assert_called_once_with('COM7', baudrate=134, timeout=5, write_timeout=5)
    backend.reset_mock()
    port.serial_number = 'another-unit'
    assert not upload_helper.request_stock_bootloader(board)
    backend.assert_not_called()


def test_upload_client_does_not_create_server_state(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    cache = tmp_path / 'server-cache'
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', cache)
    request = Upload(image=tmp_path / 'firmware.hex', sha256='0' * 64, serial='12345')
    with pytest.raises(FileNotFoundError):
        upload_helper.request_upload(request)
    assert not cache.exists()


def test_flash_record_prefers_the_checkout_over_the_user_log(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    home = tmp_path / 'home'
    checkout = tmp_path / 'checkout'
    package = checkout / 'cntrl'
    package.mkdir(parents=True)
    (package / 'upload.py').touch()
    record = checkout / 'doc' / 'flash-record.md'
    record.parent.mkdir()
    record.touch()
    monkeypatch.setattr(Path, 'home', lambda: home)
    monkeypatch.chdir(package)
    assert upload_helper.flash_record_path() == record
    monkeypatch.chdir(tmp_path)
    assert upload_helper.flash_record_path() == home / '.cntrl/doc/flash-record.md'


@pytest.mark.parametrize('location', ['symlink', 'relative', 'extension'])
def test_disallowed_image_locations_never_reach_device_access(
    firmware_path: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    location: str,
) -> None:
    worktree = tmp_path / 'worktree'
    worktree.mkdir()
    source = firmware_path
    if location == 'symlink':
        source = worktree / 'firmware.hex'
        source.symlink_to(firmware_path)
    elif location == 'relative':
        source = Path('firmware.hex')
    elif location == 'extension':
        source = worktree / 'firmware.txt'
        source.write_bytes(firmware_path.read_bytes())

    def unexpected_run(
        *args: object, **kwargs: object
    ) -> subprocess.CompletedProcess[str]:
        pytest.fail('disallowed image caused device access')

    monkeypatch.setattr(subprocess, 'run', unexpected_run)
    request = Upload(image=source, sha256='0' * 64, serial='12345')
    result = upload_helper.process_request(
        request.model_dump_json().encode() + b'\n', '12345'
    )
    assert result.returncode == 1
    assert (
        'path must be absolute' in result.output
        or 'non-symlink HEX file' in result.output
    )


@pytest.mark.parametrize('offset', [0x0C, 0x1000, 0x1004, 0x100C, 0x1024, 0x2400])
def test_unsafe_boot_envelopes_are_rejected(firmware_path: Path, offset: int) -> None:
    image = read_hex(firmware_path)
    data = bytearray(image.data)
    data[offset] ^= 1
    with pytest.raises(ValueError):
        upload_helper.validate_firmware(image.model_copy(update={'data': bytes(data)}))


@pytest.mark.parametrize('platform', ['darwin', 'linux', 'win32'])
def test_explicit_updater_overrides_platform_default(
    platform: str, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    executable = tmp_path / 'custom-tycmd'
    monkeypatch.setenv('CNTRL_TYCMD', str(executable))
    monkeypatch.setattr(sys, 'platform', platform)
    assert upload_helper.updater_path() == executable


@pytest.mark.parametrize('override', [None, ''])
def test_default_requires_provisioning_before_serving(
    override: str | None, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.delenv('CNTRL_TYCMD', raising=False)
    if override is not None:
        monkeypatch.setenv('CNTRL_TYCMD', override)
    monkeypatch.setattr(sys, 'platform', 'darwin')
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', tmp_path / 'cache')
    with pytest.raises(ValueError, match='provision before starting the helper'):
        upload_helper.serve('12345')
    assert not (tmp_path / 'cache').exists()


def test_direct_flash_provisions_only_a_missing_pinned_updater(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.delenv('CNTRL_TYCMD', raising=False)
    monkeypatch.setattr(sys, 'platform', 'darwin')
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', tmp_path)
    directory = tmp_path / 'tytools-0.9.9'
    updater = directory / 'tycmd'
    provision = MagicMock(return_value=updater)
    monkeypatch.setattr(upload_helper.updater_provision, 'provision', provision)

    assert upload_helper.updater_path(provision_missing=True) == updater
    provision.assert_called_once_with(directory)
    directory.mkdir()
    updater.write_bytes(b'wrong checksum')
    provision.reset_mock()
    with pytest.raises(ValueError, match='checksum mismatch'):
        upload_helper.updater_path(provision_missing=True)
    provision.assert_not_called()


@pytest.mark.parametrize('platform', ['linux', 'freebsd'])
def test_unsupported_platform_requires_explicit_updater_before_serving(
    platform: str, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.delenv('CNTRL_TYCMD', raising=False)
    monkeypatch.setattr(sys, 'platform', platform)
    cache = tmp_path / 'cache'
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', cache)
    with pytest.raises(ValueError, match='set CNTRL_TYCMD explicitly'):
        upload_helper.serve('12345')
    assert not cache.exists()


def test_changed_image_is_rejected_before_device_access(
    firmware_path: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', tmp_path / 'staging')

    def unexpected_run(
        *args: object, **kwargs: object
    ) -> subprocess.CompletedProcess[str]:
        pytest.fail('changed image caused device access')

    monkeypatch.setattr(subprocess, 'run', unexpected_run)
    request = Upload(image=firmware_path, sha256='0' * 64, serial='12345')
    result = upload_helper.process_request(
        request.model_dump_json().encode() + b'\n', '12345'
    )
    assert result.returncode == 1
    assert 'SHA-256 changed' in result.output


@pytest.mark.parametrize('wrong_target', [False, True])
@pytest.mark.parametrize('mode', ['manual', 'install', 'update', 'rollback'])
def test_upload_uses_snapshot_and_only_the_expected_halfkay(
    firmware_path: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    wrong_target: bool,
    mode: str,
) -> None:
    original = firmware_path.read_bytes()
    digest = hashlib.sha256(original).hexdigest()
    updater = tmp_path / 'tycmd'
    updater.write_bytes(b'qualified uploader')
    record = tmp_path / 'doc' / 'flash-record.md'
    monkeypatch.setenv('CNTRL_TYCMD', str(updater))
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', tmp_path / 'staging')
    monkeypatch.setattr(upload_helper, 'flash_record_path', lambda: record)
    if mode == 'rollback':
        monkeypatch.setattr(boot_switch, 'STOCK_SHA256', digest)
        monkeypatch.setattr(
            upload_helper,
            'validate_firmware',
            MagicMock(side_effect=ValueError('not combined')),
        )
    calls: list[list[str]] = []
    snapshots: list[Path] = []

    def run(
        command: list[str], *, capture_output: bool, text: bool, check: bool
    ) -> subprocess.CompletedProcess[str]:
        calls.append(command)
        assert capture_output and text and not check
        assert command[0] == str(updater)
        if command[1] == 'list':
            boards = [
                {
                    'tag': '12345-Teensy',
                    'serial': 'different' if wrong_target else '12345',
                    'description': 'HalfKay',
                    'model': 'Teensy 4.1',
                    'capabilities': ['upload', 'reset'],
                }
            ]
            return subprocess.CompletedProcess(command, 0, json.dumps(boards), '')
        assert command[1:4] == ['upload', '--board', '12345-Teensy']
        assert len(command) == 5
        snapshot = Path(command[4])
        snapshots.append(snapshot)
        assert snapshot != firmware_path
        firmware_path.write_text('changed after validation')
        assert snapshot.read_bytes() == original
        if len(snapshots) == 2:
            return subprocess.CompletedProcess(command, 0, 'reset', '')
        return subprocess.CompletedProcess(
            command,
            7,
            'upload@12345-Teensy Uploading...\n',
            "I/O error while writing to 'IOService:/private/device'\n",
        )

    monkeypatch.setattr(subprocess, 'run', run)
    request = (
        Upload(image=firmware_path, sha256=digest, serial='12345')
        if mode == 'manual'
        else upload_helper.ManagedUpload(
            image=firmware_path,
            sha256=digest,
            serial='12345',
            first_install=mode != 'update',
        )
    )
    result = upload_helper.process_request(
        request.model_dump_json().encode() + b'\n', '12345'
    )
    if wrong_target:
        assert result.returncode == 1
        assert len(calls) == 1
        assert firmware_path.read_bytes() == original
        assert not record.exists()
    else:
        assert result.returncode == 7
        assert 'I/O error while writing' in result.output
        assert len(calls) == 2
        assert not snapshots[0].exists()
        firmware_path.write_bytes(original)
        retry = upload_helper.process_request(
            request.model_dump_json().encode() + b'\n', '12345'
        )
        assert retry.returncode == 0
        entries = record.read_text()
        assert entries.count('\n## ') == 2
        assert entries.count(digest) == 2
        assert entries.index('Failed (uploader exit 7)') < entries.index('Succeeded')
        assert "I/O error while writing to 'IOService:/…'" in entries
        assert '/private/device' not in entries
        assert '12345' not in entries
        assert ('I/O error' in capsys.readouterr().out) == (mode == 'manual')
