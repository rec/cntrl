import hashlib
import json
import struct
import subprocess
from pathlib import Path

import pytest
import tyro
from test_firmware import hex_record

from cntrl import upload_helper
from cntrl.firmware import read_hex


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
    records.extend(hex_record(0, data[a : a + 16], a) for a in range(0, len(data), 16))
    records.extend([hex_record(5, bytes.fromhex('60001000')), hex_record(1)])
    path.write_text('\n'.join(records))
    return path


def test_cli_uses_explicit_serve_and_upload_commands() -> None:
    assert isinstance(
        tyro.cli(upload_helper.Serve | upload_helper.Upload, args=['serve']),
        upload_helper.Serve,
    )
    options = tyro.cli(
        upload_helper.Serve | upload_helper.Upload,
        args=['upload', '--sha256', 'a' * 64],
    )
    assert isinstance(options, upload_helper.Upload)
    assert options.sha256 == 'a' * 64


@pytest.mark.parametrize(
    'body',
    [
        b'{}\n',
        b'not JSON\n',
        b'{"sha256":"' + b'a' * 64 + b'"}',
        b'{"sha256":"' + b'a' * 64 + b'","path":"/some/file"}\n',
        b'{"sha256":"' + b'a' * 64 + b'","command":"reset"}\n',
        b'{"sha256":"' + b'a' * 64 + b'","board":"another"}\n',
        b' ' * 257 + b'\n',
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


@pytest.mark.parametrize('offset', [0x0C, 0x1000, 0x1004, 0x100C, 0x1024, 0x2400])
def test_unsafe_boot_envelopes_are_rejected(firmware_path: Path, offset: int) -> None:
    image = read_hex(firmware_path)
    data = bytearray(image.data)
    data[offset] ^= 1
    with pytest.raises(ValueError):
        upload_helper.validate_firmware(image.model_copy(update={'data': bytes(data)}))


def test_changed_image_is_rejected_before_device_access(
    firmware_path: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(upload_helper, 'IMAGE_PATH', firmware_path)
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', tmp_path / 'staging')

    def unexpected_run(
        *args: object, **kwargs: object
    ) -> subprocess.CompletedProcess[str]:
        pytest.fail('changed image caused device access')

    monkeypatch.setattr(subprocess, 'run', unexpected_run)
    request = upload_helper.Upload(sha256='0' * 64)
    result = upload_helper.process_request(request.model_dump_json().encode() + b'\n')
    assert result.returncode == 1
    assert 'SHA-256 changed' in result.output


@pytest.mark.parametrize('wrong_target', [False, True])
def test_upload_uses_snapshot_and_only_the_expected_halfkay(
    firmware_path: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    wrong_target: bool,
) -> None:
    original = firmware_path.read_bytes()
    digest = hashlib.sha256(original).hexdigest()
    monkeypatch.setattr(upload_helper, 'IMAGE_PATH', firmware_path)
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', tmp_path / 'staging')
    calls: list[list[str]] = []
    snapshots: list[Path] = []

    def run(
        command: list[str], *, capture_output: bool, text: bool, check: bool
    ) -> subprocess.CompletedProcess[str]:
        calls.append(command)
        assert capture_output and text and not check
        assert command[0] == str(upload_helper.UPLOADER_PATH)
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
    request = upload_helper.Upload(sha256=digest)
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
