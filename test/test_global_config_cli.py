"""Recover inherited rig sources and binary assets without the original machine."""

import sys
from contextlib import nullcontext
from hashlib import sha256
from pathlib import Path

import pytest
import tomlkit

from cntrl.animation import asset
from cntrl.patch_chooser import (
    cli,
    data_files,
    device_globals,
    devices,
    global_config,
    midi_assets,
    setup_sync,
    source_archive,
    transport,
)
from cntrl.patch_chooser.setup_compiler import compile_inputs


def test_contact_saves_offline_and_syncs_with_later_compiles(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(global_config, 'user_path', lambda: tmp_path / 'global.toml')
    connected: list[devices.Device] = []
    monkeypatch.setattr(devices, 'connected', lambda: connected)
    contact = 'Éloïse\nowner@example.org'

    def run(*arguments: str) -> None:
        monkeypatch.setattr(sys, 'argv', ['cntrl', *arguments])
        cli.main()

    run('global', 'set', 'name_and_contact', contact)
    run('global', 'list', 'name_and_contact')
    assert capsys.readouterr().out == contact + '\n'

    commands: list[str] = []

    def request(client: transport.SetupClient, command: str) -> str:
        commands.append(command)
        if command == 'CNTRL STATUS':
            return 'CNTRL READY name_and_contact=1'
        return 'CNTRL OK'

    connected.append(devices.Device(port=Path('/unused'), name='CNTRL'))
    monkeypatch.setattr(transport, 'SerialConnection', nullcontext)
    monkeypatch.setattr(transport.SetupClient, 'request', request)
    source = tmp_path / 'show.toml'
    source.write_text('brightness = 160\n')
    output = tmp_path / 'show.bin'
    run(
        'compile',
        str(source),
        '--target',
        str(ROOT / 'firmware/test/target.json'),
        '--output',
        str(output),
    )
    assert contact.encode() not in output.read_bytes()
    assert commands == [
        'CNTRL STATUS',
        f'CNTRL GLOBAL SET name_and_contact {contact.encode().hex()}',
    ]
    run('global', 'clear', 'name_and_contact')
    assert commands[-1] == 'CNTRL GLOBAL CLEAR name_and_contact'


@pytest.mark.parametrize('content', [None, '[invalid', 'format = 12'])
def test_global_restore_rejects_bad_files_before_opening_usb(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, content: str | None
) -> None:
    path = tmp_path / 'globals.toml'
    if content is not None:
        path.write_text(content)

    def unexpected_port(port: Path | None = None) -> Path:
        pytest.fail('A bad restore file must be rejected before selecting a device')

    monkeypatch.setattr(devices, 'select_port', unexpected_port)
    monkeypatch.setattr(sys, 'argv', ['cntrl', 'global', 'restore', str(path)])
    with pytest.raises(SystemExit) as caught:
        cli.main()
    assert str(path) in str(caught.value)


@pytest.mark.parametrize('error_type', [ValueError, OSError])
def test_global_cli_preserves_unexpected_failures(
    monkeypatch: pytest.MonkeyPatch, error_type: type[Exception]
) -> None:
    failure = error_type('unexpected preference failure')

    def fail(command: device_globals.List) -> None:
        raise failure

    monkeypatch.setattr(device_globals, 'run', fail)
    monkeypatch.setattr(sys, 'argv', ['cntrl', 'global', 'list'])
    with pytest.raises(error_type) as caught:
        cli.main()
    assert caught.value is failure


@pytest.mark.parametrize(
    'arguments, command, reply, output',
    [
        (
            ['list', 'default_firmware'],
            'CNTRL BOOT DEFAULT',
            'CNTRL BOOT DEFAULT stock',
            'default_firmware = stock\n',
        ),
        (
            ['set', 'default_firmware', 'stock'],
            'CNTRL BOOT DEFAULT stock',
            'CNTRL OK',
            '',
        ),
        (['clear', 'default_firmware'], 'CNTRL BOOT DEFAULT cntrl', 'CNTRL OK', ''),
    ],
)
def test_default_firmware_uses_the_pedals_boot_setting(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    arguments: list[str],
    command: str,
    reply: str,
    output: str,
) -> None:
    def request(client: transport.SetupClient, sent: str) -> str:
        assert sent == command
        return reply

    monkeypatch.setattr(transport.SetupClient, 'request', request)
    monkeypatch.setattr(transport, 'SerialConnection', nullcontext)
    monkeypatch.setattr(devices, 'select_port', lambda port: Path('/unused'))
    monkeypatch.setattr(sys, 'argv', ['cntrl', 'global', *arguments])
    cli.main()
    assert capsys.readouterr().out == output


@pytest.mark.parametrize('reply', ['CNTRL GLOBAL 999=1', 'CNTRL GLOBAL startup_set=zz'])
def test_invalid_global_settings_are_reported_as_pedal_reply_errors(
    monkeypatch: pytest.MonkeyPatch, reply: str
) -> None:
    client = transport.SetupClient(transport.SerialConnection(Path('/unused')))
    monkeypatch.setattr(client, 'request', lambda command: reply)
    with pytest.raises(transport.UsbReplyError):
        client.global_settings()


class ArchivedSource:
    def __init__(self, archive: bytes) -> None:
        self.archive = archive

    def download_source(self, identifier: str) -> bytes:
        assert identifier == sha256(self.archive).hexdigest()
        return self.archive


def test_recovered_rig_recompiles_with_its_original_defaults_and_assets(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    rig = tmp_path / 'rig'
    rig.mkdir()
    instruments = rig / 'instruments'
    instruments.mkdir()
    (instruments / 'synth.toml').write_text(
        "[instruments.synth]\noutput='din'\nchannel=1\n"
    )
    (instruments / 'live.toml').write_text('[instruments.synth]\nchannel=2\n')
    defaults = rig / 'global.toml'
    defaults.write_text("""instrument_files = ['instruments/synth.toml']
brightness = 161
splash_movie = ['intro.cnsa']
[patches.sound]
instrument = 'synth'
program = 10
[patches.play]
operation = 'play-midi-file'
filename = 'intro.mid'
""")
    monkeypatch.setattr(global_config, 'user_path', lambda: defaults)
    root = rig / 'show.toml'
    root.write_text("""instrument_files = ['instruments/live.toml']
[patches.sound]
program = 12
[patch_cycles]
main = ['sound', 'play']
""")
    geometry = asset.Geometry(width=8, height=8)
    movie = asset.encode_frames(bytes(128), [100], geometry)
    (rig / 'intro.cnsa').write_bytes(movie)
    midi = bytes.fromhex('4d546864000000060000000100604d54726b0000000400ff2f00')
    (rig / 'intro.mid').write_bytes(midi)
    target_geometry = rig / 'geometry.toml'
    target_geometry.write_text(tomlkit.dumps({'animation': geometry.model_dump()}))
    inputs = data_files.Sources(
        document=[f'Live={root}'],
        target=[str(ROOT / 'firmware/test/target.json'), '+', str(target_geometry)],
    )
    trace = data_files.Trace()
    movies: dict[int, bytes] = {}
    original = compile_inputs(inputs, movies=movies, trace=trace)
    assert midi_assets.collect(original.setups, trace.files) == {'intro.mid': midi}
    assert trace.files[rig / 'intro.cnsa'] == movie
    assert instruments / 'synth.toml' in trace.files
    assert instruments / 'live.toml' in trace.files
    archived = source_archive.create(inputs, trace, original.setups)
    recovered = tmp_path / 'recovered'
    files = source_archive.download(
        ArchivedSource(archived),
        {'Live': source_archive.attach(original.setups['Live'], archived)},
    )
    cli.publish_download(files, recovered)
    rig.rename(tmp_path / 'unavailable-original-machine')
    different = tmp_path / 'different-defaults.toml'
    different.write_text('brightness = 2\n')
    monkeypatch.setattr(global_config, 'user_path', lambda: different)
    session = next((recovered / 'sessions').iterdir())
    restored_trace = data_files.Trace()
    restored = compile_inputs(
        data_files.Sources(document=[str(session / 'Live.toml')]),
        movies={},
        trace=restored_trace,
    )
    assert setup_sync.canonical(restored.setups['Live']) == setup_sync.canonical(
        original.setups['Live']
    )
    assert midi_assets.collect(restored.setups, restored_trace.files) == {
        'intro.mid': midi
    }


ROOT = Path(__file__).resolve().parents[1]
