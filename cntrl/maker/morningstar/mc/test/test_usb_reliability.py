"""Offline qualification policy and supervision; no USB/MIDI endpoints are opened."""

import json
import os
import subprocess
import sys
from pathlib import Path
from threading import Event
from unittest.mock import Mock

import pytest
import tomlkit

from cntrl.maker.morningstar.mc import qualify_usb_midi, usb_reliability
from cntrl.patch_chooser.core_midi import Midi
from cntrl.patch_chooser.transport import SetupClient


@pytest.fixture
def health() -> dict[str, str]:
    return {
        'device': 'CNTRL READY 47 starts=100 starts_status=ready',
        'heap': 'CNTRL HEAP valid=1 failures=0 interrupt_allocations=0',
        'route_0': 'CNTRL MIDI 1 0 100 100 0 0 1 '
        'bridge_quarantine=0 stream_quarantine=0 pedal_quarantine=0 '
        'uart_rx_drops=0 uart_overrun_latches=0',
    }


@pytest.mark.parametrize('pressure', [False, True])
@pytest.mark.parametrize(
    'field,old,new',
    [
        ('device', 'starts=100', 'starts=101'),
        ('device', 'starts_status=ready', 'starts_status=pending'),
        ('heap', 'valid=1', 'valid=0'),
        ('heap', 'interrupt_allocations=0', 'interrupt_allocations=1'),
        ('route_0', 'uart_rx_drops=0', 'uart_rx_drops=1'),
        ('route_0', 'uart_overrun_latches=0', 'uart_overrun_latches=1'),
        ('route_0', 'bridge_quarantine=0', 'bridge_quarantine=1'),
        ('route_0', 'stream_quarantine=0', 'stream_quarantine=1'),
        ('route_0', 'pedal_quarantine=0', 'pedal_quarantine=1'),
        ('route_0', '100 100 0 0', '100 100 0 1'),
    ],
)
def test_transport_health_failures_are_never_accepted_as_overload(
    health: dict[str, str], pressure: bool, field: str, old: str, new: str
) -> None:
    changed = health | {field: health[field].replace(old, new)}
    with pytest.raises(ValueError):
        usb_reliability.check_health(health, changed, pressure=pressure)


@pytest.mark.parametrize(
    'field,old,new',
    [
        ('heap', 'failures=0', 'failures=7'),
        ('route_0', '100 100 0 0', '100 100 7 0'),
    ],
)
def test_only_pressure_allows_counted_admission_failures(
    health: dict[str, str], field: str, old: str, new: str
) -> None:
    changed = health | {field: health[field].replace(old, new)}
    with pytest.raises(ValueError):
        usb_reliability.check_health(health, changed)
    usb_reliability.check_health(health, changed, pressure=True)


def test_unchanged_prior_counts_are_not_mistaken_for_new_losses(
    health: dict[str, str],
) -> None:
    health['heap'] = health['heap'].replace('failures=0', 'failures=7')
    usb_reliability.check_health(health, health)


def test_pressure_parser_keeps_all_notes_and_sustain_in_order() -> None:
    lifecycle = qualify_usb_midi.note_lifecycle()
    messages = b''.join(qualify_usb_midi.short_messages())
    assert usb_reliability.held_messages(messages + lifecycle + messages) == lifecycle
    assert usb_reliability.held_messages(messages) == b''
    with pytest.raises(ValueError):
        usb_reliability.held_messages(lifecycle[:-1])
    with pytest.raises(ValueError):
        usb_reliability.held_messages(b'\xf0\xf7')


def test_profiles_use_units_and_keep_upload_resets_out_of_runtime_soak() -> None:
    root = Path(usb_reliability.__file__).parent / 'qualification'
    runtime = usb_reliability.load_profile(root / 'usb-reliability.toml')
    assert runtime.soak_seconds == 28800 and runtime.midi_period == 0.02
    schedule = usb_reliability.phase_schedule(runtime)
    assert sum(p == usb_reliability.Phase.connections for p, _ in schedule) == 100
    assert schedule[0][0] == usb_reliability.Phase.prepare
    assert schedule[-1][0] == usb_reliability.Phase.restore
    assert all(p != usb_reliability.Phase.upload for p, _ in schedule)
    upload = usb_reliability.load_profile(root / 'usb-uploader.toml')
    assert len(usb_reliability.phase_schedule(upload)) == 3
    with pytest.raises(ValueError, match='separate profile'):
        runtime.model_copy().model_validate(
            runtime.model_dump()
            | {
                'stages': ['upload', 'soak'],
            }
        )
    with pytest.raises(ValueError, match='DIN wire capacity'):
        usb_reliability.Profile.model_validate(
            runtime.model_dump() | {'midi_period': 0.001}
        )


