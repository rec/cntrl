"""Supervised USB qualification; importing or previewing never accesses hardware."""

import hashlib
import os
import platform
import shlex
import shutil
import subprocess
import sys
import time
from collections.abc import Callable, Generator
from contextlib import contextmanager
from enum import StrEnum, auto
from pathlib import Path
from threading import Event, Lock, Thread
from typing import Annotated, TextIO

import tomlkit
import tyro
from pydantic import BaseModel, ConfigDict, Field, model_validator
from reccy.configuration.units import Seconds
from serial.tools.list_ports import comports
from tomlkit.exceptions import ParseError

from .... import build
from ....firmware.intel_hex import read_hex
from ....patch_switcher import backups, cli, data_files, models
from ....patch_switcher.core_midi import Midi
from ....patch_switcher.transport import SerialConnection, SetupClient, UsbProtocolError
from ....patch_switcher.usb_frames import Frame
from . import flash_target, qualify_usb_midi, release, upload_helper


class Phase(StrEnum):
    prepare = auto()
    connections = auto()
    serial = auto()
    mixed = auto()
    soak = auto()
    pressure = auto()
    upload = auto()
    restore = auto()


class Profile(BaseModel, frozen=True):
    target_export: Path
    serial: str = Field(default='14468280', pattern=r'^[0-9]+$')
    port: Path = Path('/dev/cu.usbmodem144682801')
    source: str = 'cntrl experimental DIN'
    destination: str = 'cntrl experimental DIN'
    hub: Annotated[str, Field(min_length=1)]
    cable: Annotated[str, Field(min_length=1)]
    stages: list[Phase] = Field(
        default_factory=lambda: [
            Phase.connections,
            Phase.serial,
            Phase.mixed,
            Phase.soak,
            Phase.pressure,
        ]
    )
    connection_cycles: Annotated[int, Field(gt=0)] = 100
    serial_seconds: Annotated[Seconds, Field(gt=0)] = 1800
    mixed_seconds: Annotated[Seconds, Field(gt=0)] = 1800
    soak_seconds: Annotated[Seconds, Field(gt=0)] = 28800
    midi_period: Annotated[Seconds, Field(gt=0)] = 0.02
    settle_seconds: Annotated[Seconds, Field(gt=0)] = 2
    pressure_seconds: Annotated[Seconds, Field(gt=0)] = 20
    pressure_bursts: Annotated[int, Field(ge=2)] = 200
    upload_cycles: Annotated[int, Field(gt=0)] = 3
    stall_seconds: Annotated[Seconds, Field(gt=0)] = 60
    maintenance_seconds: Annotated[Seconds, Field(gt=0)] = 600
    trace_bytes: Annotated[int, Field(ge=65536)] = 64 * 1024 * 1024

    @model_validator(mode='after')
    def stage_contract(self) -> 'Profile':
        allowed = {
            Phase.connections,
            Phase.serial,
            Phase.mixed,
            Phase.soak,
            Phase.pressure,
        }
        if not self.stages or len(set(self.stages)) != len(self.stages):
            raise ValueError('stages must be nonempty and unique')
        if self.stages != [Phase.upload] and any(s not in allowed for s in self.stages):
            raise ValueError(
                'upload is a separate profile; prepare/restore are automatic'
            )
        if (
            sum(map(len, traffic_pattern())) / len(traffic_pattern()) / self.midi_period
            > 3125
        ):
            raise ValueError('sustained MIDI rate exceeds the DIN wire capacity')
        if self.settle_seconds < 4099 / 3125:
            raise ValueError(
                'settle time must allow the SysEx packet to drain over DIN'
            )
        if self.stall_seconds <= self.settle_seconds + self.pressure_seconds:
            raise ValueError(
                'stall timeout must exceed the pressure observation window'
            )
        return self

    model_config = ConfigDict(extra='forbid', allow_inf_nan=False)


class Worker(BaseModel, frozen=True):
    profile: Path
    directory: Path
    phase: Phase
    index: int = 0


class Asset(BaseModel, frozen=True):
    kind: str
    name: str
    sha256: str


class Baseline(BaseModel, frozen=True):
    selected: str
    diagnostics: dict[str, str]
    bundle_sha256: str
    assets: list[Asset]


