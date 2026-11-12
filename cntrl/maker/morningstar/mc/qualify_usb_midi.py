"""Explicit, macOS-only qualification of the MC USB-to-DIN MIDI path."""

import sys
import time
from pathlib import Path
from typing import Annotated

import tomlkit
import tyro
from pydantic import BaseModel, ConfigDict, Field, model_validator
from reccy.configuration.tyro import unit_spec
from reccy.configuration.units import Seconds

from ....patch_switcher.core_midi import Midi, exchange
from ....patch_switcher.transport import SerialConnection, SetupClient


class Options(BaseModel, frozen=True):
    """List endpoints by default; --run sends test traffic to the chosen output."""

    run: bool = False
    inspect: bool = False
    """Read serial diagnostics without opening MIDI endpoints or sending MIDI."""
    destination: str = ''
    source: str = ''
    port: Path = Path('/dev/cu.usbmodem144682801')
    expect_enabled: bool = False
    stress_seconds: Annotated[Seconds, unit_spec(Seconds, 'DURATION')] = 0
    stress_bursts: Annotated[int, Field(strict=True, gt=0)] | None = None
    """For recovery checks, cap sends then observe until the stress duration ends.

    Uncapped runs continuously load the CPU and can leave an OS/USB backlog.
    """
    stress_only: bool = False
    """Skip exact byte checks for transforming graphs; send only PC/modulation load."""
    report: Path | None = None
    """Save serial inspection or before/after run diagnostics as TOML."""

    @model_validator(mode='after')
    def stress_run(self) -> 'Options':
        if self.inspect and self.run:
            raise ValueError('inspect and run are separate modes; select only one')
        if self.stress_only and (not self.run or self.stress_seconds <= 0):
            raise ValueError('stress-only requires --run and positive --stress-seconds')
        if self.stress_bursts is not None and (
            not self.run or self.stress_seconds <= 0
        ):
            raise ValueError(
                'stress-bursts requires --run and positive --stress-seconds'
            )
        return self

    model_config = ConfigDict(extra='forbid')


def note_lifecycle() -> bytes:
    """Hold and release 32 distinct notes on each of two sustained channels."""
    sustain = b''.join(bytes((0xB0 | c, 64, 127)) for c in range(2))
    attacks = b''.join(
        bytes((0x90 | c, n, 100)) for n in range(48, 80) for c in range(2)
    )
    releases = b''.join(
        bytes((0x80 | c, n, 23)) for n in range(48, 80) for c in range(2)
    )
    return (
        sustain
        + attacks
        + releases
        + b''.join(bytes((0xB0 | c, 64, 0)) for c in range(2))
    )


def diagnostics(client: SetupClient) -> dict[str, str]:
    result: dict[str, str] = {}
    for name, command in (
        ('device', 'CNTRL STATUS'),
        ('heap', 'CNTRL HEAP'),
        ('streams', 'CNTRL MIDI STREAMS'),
        ('looper', 'CNTRL MIDI LOOPER'),
        ('qualification', 'CNTRL QUAL STATUS'),
    ):
        try:
            result[name] = client.request(command)
        except ValueError as error:
            if str(error) not in ('CNTRL ERROR invalid-command', 'CNTRL ERROR busy'):
                raise
            result[name] = str(error)
        print(f'{name}: {result[name]}', flush=True)
    route = 0
    while True:
        try:
            result[f'route_{route}'] = client.request(f'CNTRL MIDI STATUS {route}')
        except ValueError as error:
            if str(error) != 'CNTRL ERROR midi-route':
                raise
            break
        print(result[f'route_{route}'], flush=True)
        route += 1
    return result


def save_report(path: Path | None, result: dict[str, object]) -> None:
    if path is not None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(tomlkit.dumps(result))