def test_preview_neither_accesses_devices_nor_creates_live_evidence(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    def unexpected_access(*args: object, **kwargs: object) -> None:
        pytest.fail('preview attempted hardware access')

    monkeypatch.setattr(usb_reliability, 'require_inventory', unexpected_access)
    monkeypatch.setattr(usb_reliability, 'Midi', unexpected_access)
    monkeypatch.setattr(usb_reliability, 'RecordedConnection', unexpected_access)
    root = Path(usb_reliability.__file__).parent
    output = tmp_path / 'report.toml'
    usb_reliability.run_reliability(
        root / 'qualification/usb-reliability.toml', output, execute=False
    )
    plan = tomlkit.parse(capsys.readouterr().out)
    assert plan['state'] == 'planned'
    assert not output.exists() and not output.with_suffix('').exists()


def test_recorded_protocol_failure_retains_the_original_bytes(tmp_path: Path) -> None:
    evidence = usb_reliability.Evidence(tmp_path, 65536)
    connection = usb_reliability.RecordedConnection(Path('/not-opened'), evidence)
    try:
        error = connection.protocol_error('malformed header', b'\x89NTRL\r\n')
        assert 'malformed header' in str(error)
        saved = tomlkit.parse(evidence.path.read_text())
        assert saved['success'] is False
        assert '894e54524c0d0a' in (tmp_path / 'trace-0.log').read_text()
    finally:
        evidence.stream.close()


def test_raw_capture_rotates_without_losing_the_latest_evidence(tmp_path: Path) -> None:
    evidence = usb_reliability.Evidence(tmp_path, 512)
    try:
        for i in range(100):
            evidence.capture('rx', bytes([i]) * 40)
        files = list(tmp_path.glob('trace-*.log'))
        assert len(files) == 2
        assert sum(p.stat().st_size for p in files) <= 512
        assert any((bytes([99]) * 40).hex() in p.read_text() for p in files)
        evidence.capture('rx', b'x' * 10000 + b'last')
        assert sum(p.stat().st_size for p in files) <= 512
        assert any(b'last'.hex() in p.read_text() for p in files)
        for i in range(128):
            evidence.capture('rx', bytes([i]) * i)
            assert sum(p.stat().st_size for p in files) <= 512
        assert all(b'\r\n' not in p.read_bytes() for p in files)
    finally:
        evidence.stream.close()


@pytest.mark.parametrize(
    'behavior,expected',
    [
        ('passed', True),
        ('failed', False),
        ('malformed', False),
    ],
)
def test_supervisor_requires_both_a_success_report_and_a_clean_worker_exit(
    tmp_path: Path, behavior: str, expected: bool
) -> None:
    def capture(pid: int, directory: Path) -> None:
        (directory / 'failure-captured.txt').write_text(str(pid))

    script = Path(__file__).parent / 'fixtures/reliability_worker.py'
    command = [
        sys.executable,
        str(script),
        '--directory',
        str(tmp_path),
        '--behavior',
        behavior,
    ]
    assert (
        usb_reliability.supervise(
            command,
            tmp_path,
            5 if behavior != 'slow_upload' else 0.5,
            uploading=behavior == 'slow_upload',
            capture=capture,
            maximum_seconds=5 if behavior == 'busy' else None,
        )
        is expected
    )
    assert (tmp_path / 'evidence.txt').read_text() == 'retained bytes'
    if not expected:
        assert (tmp_path / 'failure-captured.txt').exists()


@pytest.mark.parametrize('behavior', ['hung', 'busy', 'slow_upload'])
def test_supervisor_deadlines_preserve_evidence_and_protect_uploads(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, behavior: str
) -> None:
    class Clock:
        now = 0.0

        def monotonic(self) -> float:
            return self.now

        def sleep(self, seconds: float) -> None:
            self.now += seconds
            assert self.now < 10  # A broken supervisor must not loop forever.

    clock = Clock()

    class Worker:
        pid = 1234
        returncode: int | None = None
        terminated = False

        def poll(self) -> int | None:
            if behavior == 'busy':
                heartbeat = tmp_path / 'heartbeat.txt'
                stamp = round((clock.now + 1) * 1_000_000_000)
                os.utime(heartbeat, ns=(stamp, stamp))
            if (
                behavior == 'slow_upload'
                and (tmp_path / 'failure-captured.txt').exists()
            ):
                (tmp_path / 'upload-finished.txt').write_text('not interrupted')
                self.returncode = 0
            return self.returncode

        def terminate(self) -> None:
            self.terminated = True
            self.returncode = -15

        def wait(self, timeout: float) -> int:
            assert self.returncode is not None
            return self.returncode

    worker = Worker()

    def start(command: list[str], **options: object) -> Worker:
        assert options['start_new_session'] is True
        (tmp_path / 'heartbeat.txt').write_text('started')
        (tmp_path / 'evidence.txt').write_text('retained bytes')
        (tmp_path / 'result.toml').write_text('success = true\n')
        return worker

    captures: list[float] = []

    def capture(pid: int, directory: Path) -> None:
        assert pid == worker.pid
        captures.append(clock.now)
        (directory / 'failure-captured.txt').write_text(str(pid))

    monkeypatch.setattr(usb_reliability, 'time', clock)
    monkeypatch.setattr(usb_reliability.subprocess, 'Popen', start)
    assert not usb_reliability.supervise(
        ['worker'],
        tmp_path,
        0.5 if behavior == 'slow_upload' else 5,
        uploading=behavior == 'slow_upload',
        capture=capture,
        maximum_seconds=5 if behavior == 'busy' else None,
    )
    assert len(captures) == 1
    assert (
        0.5 < captures[0] <= 0.7
        if behavior == 'slow_upload'
        else 5 < captures[0] <= 5.3
    )
    assert worker.terminated is (behavior != 'slow_upload')
    assert (tmp_path / 'evidence.txt').read_text() == 'retained bytes'
    report = tomlkit.parse((tmp_path / 'supervisor.toml').read_text())
    assert report['stalled'] is True
    assert report['worker_alive'] is False
    assert report['worker_report_passed'] is True
    assert report['deadline_exceeded'] is (behavior == 'busy')
    if behavior == 'slow_upload':
        assert (tmp_path / 'upload-finished.txt').read_text() == 'not interrupted'


def test_stalled_worker_is_terminated_and_reaped(tmp_path: Path) -> None:
    script = Path(__file__).parent / 'fixtures/reliability_worker.py'
    with subprocess.Popen(
        [
            sys.executable,
            str(script),
            '--directory',
            str(tmp_path),
            '--behavior',
            'hung',
        ],
        stdout=subprocess.PIPE,
        text=True,
    ) as process:
        try:
            assert process.stdout is not None
            assert process.stdout.readline() == 'ready\n'
            assert process.poll() is None
            assert usb_reliability.stop_worker(process)
            assert process.returncode is not None and process.returncode != 0
        finally:
            if process.poll() is None:
                process.kill()
    assert (tmp_path / 'evidence.txt').read_text() == 'retained bytes'


def test_recovery_is_offered_only_for_a_completed_backup(tmp_path: Path) -> None:
    profile = usb_reliability.Profile(target_export=tmp_path, hub='hub', cable='cable')
    assert usb_reliability.recovery_commands(profile, tmp_path) == []
    before = tmp_path / 'before'
    before.mkdir()
    (before / 'backup.toml').write_text('complete = true')
    (before / 'selected.txt').write_text('original')
    commands = usb_reliability.recovery_commands(profile, tmp_path)
    assert commands[0][:4] == ['cntrl', 'setup', 'set', 'original']
    assert commands[-1][2] == 'verify'
    upload = usb_reliability.recovery_commands(profile, tmp_path, temporary=False)
    assert all(c[2] != 'del' for c in upload)
    (before / 'backup.toml').write_text('complete = false')
    assert usb_reliability.recovery_commands(profile, tmp_path) == []
    (before / 'backup.toml').write_text('complete = true')
    (before / 'backup-pending.toml').touch()
    assert usb_reliability.recovery_commands(profile, tmp_path) == []


def test_frozen_export_keeps_fonts_and_does_not_copy_old_device_backups(
    tmp_path: Path,
) -> None:
    source = tmp_path / 'export'
    source.mkdir()
    (source / 'build.toml').write_text('image = "firmware.hex"\ntarget = "target.toml"')
    (source / 'firmware.hex').write_bytes(b'image')
    (source / 'target.toml').write_text('description = "target"')
    (source / 'fonts').mkdir()
    (source / 'fonts/Arimo.cfont').write_bytes(b'font')
    (source / 'distribution').mkdir()
    (source / 'distribution/application.bin').write_bytes(b'application')
    (source / 'before').mkdir()
    (source / 'before/private.txt').write_text('old backup')
    frozen = tmp_path / 'frozen'
    hashes = usb_reliability.freeze_export(source, frozen)
    assert (frozen / 'fonts/Arimo.cfont').read_bytes() == b'font'
    assert hashes[str(Path('fonts/Arimo.cfont'))] == usb_reliability.digest(b'font')
    assert not (frozen / 'before').exists()
    assert (frozen / 'distribution/application.bin').read_bytes() == b'application'


def test_qualification_resolves_fonts_from_the_frozen_export(tmp_path: Path) -> None:
    source = Path(usb_reliability.__file__).parent / 'patch-chooser-target.json'
    data = json.loads(source.read_text())
    data['font_sources'] = {'Arimo': 'fonts/Arimo.cfont'}
    (tmp_path / 'target.toml').write_text(tomlkit.dumps(data))
    target = usb_reliability.read_target(tmp_path)
    assert Path(target.font_sources['Arimo']) == tmp_path / 'fonts/Arimo.cfont'


@pytest.mark.parametrize('fault', ['none', 'drop', 'extra-cleanup'])
def test_background_midi_requires_exact_delivery_while_serial_owner_can_block(
    tmp_path: Path, fault: str
) -> None:
    ready = Event()

    class Loopback(Midi):
        def __init__(self) -> None:
            self.data = bytearray()

        def send(self, endpoint: int, data: bytes) -> None:
            if fault != 'drop':
                self.data.extend(data)
            if fault == 'extra-cleanup':
                self.data.extend(b'\xb0\x40\x00')
            ready.set()

    evidence = usb_reliability.Evidence(tmp_path, 65536)
    traffic = usb_reliability.MidiTraffic(Loopback(), 1, 1, evidence)
    try:
        traffic.thread.start()
        assert ready.wait(5)
        if fault != 'none':
            with pytest.raises(ValueError, match='MIDI mismatch'):
                traffic.finish(0)
            report = qualify_usb_midi.analyze_report(evidence.path)
            trials = report['trials']
            assert isinstance(trials, list) and len(trials) == 1
            trial = trials[0]
            assert isinstance(trial, dict)
            assert trial['matches'] is False
            assert trial['sent_bytes'] == len(traffic.expected)
        else:
            traffic.finish(0)
            assert traffic.received == traffic.expected
        assert len(traffic.expected) >= 4099
    finally:
        traffic.stop.set()
        traffic.thread.join(5)
        evidence.stream.close()


@pytest.mark.parametrize(
    'data,valid',
    [
        (b'', True),
        (
            bytes.fromhex(
                'b04000b04100b04200b04300b04400b04500b07b00b07800b07900'
                'b14000b14100b14200b14300b14400b14500b17b00b17800b17900'
                'b14200b14300b14400b14500b17b00b17800b17900'
            ),
            True,
        ),
        (b'\xb0\x40', False),
        (b'\xb0\x40\x7f', False),
        (b'\xb0\x01\x00', False),
        (b'\xc0\x00', False),
        (b'\x80\x30\x00', False),
        (b'\xf0\xf7', False),
    ],
)
def test_setup_handoff_accepts_only_recorded_channel_resets(
    tmp_path: Path, data: bytes, valid: bool
) -> None:
    class Capture(Midi):
        def __init__(self) -> None:
            self.data = bytearray(data)

    midi = Capture()
    evidence = usb_reliability.Evidence(tmp_path, 65536)
    try:
        if valid:
            usb_reliability.capture_setup_cleanup(midi, evidence, 0)
            assert not midi.data
        else:
            with pytest.raises(
                ValueError, match='unexpected MIDI during setup cleanup'
            ):
                usb_reliability.capture_setup_cleanup(midi, evidence, 0)
            assert bytes(midi.data) == data
        report = tomlkit.parse(evidence.path.read_text())
        assert report['setup_cleanup']['received_hex'] == data.hex()
        assert report['setup_cleanup']['matches'] is valid
    finally:
        evidence.stream.close()


def test_concurrent_downloads_drain_setup_cleanup_before_the_next_payload(
    tmp_path: Path, health: dict[str, str], monkeypatch: pytest.MonkeyPatch
) -> None:
    ready = Event()

    class Clock:
        now = 0.0

        def monotonic(self) -> float:
            return self.now

        def sleep(self, seconds: float) -> None:
            pass

    class Loopback(Midi):
        def __init__(self) -> None:
            self.data = bytearray()

        def send(self, endpoint: int, data: bytes) -> None:
            self.data.extend(data)
            ready.set()

    clock, midi = Clock(), Loopback()

    class Client(SetupClient):
        def __init__(self) -> None:
            self.selections: list[str] = []

        def download_bundle(self) -> bytes:
            assert ready.wait(5)
            ready.clear()
            clock.now += 1
            return b'bundle'

        def select(self, name: str) -> None:
            self.selections.append(name)
            midi.data.extend(b'\xb0\x40\x00')

    monkeypatch.setattr(usb_reliability, 'time', clock)
    monkeypatch.setattr(usb_reliability, 'require_inventory', Mock(return_value=[]))
    monkeypatch.setattr(qualify_usb_midi, 'diagnostics', Mock(return_value=health))
    profile = usb_reliability.Profile(
        target_export=tmp_path, hub='hub', cable='cable', midi_period=1
    )
    baseline = usb_reliability.Baseline(
        selected='original',
        diagnostics=health,
        bundle_sha256=usb_reliability.digest(b'bundle'),
        assets=[],
    )
    evidence, client = usb_reliability.Evidence(tmp_path, 65536), Client()
    try:
        usb_reliability.perform_downloads(
            profile, baseline, evidence, client, midi, 1, 6, [None]
        )
        assert evidence.data['completed_downloads'] == 6
        assert len(client.selections) == 1
        report = tomlkit.parse(evidence.path.read_text())
        assert report['setup_cleanup']['received_hex'] == 'b04000'
        assert report['traffic']['setup-cleanup-rx_bytes'] == 3
        assert not midi.data
    finally:
        evidence.stream.close()


def test_bounded_stress_keeps_counts_and_release_hook(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    class Clock:
        now = 0.0

        def monotonic(self) -> float:
            return self.now

        def sleep(self, seconds: float) -> None:
            self.now += seconds

    class Loopback(Midi):
        def __init__(self) -> None:
            self.data = bytearray()

        def send(self, endpoint: int, data: bytes) -> None:
            self.data.extend(data)

    monkeypatch.setattr(qualify_usb_midi, 'time', Clock())
    releases: list[int] = []
    result = qualify_usb_midi.stress_traffic(
        Loopback(), [1, 2], b'abc', 0.02, 3, releases.append
    )
    assert result['bursts'] == 3 and result['outputs'] == 2
    assert result['sent_bytes'] == result['received_bytes'] == 18
    assert releases == [1, 2, 3]


def test_reliability_preview_is_the_default_and_live_run_is_explicit(
    tmp_path: Path,
) -> None:
    values = {
        'reliability': tmp_path / 'profile.toml',
        'report': tmp_path / 'report.toml',
    }
    assert not qualify_usb_midi.Options.model_validate(values).run
    assert qualify_usb_midi.Options.model_validate(values | {'run': True}).run
    for extra in (
        {'inspect': True},
        {'report': None},
        {'destination': 'ignored'},
        {'port': tmp_path / 'ignored'},
    ):
        with pytest.raises(ValueError, match='profile settings'):
            qualify_usb_midi.Options.model_validate(values | extra)