class Evidence:
    """Bounded raw capture plus an independently observable progress heartbeat."""

    def __init__(self, directory: Path, limit: int) -> None:
        self.directory = directory
        self.path = directory / 'result.toml'
        self.heartbeat = directory / 'heartbeat.txt'
        self.data: dict[str, object] = {'success': False}
        self.counts: dict[str, int] = {}
        self.data['pid'] = os.getpid()
        self.limit = limit // 2
        self.slot = 0
        self.size = 0
        self.lock = Lock()
        self.stream: TextIO = (directory / 'trace-0.log').open('w', newline='\n')
        self.last_beat = 0.0
        self.beat('starting', force=True)

    def capture(self, direction: str, data: bytes) -> None:
        timestamp = time.monotonic()
        capacity = max(1, (self.limit - 128) // 2)
        with self.lock:
            for suffix, value in (('records', 1), ('bytes', len(data))):
                key = f'{direction}_{suffix}'
                self.counts[key] = self.counts.get(key, 0) + value
            for offset in range(0, max(1, len(data)), capacity):
                label = (
                    direction
                    if len(data) <= capacity
                    else f'{direction}@{offset}/{len(data)}'
                )
                chunk = data[offset : offset + capacity].hex()
                line = f'{timestamp:.9f} {label} {chunk}\n'
                if self.size and self.size + len(line) > self.limit:
                    self.stream.close()
                    self.slot = 1 - self.slot
                    self.stream = (self.directory / f'trace-{self.slot}.log').open(
                        'w', newline='\n'
                    )
                    self.size = 0
                self.stream.write(line)
                self.stream.flush()
                self.size += len(line)

    def beat(self, operation: str, *, force: bool = False) -> None:
        now = time.monotonic()
        if force or now - self.last_beat >= 1:
            self.data['operation'] = operation
            self.heartbeat.write_text(f'{now:.9f} {operation}\n')
            self.last_beat = now

    def save(self) -> None:
        with self.lock:
            self.data['traffic'] = self.counts.copy()
        qualify_usb_midi.save_report(self.path, self.data)

    def fail(self, error: str) -> None:
        self.data.setdefault('error', error)
        self.data['success'] = False
        self.save()


class RecordedConnection(SerialConnection):
    def __init__(self, port: Path, evidence: Evidence) -> None:
        super().__init__(port, timeout=5, retry=False)
        self.evidence = evidence

    def write_frame(self, data: bytes) -> None:
        self.evidence.capture('serial-tx', data)
        self.evidence.beat(
            f'serial write session={self.session:x} sequence={self.sequence}'
        )
        super().write_frame(data)

    def read_frame(self) -> Frame:
        self.evidence.beat(
            f'serial read session={self.session:x} sequence={self.sequence}'
        )
        frame = super().read_frame()
        self.evidence.capture('serial-rx', frame.encode())
        return frame

    def protocol_error(self, reason: str, response: bytes) -> UsbProtocolError:
        error = super().protocol_error(reason, response)
        self.evidence.capture('serial-invalid', response)
        self.evidence.fail(str(error))
        return error

    def close(self) -> None:
        self.evidence.beat('serial close', force=True)
        super().close()


class MidiTraffic:
    """Send paced MIDI while the sole serial owner performs blocking transfers."""

    def __init__(
        self, midi: Midi, destination: int, period: float, evidence: Evidence
    ) -> None:
        self.midi, self.destination, self.period, self.evidence = (
            midi,
            destination,
            period,
            evidence,
        )
        self.expected, self.received = bytearray(), bytearray()
        self.errors: list[OSError | ValueError] = []
        self.stop = Event()
        self.thread = Thread(target=self.send, daemon=True)

    def collect(self) -> None:
        data = bytes(self.midi.data)
        del self.midi.data[: len(data)]
        if data:
            self.received.extend(data)
            self.evidence.capture('midi-rx', data)

    def send(self) -> None:
        pattern = traffic_pattern()
        index = 0
        try:
            while not self.stop.is_set():
                data = pattern[index % len(pattern)]
                self.evidence.capture('midi-tx', data)
                self.midi.send(self.destination, data)
                self.expected.extend(data)
                self.collect()
                index += 1
                self.stop.wait(self.period)
        except (OSError, ValueError) as error:
            self.errors.append(error)
            self.stop.set()

    def finish(self, settle: float) -> None:
        self.stop.set()
        self.thread.join(5)
        if self.thread.is_alive():
            raise TimeoutError('CoreMIDI sender did not stop')
        time.sleep(settle)
        self.collect()
        if self.errors:
            raise self.errors[0]
        if self.received != self.expected:
            self.evidence.data['trials'] = [
                {
                    'name': 'concurrent-transfer',
                    'sent_hex': self.expected.hex(),
                    'received_hex': self.received.hex(),
                }
            ]
            self.evidence.save()
            raise ValueError(
                f'MIDI mismatch: sent {len(self.expected)}, '
                f'received {len(self.received)}'
            )


def traffic_pattern() -> list[bytes]:
    return [
        b'\xf0\x7d' + bytes(i % 128 for i in range(4096)) + b'\xf7',
        qualify_usb_midi.note_lifecycle(),
        *qualify_usb_midi.short_messages(),
    ]


def capture_setup_cleanup(midi: Midi, evidence: Evidence, settle: float) -> None:
    """Validate setup teardown separately from the next exact payload trial."""
    evidence.beat('setup cleanup', force=True)
    time.sleep(settle)
    received = bytes(midi.data)
    evidence.capture('setup-cleanup-rx', received)
    # Sink::reset_channel emits these zero-valued channel resets on handoff.
    valid = len(received) % 3 == 0 and all(
        received[i] & 0xF0 == 0xB0
        and received[i + 1] in (64, 65, 66, 67, 68, 69, 120, 121, 123)
        and received[i + 2] == 0
        for i in range(0, len(received), 3)
    )
    evidence.data['setup_cleanup'] = {
        'received_hex': received.hex(),
        'matches': valid,
        'settle_seconds': settle,
    }
    evidence.save()
    if not valid:
        raise ValueError('unexpected MIDI during setup cleanup')
    del midi.data[: len(received)]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def check_health(
    before: dict[str, str], after: dict[str, str], *, pressure: bool = False
) -> None:
    old = qualify_usb_midi.status_fields(before['device'])
    new = qualify_usb_midi.status_fields(after['device'])
    if not new.get('starts', '').isdigit() or new.get('starts_status') != 'ready':
        raise ValueError('device startup state is not ready')
    if old.get('starts') != new['starts']:
        raise ValueError('unexpected device reboot')
    heap = qualify_usb_midi.status_fields(after['heap'])
    if heap.get('valid') != '1' or heap.get('interrupt_allocations') != '0':
        raise ValueError('heap validation or interrupt allocation check failed')
    if not pressure and heap.get('failures') != qualify_usb_midi.status_fields(
        before['heap']
    ).get('failures'):
        raise ValueError('unexpected allocation failures')
    routes = [k for k in before if k.startswith('route_')]
    if routes != [k for k in after if k.startswith('route_')]:
        raise ValueError('MIDI route inventory changed')
    for key in routes:
        a, b = before[key].split(), after[key].split()
        fields = qualify_usb_midi.status_fields(after[key])
        previous = qualify_usb_midi.status_fields(before[key])
        for name in ('uart_rx_drops', 'uart_overrun_latches'):
            if name not in fields or fields[name] != previous.get(name):
                raise ValueError(f'{key}: {name} changed or unavailable')
        for name in ('bridge_quarantine', 'stream_quarantine', 'pedal_quarantine'):
            if int(fields.get(name, '-1'), 16) != 0:
                raise ValueError(f'{key}: {name} is active')
        if len(b) < 8 or b[7] != a[7] or (not pressure and b[6] != a[6]):
            raise ValueError(f'{key}: unexpected MIDI drops/aborts')


def held_messages(data: bytes) -> bytes:
    """Validate this flood's PC/CC/note stream and retain notes and sustain."""
    result = bytearray()
    offset = 0
    while offset < len(data):
        status = data[offset] & 0xF0
        size = 2 if status == 0xC0 else 3
        packet = data[offset : offset + size]
        if (
            status not in (0x80, 0x90, 0xB0, 0xC0)
            or len(packet) != size
            or any(b > 127 for b in packet[1:])
        ):
            raise ValueError('malformed/unexpected pressure-test MIDI')
        if status in (0x80, 0x90) or (status == 0xB0 and packet[1] == 64):
            result.extend(packet)
        offset += size
    return bytes(result)


def load_profile(path: Path) -> Profile:
    profile = Profile.model_validate(tomlkit.parse(path.read_text()).unwrap())
    return profile.model_copy(
        update={'target_export': (path.parent / profile.target_export).resolve()}
    )


def freeze_export(export: Path, destination: Path) -> dict[str, str]:
    artifacts = build.read_artifacts(export)
    destination.mkdir()
    hashes: dict[str, str] = {}
    paths = [export / n for n in ('build.toml', artifacts.image, artifacts.target)]
    paths += [p for p in (export / 'fonts').rglob('*') if p.is_file()]
    paths += [p for p in (export / 'distribution').rglob('*') if p.is_file()]
    paths += [
        export / n
        for n in ('firmware.elf', 'toolchain.toml', 'memory.txt')
        if (export / n).is_file()
    ]
    if not (export / 'fonts').is_dir():
        raise ValueError('MC qualification requires the exported font assets')
    for source in paths:
        relative = source.relative_to(export)
        checksum = digest(source.read_bytes())
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        if digest(target.read_bytes()) != checksum:
            raise ValueError('build export changed while preparing qualification')
        hashes[str(relative)] = checksum
    return hashes


def read_target(export: Path) -> models.Target:
    errors: list[str] = []
    target = data_files.load_target(
        data_files.DataFile(name='target', paths=[export / 'target.toml']),
        True,
        errors,
    )
    if errors:
        raise ValueError('\n'.join(errors))
    return models.Target.model_validate(target)


def phase_schedule(profile: Profile) -> list[tuple[Phase, int]]:
    if profile.stages == [Phase.upload]:
        return [(Phase.upload, i) for i in range(profile.upload_cycles)]
    return [
        (Phase.prepare, 0),
        *(
            (s, i)
            for s in profile.stages
            for i in range(profile.connection_cycles if s == Phase.connections else 1)
        ),
        (Phase.restore, 0),
    ]


def require_inventory(profile: Profile) -> list[dict[str, str]]:
    ports = [p for p in comports() if p.serial_number == profile.serial]
    if len(ports) != 1 or Path(ports[0].device) != profile.port:
        raise ValueError('expected pedal serial endpoint is absent or ambiguous')
    if (ports[0].vid, ports[0].pid) != (0x16C0, 0x0489):
        raise ValueError('expected cntrl USB application identity is absent')
    return [
        {
            'port': p.device,
            'serial': p.serial_number or '',
            'location': p.location or '',
        }
        for p in ports
    ]


@contextmanager
def midi_connection(
    profile: Profile, evidence: Evidence
) -> Generator[tuple[Midi, int, list[int]]]:
    midi = Midi()
    try:
        sources, destinations = midi.endpoints(True), midi.endpoints(False)
        evidence.data['midi_sources'] = list(sources)
        evidence.data['midi_destinations'] = list(destinations)
        source, destination = sources[profile.source], destinations[profile.destination]
        peers = [v for k, v in destinations.items() if 'cntrl experimental' in k]
        midi.check(midi.core.MIDIPortConnectSource(midi.input, source, None))
        yield midi, destination, peers
        if sources != midi.endpoints(True) or destinations != midi.endpoints(False):
            raise ValueError('MIDI endpoint inventory changed')
    except (OSError, ValueError, KeyError) as error:
        evidence.capture('midi-rx', bytes(midi.data))
        evidence.fail(str(error))
        raise
    finally:
        evidence.beat('CoreMIDI close', force=True)
        midi.close()


def fetch_asset(client: SetupClient, asset: Asset) -> bytes:
    if asset.kind == 'font':
        return client.download_font(int(Path(asset.name).stem, 16))
    if asset.kind == 'movie':
        return client.download_movie(int(Path(asset.name).stem, 16))
    return client.download_midi(asset.name)


def prepare(profile: Profile, root: Path, evidence: Evidence) -> None:
    with RecordedConnection(profile.port, evidence) as connection:
        client = SetupClient(connection)
        before = qualify_usb_midi.diagnostics(client)
        check_health(before, before)
        target = read_target(profile.target_export)
        if client.memory().capacity != target.memory.capacity:
            raise ValueError(
                'target export does not match the installed memory capacity'
            )
        if any(e.name.lower() in SETUPS for e in client.catalogue()):
            raise ValueError(
                'qualification setup names already exist; originals are untouched'
            )
        backups.backup_pedal(root / 'before', profile.port, connection)
    fixture = Path(__file__).parent / 'qualification/dynamic-memory-thru.toml'
    with RecordedConnection(profile.port, evidence) as connection:
        cli.run_setup(
            cli.Copy(
                document=[f'{n}={fixture}' for n in SETUPS],
                target=[str(profile.target_export / 'target.toml')],
                port=profile.port,
            ),
            connection=connection,
        )
    assets = [
        Asset(kind=k, name=p.name, sha256=digest(p.read_bytes()))
        for k, pattern in [
            ('font', 'fonts/*.cfont'),
            ('movie', 'movies/*.cnsa'),
            ('midi', 'midi/*.mid'),
        ]
        for p in sorted((root / 'before/setups').glob(pattern))
    ]
    with RecordedConnection(profile.port, evidence) as connection:
        client = SetupClient(connection)
        client.select(SETUPS[0])
        baseline = Baseline(
            selected=(root / 'before/selected.txt').read_text(),
            diagnostics=qualify_usb_midi.diagnostics(client),
            bundle_sha256=digest(client.download_bundle()),
            assets=assets,
        )
        check_health(before, baseline.diagnostics)
    (root / 'baseline.toml').write_text(tomlkit.dumps(baseline.model_dump()))


def run_probe(
    profile: Profile, baseline: Baseline, evidence: Evidence, index: int
) -> None:
    with RecordedConnection(profile.port, evidence) as connection:
        before = qualify_usb_midi.diagnostics(SetupClient(connection))
        evidence.data['before'] = before
        evidence.save()
        check_health(baseline.diagnostics, before)
    # Alternation exercises both MIDI-only and simultaneous MIDI/serial clients.
    with midi_connection(profile, evidence) as (midi, destination, _):
        if index % 2:
            with RecordedConnection(profile.port, evidence) as connection:
                qualify_usb_midi.run_message_probe(
                    midi,
                    destination,
                    SetupClient(connection),
                    evidence.data,
                    evidence.path,
                )
        else:
            qualify_usb_midi.run_message_probe(
                midi, destination, None, evidence.data, evidence.path
            )
    with RecordedConnection(profile.port, evidence) as connection:
        after = qualify_usb_midi.diagnostics(SetupClient(connection))
        evidence.data['after'] = after
        check_health(before, after)


def run_downloads(
    profile: Profile, baseline: Baseline, evidence: Evidence, phase: Phase
) -> None:
    duration = float(getattr(profile, f'{phase.value}_seconds'))
    assets = [None, *baseline.assets]
    with RecordedConnection(profile.port, evidence) as connection:
        client = SetupClient(connection)
        if phase == Phase.serial:
            perform_downloads(
                profile,
                baseline,
                evidence,
                client,
                None,
                0,
                time.monotonic() + duration,
                assets,
            )
        else:
            with midi_connection(profile, evidence) as (midi, destination, _):
                qualify_usb_midi.capture_startup(midi, evidence.data, evidence.path)
                perform_downloads(
                    profile,
                    baseline,
                    evidence,
                    client,
                    midi,
                    destination,
                    time.monotonic() + duration,
                    assets,
                )
        evidence.data['after'] = after = qualify_usb_midi.diagnostics(client)
        check_health(baseline.diagnostics, after)


def perform_downloads(
    profile: Profile,
    baseline: Baseline,
    evidence: Evidence,
    client: SetupClient,
    midi: Midi | None,
    destination: int,
    deadline: float,
    assets: list[Asset | None],
) -> None:
    rounds = 0
    while time.monotonic() < deadline:
        asset = assets[rounds % len(assets)]
        evidence.beat(f'download {asset.name if asset else "bundle"}', force=True)
        evidence.data['completed_downloads'] = rounds
        evidence.save()
        traffic = (
            MidiTraffic(midi, destination, profile.midi_period, evidence)
            if midi
            else None
        )
        if traffic:
            traffic.thread.start()
        try:
            data = fetch_asset(client, asset) if asset else client.download_bundle()
        except (OSError, ValueError) as error:
            evidence.fail(str(error))
            raise
        finally:
            if traffic:
                traffic.finish(profile.settle_seconds)
        expected = asset.sha256 if asset else baseline.bundle_sha256
        if digest(data) != expected:
            raise ValueError('download content changed or failed checksum comparison')
        evidence.data['latest_diagnostics'] = current = qualify_usb_midi.diagnostics(
            client, quiet=True
        )
        evidence.save()
        check_health(baseline.diagnostics, current)
        if rounds % 10 == 0:
            require_inventory(profile)
        # Change real setups only after the exact MIDI stream has drained.
        if rounds % 5 == 4:
            client.select(SETUPS[(rounds // 5 + 1) % len(SETUPS)])
            if midi is not None:
                capture_setup_cleanup(midi, evidence, profile.settle_seconds)
        rounds += 1
        evidence.data['completed_downloads'] = rounds
        evidence.data['last_sha256'] = expected
        evidence.save()

    if not rounds:
        raise ValueError('qualification phase completed without transferring data')


def run_pressure(profile: Profile, baseline: Baseline, evidence: Evidence) -> None:
    with RecordedConnection(profile.port, evidence) as connection:
        client = SetupClient(connection)
        before = qualify_usb_midi.diagnostics(client)
        evidence.data['before'] = before
        evidence.save()
        check_health(baseline.diagnostics, before)
        with midi_connection(profile, evidence) as (midi, destination, peers):
            if not peers:
                raise ValueError('no cntrl USB outputs available for pressure')
            qualify_usb_midi.capture_startup(midi, evidence.data, evidence.path)
            lifecycle = qualify_usb_midi.note_lifecycle()
            held, released = (
                lifecycle[: len(lifecycle) // 2],
                lifecycle[len(lifecycle) // 2 :],
            )
            midi.send(destination, held)
            evidence.capture('midi-tx', held)
            time.sleep(profile.settle_seconds)
            evidence.capture('midi-rx', bytes(midi.data))
            if bytes(midi.data) != held:
                raise ValueError('held notes were not delivered before pressure')
            midi.data.clear()
            packet = b''.join(qualify_usb_midi.short_messages())

            def release(index: int) -> None:
                evidence.beat(f'pressure burst {index}')
                evidence.capture('pressure-tx', packet * len(peers))
                if index == profile.pressure_bursts // 2:
                    evidence.capture('midi-tx', released)
                    midi.send(destination, released)
                    evidence.data['release_burst'] = index
                    evidence.save()

            stress = qualify_usb_midi.stress_traffic(
                midi,
                peers,
                packet,
                profile.pressure_seconds,
                profile.pressure_bursts,
                release,
            )
            evidence.data['stress'] = stress
            received = bytes(midi.data)
            evidence.capture('midi-rx', received)
            evidence.data['after_pressure'] = after = qualify_usb_midi.diagnostics(
                client
            )
            evidence.save()
            if stress['bursts'] != profile.pressure_bursts:
                raise ValueError('pressure window ended before all configured bursts')
            if held_messages(received) != released:
                raise ValueError(
                    'held-note releases/sustain-off were lost under pressure'
                )
            del midi.data[: len(received)]
            check_health(before, after, pressure=True)
            observed = (
                qualify_usb_midi.status_fields(before['heap'])['failures']
                != qualify_usb_midi.status_fields(after['heap'])['failures']
                or before['route_0'].split()[6] != after['route_0'].split()[6]
            )
            evidence.data['pressure_observed'] = observed
            evidence.save()
            if not observed:
                raise ValueError('bounded load did not reach admission pressure')
            # A fresh strict probe must work immediately after the queues recover.
            trials = qualify_usb_midi.probe_short_messages(
                midi, destination, client, evidence.data, evidence.path
            )
            if not all(t['matches'] for t in trials):
                raise ValueError('post-pressure exact MIDI comparison failed')
            recovered = qualify_usb_midi.diagnostics(client)
            check_health(after, recovered)
            evidence.data['after'] = recovered


def restore(profile: Profile, root: Path, evidence: Evidence) -> None:
    baseline = Baseline.model_validate(
        tomlkit.parse((root / 'baseline.toml').read_text()).unwrap()
    )
    with RecordedConnection(profile.port, evidence) as connection:
        client = SetupClient(connection)
        if baseline.selected:
            client.select(baseline.selected)
        client.delete(SETUPS)
    target = read_target(profile.target_export)
    with RecordedConnection(profile.port, evidence) as connection:
        backups.restore_setups(
            root / 'before',
            profile.port,
            target,
            preserve_memory_estimates=True,
            connection=connection,
        )
        backups.verify_after_flashing(
            root / 'before',
            profile.port,
            target,
            preserve_memory_estimates=True,
            connection=connection,
        )


def worker(options: Worker) -> None:
    profile = load_profile(options.profile)
    evidence = Evidence(options.directory, profile.trace_bytes)
    root = options.directory.parent
    try:
        evidence.data['phase'] = options.phase.value
        evidence.data['serial_inventory'] = require_inventory(profile)
        evidence.save()
        if options.phase == Phase.prepare:
            prepare(profile, root, evidence)
        elif options.phase == Phase.restore:
            restore(profile, root, evidence)
        elif options.phase == Phase.upload:
            directory = options.directory / 'export'
            flash_target.run(
                flash_target.Flash(
                    directory=directory,
                    resources=profile.target_export / 'distribution',
                    port=profile.port,
                    serial=profile.serial,
                ),
                connection_factory=lambda port: RecordedConnection(port, evidence),
            )
        else:
            baseline = Baseline.model_validate(
                tomlkit.parse((root / 'baseline.toml').read_text()).unwrap()
            )
            if options.phase == Phase.connections:
                run_probe(profile, baseline, evidence, options.index)
            elif options.phase == Phase.pressure:
                run_pressure(profile, baseline, evidence)
            else:
                run_downloads(profile, baseline, evidence, options.phase)
        evidence.data['serial_inventory_after'] = require_inventory(profile)
        evidence.data['success'] = True
        evidence.save()
    except (
        OSError,
        ValueError,
        KeyError,
        subprocess.CalledProcessError,
        KeyboardInterrupt,
    ) as error:
        evidence.fail(str(error))
        raise
    finally:
        evidence.stream.close()


def capture_failure(pid: int, directory: Path) -> None:
    for name, command in (
        (
            'host-stack.txt',
            ['sample', str(pid), '1', '-file', str(directory / 'sample.txt')],
        ),
        ('usb-enumeration.txt', ['ioreg', '-p', 'IOUSB', '-l', '-w', '0']),
    ):
        with (directory / name).open('w') as output:
            try:
                subprocess.run(
                    command,
                    stdout=output,
                    stderr=subprocess.STDOUT,
                    timeout=10,
                    check=False,
                )
            except (OSError, subprocess.TimeoutExpired) as error:
                output.write(str(error))


def stop_worker(process: subprocess.Popen[str]) -> bool:
    process.terminate()
    try:
        process.wait(5)
    except subprocess.TimeoutExpired:
        process.kill()
        try:
            process.wait(5)
        except subprocess.TimeoutExpired:
            # A kernel-blocked USB close may not respond even to SIGKILL.
            # Keep the supervisor responsive and retain the PID for recovery.
            return False
    return True


def supervise(
    command: list[str],
    directory: Path,
    stall: float,
    *,
    uploading: bool = False,
    capture: Callable[[int, Path], None] = capture_failure,
    maximum_seconds: float | None = None,
) -> bool:
    """A completed report is insufficient until the worker also exits cleanly."""
    heartbeat = directory / 'heartbeat.txt'
    previous = 0
    started = changed = time.monotonic()
    stalled = False
    expired = False
    with (directory / 'worker.log').open('w') as output:
        process = subprocess.Popen(
            command,
            stdout=output,
            stderr=subprocess.STDOUT,
            start_new_session=True,
            text=True,
        )
        try:
            while process.poll() is None:
                stamp = heartbeat.stat().st_mtime_ns if heartbeat.exists() else 0
                if stamp != previous:
                    previous, changed = stamp, time.monotonic()
                expired = (
                    maximum_seconds is not None
                    and time.monotonic() - started > maximum_seconds
                )
                if not stalled and (time.monotonic() - changed > stall or expired):
                    stalled = True
                    capture(process.pid, directory)
                    if not uploading:
                        stop_worker(process)
                        break
                    else:
                        print(
                            'Uploader stage stalled; evidence saved. '
                            'Waiting without interrupting the upload.',
                            flush=True,
                        )
                time.sleep(0.2)
        except KeyboardInterrupt:
            capture(process.pid, directory)
            if uploading:
                print(
                    f'Upload worker {process.pid} remains running; '
                    'no recovery has been attempted.',
                    flush=True,
                )
            else:
                stop_worker(process)
            raise
    path = directory / 'result.toml'
    try:
        passed = (
            path.is_file() and tomlkit.parse(path.read_text()).get('success') is True
        )
    except (OSError, ParseError):
        passed = False
    if process.returncode != 0 or stalled or not passed:
        if not stalled:
            capture(process.pid, directory)
        qualify_usb_midi.save_report(
            directory / 'supervisor.toml',
            {
                'success': False,
                'returncode': process.returncode
                if process.returncode is not None
                else 'running',
                'worker_pid': process.pid,
                'worker_alive': process.poll() is None,
                'stalled': stalled,
                'deadline_exceeded': expired,
                'worker_report_passed': passed,
            },
        )
        return False
    return True


def run_reliability(recipe: Path, report: Path | None, *, execute: bool) -> None:
    if report is None:
        raise ValueError('reliability requires --report')
    recipe, report = recipe.resolve(), report.resolve()
    profile = load_profile(recipe)
    schedule = phase_schedule(profile)
    result: dict[str, object] = {
        'success': False,
        'state': 'planned',
        'profile': profile.model_dump(mode='json'),
        'offered_midi_bytes_per_second': sum(map(len, traffic_pattern()))
        / len(traffic_pattern())
        / profile.midi_period,
        'stages': [
            {'phase': s.value, 'repetitions': sum(p == s for p, _ in schedule)}
            for s in dict.fromkeys(p for p, _ in schedule)
        ],
        'report': str(report),
    }
    if not execute:
        print(tomlkit.dumps(result))
        return
    if sys.platform != 'darwin':
        raise ValueError('live USB reliability qualification currently requires macOS')
    if profile.hub == 'UNRECORDED' or profile.cable == 'UNRECORDED':
        raise ValueError('record the actual hub and cable in the profile before --run')
    if report.exists():
        raise ValueError(
            'use a fresh report path; existing evidence is never overwritten'
        )
    artifacts = build.read_artifacts(profile.target_export)
    if artifacts.target != 'target.toml' or artifacts.image != 'firmware.hex':
        raise ValueError('MC reliability requires firmware.hex and target.toml')
    result['expected_image_sha256'] = digest(
        (profile.target_export / artifacts.image).read_bytes()
    )
    result['target_sha256'] = digest(
        (profile.target_export / artifacts.target).read_bytes()
    )
    if profile.stages == [Phase.upload]:
        upload_helper.validate_firmware(
            read_hex(profile.target_export / artifacts.image)
        )
    result['revision'] = subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], text=True
    ).strip()
    updater = upload_helper.updater_path()
    result['updater_path'] = str(updater)
    result['host'] = platform.platform()
    result['python'] = sys.version
    result['updater_sha256'] = digest(updater.read_bytes())
    directory = report.with_name(report.stem + '-artifacts')
    directory.mkdir(parents=True)
    frozen = directory / 'input-export'
    result['input_sha256'] = freeze_export(profile.target_export, frozen)
    if (
        digest((frozen / artifacts.image).read_bytes())
        != result['expected_image_sha256']
        or digest((frozen / artifacts.target).read_bytes()) != result['target_sha256']
    ):
        raise ValueError('build export changed while preparing qualification')
    if profile.stages == [Phase.upload]:
        prepared = directory / 'prepared-image'
        release.prepare(frozen / 'distribution', prepared)
        if (
            digest((prepared / artifacts.image).read_bytes())
            != result['expected_image_sha256']
        ):
            raise ValueError(
                'distribution resources do not reproduce the selected image'
            )
    profile = profile.model_copy(update={'target_export': frozen})
    saved_recipe = directory / 'profile.toml'
    saved_recipe.write_text(tomlkit.dumps(profile.model_dump(mode='json')))
    completed: list[str] = []
    result['completed'] = completed
    result['state'] = 'running'
    qualify_usb_midi.save_report(report, result)
    for phase, index in schedule:
        name = f'{phase.value}-{index:03}'
        child = directory / name
        child.mkdir()
        result['active_stage'] = name
        qualify_usb_midi.save_report(report, result)
        maintenance = phase in (Phase.prepare, Phase.restore, Phase.upload)
        maximum: float | None = profile.stall_seconds
        if phase == Phase.upload:
            maximum = None
        elif maintenance:
            maximum = profile.maintenance_seconds
        elif phase in (Phase.serial, Phase.mixed, Phase.soak):
            maximum = (
                float(getattr(profile, f'{phase.value}_seconds'))
                + profile.maintenance_seconds
            )
        elif phase == Phase.pressure:
            maximum = profile.pressure_seconds + profile.stall_seconds
        command = [
            sys.executable,
            '-u',
            str(Path(__file__).with_name('usb_reliability_entry.py')),
            '--profile',
            str(saved_recipe),
            '--directory',
            str(child),
            '--phase',
            phase.value,
            '--index',
            str(index),
        ]
        try:
            passed = supervise(
                command,
                child,
                profile.maintenance_seconds if maintenance else profile.stall_seconds,
                uploading=phase == Phase.upload,
                maximum_seconds=maximum,
            )
        except KeyboardInterrupt:
            result['state'] = 'interrupted'
            result['recovery_required'] = phase != Phase.upload
            qualify_usb_midi.save_report(report, result)
            raise
        if not passed:
            result['state'] = 'failed'
            result['recovery_required'] = True
            commands = recovery_commands(
                profile,
                child / 'export' if phase == Phase.upload else directory,
                temporary=phase != Phase.upload,
            )
            result['recovery_commands'] = commands
            qualify_usb_midi.save_report(report, result)
            for command in commands:
                print(shlex.join(command), flush=True)
            raise ValueError(
                f'{name} failed; evidence and backup retained at {directory}. '
                'No next stage or automatic reset.'
            )
        completed.append(name)
        qualify_usb_midi.save_report(report, result)
    result.update(success=True, state='complete')
    qualify_usb_midi.save_report(report, result)


def worker_main() -> None:
    worker(tyro.cli(Worker))


def recovery_commands(
    profile: Profile, directory: Path, *, temporary: bool = True
) -> list[list[str]]:
    before = directory / 'before'
    if (
        not (before / 'backup.toml').is_file()
        or (before / 'backup-pending.toml').exists()
    ):
        return []
    if tomlkit.parse((before / 'backup.toml').read_text()).get('complete') is not True:
        return []
    base = ['cntrl', 'setup']
    port = ['--port', str(profile.port)]
    selected = (before / 'selected.txt').read_text()
    commands = [base + ['set', selected, *port]] if selected else []
    if temporary:
        commands.append(base + ['del', *SETUPS, *port, '--yes'])
    for action in ('restore', 'verify'):
        commands.append(
            base
            + [
                action,
                str(before),
                *port,
                '--target',
                str(profile.target_export / 'target.toml'),
                '--preserve-memory-estimates',
            ]
        )
    return commands


SETUPS = ['usb-reliability-a', 'usb-reliability-b']
