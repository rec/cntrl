import hashlib
import json
import os
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from multiprocessing.connection import Listener
from pathlib import Path
from unittest.mock import MagicMock

import pytest
from test_firmware import hex_record

from cntrl.firmware.intel_hex import read_hex
from cntrl.maker.morningstar.mc import upload_helper
from cntrl.patch_switcher.devices import Device


def test_upload_request_uses_authenticated_local_bytes(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    key = os.urandom(32)
    (tmp_path / 'authentication.key').write_bytes(key)
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', tmp_path)
    request = upload_helper.Upload(
        image=(tmp_path / 'firmware.hex').resolve(), sha256='a' * 64
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
            assert (
                upload_helper.Upload.model_validate_json(pending.result(timeout=5))
                == request
            )


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
        (['serve'], upload_helper.Serve()),
        (['bootloader'], upload_helper.Bootloader()),
        (
            ['upload', '--image', '/worktree/firmware.hex', '--sha256', 'a' * 64],
            upload_helper.Upload(image=Path('/worktree/firmware.hex'), sha256='a' * 64),
        ),
    ],
)
def test_adapter_module_dispatches_helper_commands(
    arguments: list[str],
    expected: upload_helper.Serve | upload_helper.Upload | upload_helper.Bootloader,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    received: list[
        upload_helper.Serve | upload_helper.Upload | upload_helper.Bootloader
    ] = []
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
        ],
    )
    with pytest.raises(SystemExit) as error:
        upload_helper.main()
    assert error.value.code == 7
    assert capsys.readouterr().out == 'upload output\n'


def test_worktree_discovery_includes_primary_and_linked_checkout(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    primary = tmp_path / 'primary checkout'
    secondary = tmp_path / 'agent checkout'
    subprocess.run(['git', 'init', str(primary)], check=True, capture_output=True)
    subprocess.run(
        [
            'git',
            '-C',
            str(primary),
            '-c',
            'user.name=Test',
            '-c',
            'user.email=test@example.com',
            'commit',
            '--allow-empty',
            '--no-gpg-sign',
            '-m',
            'Initial test commit',
        ],
        check=True,
        capture_output=True,
    )
    subprocess.run(
        ['git', '-C', str(primary), 'worktree', 'add', '--detach', str(secondary)],
        check=True,
        capture_output=True,
    )
    for root in (primary, secondary):
        monkeypatch.setattr(upload_helper, 'ROOT', root)
        assert upload_helper.project_worktrees() == [
            primary.resolve(),
            secondary.resolve(),
        ]


@pytest.mark.skipif(sys.platform == 'win32', reason='POSIX tty line-coding check')
def test_bootloader_selects_special_baud_without_serial_payload(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
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
                    serial=upload_helper.BOARD_SERIAL,
                )
            ],
        )
        upload_helper.request_bootloader()
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
        lambda: [
            Device(port=Path('COM10'), name='cntrl', serial=upload_helper.BOARD_SERIAL)
        ],
    )
    upload_helper.request_bootloader()
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
    def unexpected_upload(request: upload_helper.Upload) -> upload_helper.UploadResult:
        pytest.fail('invalid request reached the uploader')

    monkeypatch.setattr(upload_helper, 'upload_image', unexpected_upload)
    result = upload_helper.process_request(body)
    assert result.returncode == 1
    assert result.output.startswith('Upload helper:')


def test_valid_boot_image_is_accepted(firmware_path: Path) -> None:
    upload_helper.validate_firmware(read_hex(firmware_path))