def run(options: Options) -> None:
    if options.inspect:
        inspection: dict[str, object] = {
            'success': False,
            'options': options.model_dump(mode='json', exclude_none=True),
        }
        try:
            with SerialConnection(options.port, timeout=3, retry=False) as connection:
                inspection['diagnostics'] = diagnostics(SetupClient(connection))
        except (OSError, ValueError) as error:
            inspection['error'] = str(error)
            save_report(options.report, inspection)
            raise
        inspection['success'] = True
        save_report(options.report, inspection)
        return
    if sys.platform != 'darwin':
        sys.exit('This qualification uses macOS CoreMIDI.')
    midi = Midi()
    report: dict[str, object] | None = None
    try:
        sources, destinations = midi.endpoints(True), midi.endpoints(False)
        for n in sources:
            print(f'Input: {n}')
        for n in destinations:
            print(f'Output: {n}')
        if not options.run:
            return
        destination, source = destinations[options.destination], sources[options.source]
        midi.check(midi.core.MIDIPortConnectSource(midi.input, source, None))
        with SerialConnection(options.port, timeout=3, retry=False) as connection:
            client = SetupClient(connection)
            before = client.request('CNTRL MIDI STATUS 0')
            print(before, flush=True)
            if before.split()[2] != str(int(options.expect_enabled)):
                raise ValueError(
                    'Bridge enabled state differs from requested qualification'
                )
            client.ok('CNTRL MIDI RESET')
            time.sleep(0.1)
            before_diagnostics = diagnostics(client)
            report = {
                'success': False,
                'options': options.model_dump(mode='json', exclude_none=True),
                'before': before_diagnostics,
                'timing': (
                    'CoreMIDI receiver bytes only; no wire timestamps '
                    'or clock jitter measurement'
                ),
            }
            save_report(options.report, report)
            armed = False
            try:
                if 'enabled=1' in before_diagnostics['qualification'].split():
                    armed = True
                    client.ok('CNTRL QUAL START')
                # Program changes and benign modulation controls, not note events.
                messages = b''.join(bytes((0xC0 | i % 16, i)) for i in range(128))
                messages += b''.join(bytes((0xB0 | i, 1, 0)) for i in range(16))
                if options.stress_only:
                    report['byte_checks'] = 'skipped'
                    print(
                        'Exact byte and note-release checks skipped '
                        'for stress-only mode',
                        flush=True,
                    )
                else:
                    received = exchange(
                        midi,
                        destination,
                        messages,
                        messages if options.expect_enabled else b'',
                        chunk_bytes=len(messages),
                        interval_seconds=0,
                        settle_seconds=0.5,
                        label='Short messages',
                    )
                    print(f'Short messages: {len(received)} bytes verified', flush=True)
                    verified = {
                        'short_sent_bytes': len(messages),
                        'short_received_bytes': len(received),
                    }
                    report['verified'] = verified
                    notes = note_lifecycle()
                    received = exchange(
                        midi,
                        destination,
                        notes,
                        notes if options.expect_enabled else b'',
                        chunk_bytes=3,
                        interval_seconds=0.002,
                        settle_seconds=0.2,
                        label='Notes and sustain',
                    )
                    print(
                        f'Notes and sustain: {len(received)} bytes verified', flush=True
                    )
                    verified.update(
                        notes_sent_bytes=len(notes),
                        notes_received_bytes=len(received),
                        note_onsets_sent=64,
                        note_releases_sent=64,
                        sustain_messages_sent=4,
                    )
                    save_report(options.report, report)
                    # Submit one complete non-commercial SysEx frame to CoreMIDI.
                    # Allow its 4,099 bytes to drain over DIN at 3,125 bytes/second.
                    sysex = b'\xf0\x7d' + bytes(i % 128 for i in range(4096)) + b'\xf7'
                    client.ok('CNTRL MIDI RESET')
                    try:
                        received = exchange(
                            midi,
                            destination,
                            sysex,
                            sysex if options.expect_enabled else b'',
                            chunk_bytes=len(sysex),
                            interval_seconds=0,
                            settle_seconds=2,
                            label='SysEx',
                        )
                    finally:
                        if connection.serial is not None:
                            try:
                                report['sysex_timing'] = client.request(
                                    'CNTRL MIDI STATUS 0'
                                )
                            except (OSError, ValueError) as error:
                                report['sysex_timing_error'] = str(error)
                        else:
                            report['sysex_timing_error'] = 'USB connection is closed'
                    print(f'Streamed SysEx: {len(received)} bytes verified', flush=True)
                    verified.update(
                        sysex_sent_bytes=len(sysex), sysex_received_bytes=len(received)
                    )
                    report['byte_checks'] = 'verified'
                if options.stress_seconds:
                    peers = [
                        v for k, v in destinations.items() if 'cntrl experimental' in k
                    ]
                    if not peers:
                        raise ValueError(
                            'No cntrl MIDI outputs found for the stress run'
                        )
                    report['before_stress'] = diagnostics(client)
                    save_report(options.report, report)
                    client.ok('CNTRL MIDI RESET')
                    midi.data.clear()
                    begin = time.monotonic()
                    deadline = begin + options.stress_seconds
                    sending_finished = begin
                    calls = 0
                    while time.monotonic() < deadline:
                        if (
                            options.stress_bursts is None
                            or calls < options.stress_bursts
                        ):
                            for p in peers:
                                midi.send(p, messages)
                            calls += 1
                            sending_finished = time.monotonic()
                        time.sleep(0.001)
                    time.sleep(0.5)
                    finished = time.monotonic()
                    print(
                        f'Overload: {calls} bursts to {len(peers)} outputs', flush=True
                    )
                    report['stress'] = {
                        'bursts': calls,
                        'outputs': len(peers),
                        'sent_bytes': calls * len(peers) * len(messages),
                        'received_bytes': len(midi.data),
                        'sending_seconds': sending_finished - begin,
                        'observation_seconds': finished - sending_finished,
                        'elapsed_seconds': finished - begin,
                    }
                    report['stress_timing'] = client.request('CNTRL MIDI STATUS 0')
            finally:
                if armed and connection.serial is not None:
                    try:
                        client.ok('CNTRL QUAL STOP')
                    except (OSError, ValueError) as error:
                        print(
                            f'Qualification stop failed: {error}',
                            file=sys.stderr,
                            flush=True,
                        )
            report['after'] = diagnostics(client)
            report['success'] = True
            save_report(options.report, report)
    except (OSError, ValueError) as error:
        if options.report is not None:
            if report is None:
                report = {'options': options.model_dump(mode='json', exclude_none=True)}
            report.update(
                success=False,
                error=str(error),
                received_hex=bytes(midi.data).hex(),
            )
            save_report(options.report, report)
        raise
    finally:
        midi.close()


def main() -> None:
    run(tyro.cli(Options))


if __name__ == '__main__':
    main()