def test_upload_client_does_not_create_server_state(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    cache = tmp_path / 'server-cache'
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', cache)
    request = upload_helper.Upload(image=tmp_path / 'firmware.hex', sha256='0' * 64)
    with pytest.raises(FileNotFoundError):
        upload_helper.request_upload(request)
    assert not cache.exists()


@pytest.mark.parametrize('location', ['outside', 'symlink', 'relative', 'extension'])
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
    monkeypatch.setattr(upload_helper, 'project_worktrees', lambda: [worktree])

    def unexpected_run(
        *args: object, **kwargs: object
    ) -> subprocess.CompletedProcess[str]:
        pytest.fail('disallowed image caused device access')

    monkeypatch.setattr(subprocess, 'run', unexpected_run)
    request = upload_helper.Upload(image=source, sha256='0' * 64)
    result = upload_helper.process_request(request.model_dump_json().encode() + b'\n')
    assert result.returncode == 1
    assert (
        'path must be absolute' in result.output
        or 'registered project worktree' in result.output
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
def test_macos_defaults_to_installed_morningstar_updater(
    override: str | None, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.delenv('CNTRL_TYCMD', raising=False)
    if override is not None:
        monkeypatch.setenv('CNTRL_TYCMD', override)
    monkeypatch.setattr(sys, 'platform', 'darwin')
    assert upload_helper.updater_path() == Path(
        '/Applications/Morningstar Firmware Updater.app/'
        'Contents/Resources/extraResources/tycmd'
    )


@pytest.mark.parametrize('platform', ['linux', 'win32'])
def test_unsupported_platform_requires_explicit_updater_before_serving(
    platform: str, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.delenv('CNTRL_TYCMD', raising=False)
    monkeypatch.setattr(sys, 'platform', platform)
    cache = tmp_path / 'cache'
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', cache)
    with pytest.raises(ValueError, match='set CNTRL_TYCMD explicitly'):
        upload_helper.serve()
    assert not cache.exists()


def test_changed_image_is_rejected_before_device_access(
    firmware_path: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(upload_helper, 'project_worktrees', lambda: [tmp_path])
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', tmp_path / 'staging')

    def unexpected_run(
        *args: object, **kwargs: object
    ) -> subprocess.CompletedProcess[str]:
        pytest.fail('changed image caused device access')

    monkeypatch.setattr(subprocess, 'run', unexpected_run)
    request = upload_helper.Upload(image=firmware_path, sha256='0' * 64)
    result = upload_helper.process_request(request.model_dump_json().encode() + b'\n')
    assert result.returncode == 1
    assert 'SHA-256 changed' in result.output


@pytest.mark.parametrize('wrong_target', [False, True])
@pytest.mark.parametrize('worktree_index', [0, 1])
def test_upload_uses_snapshot_and_only_the_expected_halfkay(
    firmware_path: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    wrong_target: bool,
    worktree_index: int,
) -> None:
    original = firmware_path.read_bytes()
    digest = hashlib.sha256(original).hexdigest()
    updater = tmp_path / 'tycmd'
    monkeypatch.setenv('CNTRL_TYCMD', str(updater))
    roots = [tmp_path / 'primary', tmp_path / 'other worktree']
    roots[worktree_index] = tmp_path
    monkeypatch.setattr(upload_helper, 'project_worktrees', lambda: roots)
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', tmp_path / 'staging')
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
                    'tag': upload_helper.BOARD_TAG,
                    'serial': 'different'
                    if wrong_target
                    else upload_helper.BOARD_SERIAL,
                    'description': 'HalfKay',
                    'model': 'Teensy 4.1',
                    'capabilities': ['upload', 'reset'],
                }
            ]
            return subprocess.CompletedProcess(command, 0, json.dumps(boards), '')
        assert command[1:4] == ['upload', '--board', upload_helper.BOARD_TAG]
        assert len(command) == 5
        snapshot = Path(command[4])
        snapshots.append(snapshot)
        assert snapshot != firmware_path
        firmware_path.write_text('changed after validation')
        assert snapshot.read_bytes() == original
        return subprocess.CompletedProcess(
            command, 7, 'uploader output', 'uploader error'
        )

    monkeypatch.setattr(subprocess, 'run', run)
    request = upload_helper.Upload(image=firmware_path, sha256=digest)
    result = upload_helper.process_request(request.model_dump_json().encode() + b'\n')
    if wrong_target:
        assert result.returncode == 1
        assert len(calls) == 1
        assert firmware_path.read_bytes() == original
    else:
        assert result.returncode == 7
        assert result.output == 'uploader outputuploader error'
        assert len(calls) == 2
        assert not snapshots[0].exists()
