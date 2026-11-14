"""Explicit, macOS-only qualification of the MC USB-to-DIN MIDI path."""

import ctypes
import sys
import time
from difflib import SequenceMatcher
from enum import StrEnum, auto
from pathlib import Path
from typing import Annotated

import tomlkit
import tyro
from pydantic import BaseModel, ConfigDict, Field, model_validator
from reccy.configuration.tyro import unit_spec
from reccy.configuration.units import Seconds
from serial.tools.list_ports import comports

from ....patch_switcher import devices
from ....patch_switcher.core_midi import Midi, exchange
from ....patch_switcher.transport import SerialConnection, SetupClient
from . import transport_trace


class Trial(StrEnum):
    paced = auto()
    burst = auto()
    sysex = auto()
    notes = auto()


class Options(BaseModel, frozen=True):
    """List endpoints by default; --run sends test traffic to the chosen output."""

    run: bool = False
    inspect: bool = False
    """Read serial diagnostics without opening MIDI endpoints or sending MIDI."""
    analyze_report: Path | None = None
    """Compare the saved trial bytes without opening MIDI or serial connections."""
    watchdog_test: bool = False
    """Intentionally hang diagnostic firmware and verify its watchdog warm reset."""
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
    probe_short_messages: bool = False
    """Compare paced/burst messages, SysEx and notes with raw bytes and counters."""
    probe_trial: Trial | None = None
    """Run one probe trial instead of the complete probe sequence."""
    trace_build: Path | None = None
    """Capture transport/UART evidence matching this build export around a probe.

    With --midi-only, serial is closed before MIDI opens and reopened after MIDI
    closes. Requires --run --probe-short-messages and --report.
    """
    midi_only: bool = False
    """Probe without opening serial. Requires already-enabled MIDI forwarding."""
    report: Path | None = None
    """Save serial inspection or before/after run diagnostics as TOML."""

    @model_validator(mode='after')
    def stress_run(self) -> 'Options':
        if self.trace_build is not None and not (
            self.run and self.probe_short_messages and self.report is not None
        ):
            raise ValueError(
                'trace-build requires --run --probe-short-messages --report'
            )
        if self.probe_trial is not None and not self.probe_short_messages:
            raise ValueError('probe-trial requires --probe-short-messages')
        if self.probe_short_messages and (
            not self.run or self.inspect or self.stress_only or self.stress_seconds
        ):
            raise ValueError('probe-short-messages requires --run without other modes')
        if self.midi_only and not (
            self.run and self.probe_short_messages and self.expect_enabled
        ):
            raise ValueError(
                'midi-only requires --run --probe-short-messages --expect-enabled'
            )
        if (
            sum(
                (
                    self.inspect,
                    self.run,
                    self.watchdog_test,
                    self.analyze_report is not None,
                )
            )
            > 1
        ):
            raise ValueError(
                'inspect, run, watchdog-test and analyze-report are separate modes; '
                'select only one'
            )
        if (
            self.analyze_report is not None
            and self.report is not None
            and self.analyze_report.resolve() == self.report.resolve()
        ):
            raise ValueError('analysis output must differ from the captured report')
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


def analyze_report(source: Path) -> dict[str, object]:
    """Align captured bytes; repeated payloads can admit several valid alignments."""
    captured = tomlkit.parse(source.read_text()).unwrap()
    trials: list[dict[str, object]] = []
    for trial in captured['trials']:
        sent = bytes.fromhex(trial['sent_hex'])
        received = bytes.fromhex(trial['received_hex'])
        limit = min(len(sent), len(received))
        prefix = suffix = 0
        while prefix < limit and sent[prefix] == received[prefix]:
            prefix += 1
        while suffix < limit - prefix and sent[-suffix - 1] == received[-suffix - 1]:
            suffix += 1
        sent_end, received_end = len(sent) - suffix, len(received) - suffix
        sent_middle, received_middle = (
            sent[prefix:sent_end],
            received[prefix:received_end],
        )
        trials.append(
            {
                'name': trial['name'],
                'sent_bytes': len(sent),
                'received_bytes': len(received),
                'matches': sent == received,
                'common_prefix_bytes': prefix,
                'common_suffix_bytes': suffix,
                'differences': [
                    {
                        'kind': k,
                        'sent_offset': prefix + a,
                        'received_offset': prefix + c,
                        'sent_hex': sent_middle[a:b].hex(),
                        'received_hex': received_middle[c:d].hex(),
                    }
                    for k, a, b, c, d in SequenceMatcher(
                        None, sent_middle, received_middle, autojunk=False
                    ).get_opcodes()
                    if k != 'equal'
                ],
            }
        )
    result: dict[str, object] = {
        'source': str(source),
        'interpretation': (
            'Byte alignment only; repeated payloads may have ambiguous offsets. '
            'This does not identify the transport stage that changed bytes.'
        ),
        'trials': trials,
    }
    for name in ('transport_comparison', 'transport_error'):
        if name in captured:
            result[name] = captured[name]
    return result


def capture_transport(options: Options) -> None:
    export, path = options.trace_build, options.report
    if export is None or path is None:
        raise ValueError('transport capture requires a trace build and report')
    result: dict[str, object] = {
        'success': False,
        'options': options.model_dump(mode='json', exclude_none=True),
    }
    try:
        expected = transport_trace.expected_build(export)
        before = transport_trace.snapshot(options.port, expected.trace_id, 'RESET')
        if before.din_route.split()[2] != str(int(options.expect_enabled)):
            raise ValueError(
                'bridge enabled state differs from requested qualification'
            )
        first = status_fields(before.device)
        if first.get('starts_status') != 'ready':
            raise ValueError('transport capture requires a ready startup counter')
    except (OSError, ValueError) as error:
        result['error'] = str(error)
        save_report(path, result)
        raise
    result['expected_trace_build'] = expected.model_dump()
    result['transport_before'] = before.model_dump()
    save_report(path, result)
    midi_path = path.with_name(path.stem + '.midi.toml')
    child = options.model_copy(update={'trace_build': None, 'report': midi_path})
    # Seed the child report so an early failure cannot reuse an older capture.
    save_report(
        midi_path, {'success': False, 'error': 'MIDI trial did not produce a report'}
    )
    try:
        run(child)
    finally:
        result.update(tomlkit.parse(midi_path.read_text()).unwrap())
        result['options'] = options.model_dump(mode='json', exclude_none=True)
        result['midi_report'] = str(midi_path)
        try:
            after = transport_trace.snapshot(options.port, expected.trace_id, 'STOP')
            result['transport_after'] = after.model_dump()
            last = status_fields(after.device)
            if last.get('starts_status') != 'ready' or first.get('starts') != last.get(
                'starts'
            ):
                raise ValueError(
                    'pedal rebooted or lost its startup counter '
                    'during transport capture'
                )
            result['transport_comparison'] = transport_trace.compare_transport(
                before, after
            )
            result['transport_captured'] = True
        except (OSError, ValueError) as error:
            result['transport_error'] = str(error)
            result['success'] = False
        save_report(path, result)
    if 'transport_error' in result:
        raise ValueError(str(result['transport_error']))


def diagnostics(client: SetupClient) -> dict[str, str]:
    result: dict[str, str] = {}
    for name, command in (
        ('device', 'CNTRL STATUS'),
        ('heap', 'CNTRL HEAP'),
        ('streams', 'CNTRL MIDI STREAMS'),
        ('looper', 'CNTRL MIDI LOOPER'),
        ('qualification', 'CNTRL QUAL STATUS'),
        ('transport', 'CNTRL MIDI TRACE STATUS'),
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


def status_fields(text: str) -> dict[str, str]:
    return dict(s.split('=', 1) for s in text.split() if '=' in s)


def watchdog_evidence(before: dict[str, str], after: dict[str, str]) -> dict[str, int]:
    previous = status_fields(before['device'])
    current = status_fields(after['device'])
    qualification = status_fields(after['qualification'])
    if (
        previous.get('starts_status') != 'ready'
        or current.get('starts_status') != 'ready'
    ):
        raise ValueError('Watchdog verification requires ready startup counters')
    first, last = int(previous['starts']), int(current['starts'])
    reset = int(qualification.get('reset', '0'), 16)
    phase = int(qualification.get('b1', '0'), 16)
    if last != first + 1:
        raise ValueError(f'Watchdog reset expected starts={first + 1}, got {last}')
    if not reset & (1 << 4):
        raise ValueError('Reboot did not retain the SRC watchdog reset bit')
    if not int(qualification.get('breadcrumbs', '0'), 16) & 1 or phase != 21:
        raise ValueError('Reboot did not retain the watchdog-test breadcrumb')
    if qualification.get('fault') != '0' or int(qualification.get('pc', '0'), 16):
        raise ValueError('Watchdog self-test reboot retained an unexpected fault')
    return {
        'starts_before': first,
        'starts_after': last,
        'reset': reset,
        'phase': phase,
    }


def watchdog_test(options: Options) -> None:
    report: dict[str, object] = {
        'success': False,
        'options': options.model_dump(mode='json', exclude_none=True),
    }
    try:
        with SerialConnection(options.port, timeout=3, retry=False) as connection:
            client = SetupClient(connection)
            before = diagnostics(client)
            report['before'] = before
            if 'enabled=1' not in before['qualification'].split():
                raise ValueError('Watchdog self-test requires diagnostic firmware')
            initial = status_fields(before['device'])
            if initial.get('starts_status') != 'ready':
                raise ValueError('Watchdog self-test requires a ready startup counter')
            starts = int(initial['starts'])
            save_report(options.report, report)
            client.ok('CNTRL QUAL START')
            try:
                client.ok('CNTRL QUAL HANG')
            except (OSError, ValueError):
                if connection.serial is not None:
                    try:
                        client.ok('CNTRL QUAL STOP')
                    except (OSError, ValueError) as error:
                        print(f'Qualification STOP failed: {error}', file=sys.stderr)
                raise
            report['hang_acknowledged'] = True
            save_report(options.report, report)
            # The target deliberately stops processing commands after this ACK.
            # Release without an acknowledged context-close exchange.
            connection.release()
            connection.close()
        save_report(options.report, report)
        deadline = time.monotonic() + 30
        time.sleep(8.5)
        last_error = 'USB serial endpoint has not returned'
        while time.monotonic() < deadline:
            after = None
            if any(d.port == options.port for d in devices.connected()):
                try:
                    with SerialConnection(
                        options.port, timeout=3, retry=False
                    ) as connection:
                        client = SetupClient(connection)
                        current = status_fields(client.request('CNTRL STATUS'))
                        if (
                            current.get('starts_status') == 'ready'
                            and int(current['starts']) != starts
                        ):
                            after = diagnostics(client)
                except (OSError, ValueError) as error:
                    last_error = str(error)
            if after is not None:
                report['after'] = after
                report['evidence'] = watchdog_evidence(before, after)
                report['success'] = True
                save_report(options.report, report)
                print('Watchdog warm reset verified', flush=True)
                return
            time.sleep(0.25)
        raise TimeoutError(f'Watchdog warm reset was not observed: {last_error}')
    except (OSError, ValueError) as error:
        report['error'] = str(error)
        save_report(options.report, report)
        raise


def probe_virtual_source(midi: Midi) -> dict[str, object]:
    """Check first-delivery framing without USB, firmware or the DIN cable."""
    midi.core.MIDISourceCreate.argtypes = [
        ctypes.c_uint32,
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_uint32),
    ]
    midi.core.MIDIReceived.argtypes = [ctypes.c_uint32, ctypes.c_void_p]
    endpoint = ctypes.c_uint32()
    name = midi.string('cntrl qualification host loopback')
    try:
        midi.check(
            midi.core.MIDISourceCreate(midi.client, name, ctypes.byref(endpoint))
        )
    finally:
        midi.cf.CFRelease(name)
    try:
        midi.check(midi.core.MIDIPortConnectSource(midi.input, endpoint, None))
        time.sleep(0.1)
        idle = bytes(midi.data)
        midi.data.clear()
        messages = b'\xc0\x01\xc1\x02'
        buffer = ctypes.create_string_buffer(64)
        payload = ctypes.create_string_buffer(messages)
        packet = midi.core.MIDIPacketListInit(buffer)
        if not midi.core.MIDIPacketListAdd(
            buffer, len(buffer), packet, 0, len(messages), payload
        ):
            raise ValueError('Host loopback packet allocation failed')
        midi.check(midi.core.MIDIReceived(endpoint, buffer))
        time.sleep(0.1)
        received = bytes(midi.data)
        return {
            'sent_hex': messages.hex(),
            'received_hex': received.hex(),
            'idle_hex': idle.hex(),
            'matches': received == messages,
        }
    finally:
        midi.core.MIDIPortDisconnectSource(midi.input, endpoint)
        midi.core.MIDIEndpointDispose(endpoint)
        midi.data.clear()


def probe_short_messages(
    midi: Midi,
    destination: int,
    client: SetupClient | None,
    report: dict[str, object],
    path: Path | None,
    selected: Trial | None = None,
) -> list[dict[str, object]]:
    """Retain unsolicited traffic separately from each controlled wire trial."""
    messages = [bytes((0xC0 | i % 16, i)) for i in range(128)]
    messages.extend(bytes((0xB0 | i, 1, 0)) for i in range(16))
    sysex = b'\xf0\x7d' + bytes(i % 128 for i in range(4096)) + b'\xf7'
    notes = note_lifecycle()
    trials: list[dict[str, object]] = []
    report['trials'] = trials
    for name, interval, chunks, settle in [
        ('paced', 0.002, messages, 0.5),
        ('burst', 0, [b''.join(messages)], 0.5),
        ('sysex', 0, [sysex], 2),
        ('notes', 0.002, [notes[i : i + 3] for i in range(0, len(notes), 3)], 0.2),
    ]:
        if selected is not None and name != selected:
            continue
        payload = b''.join(chunks)
        before = diagnostics(client) if client is not None else None
        time.sleep(0.2)
        idle = bytes(midi.data)
        midi.data.clear()
        for message in chunks:
            midi.send(destination, message)
            if interval:
                time.sleep(interval)
        time.sleep(settle)
        received = bytes(midi.data)
        trial: dict[str, object] = {
            'name': name,
            'sent_hex': payload.hex(),
            'received_hex': received.hex(),
            'idle_hex': idle.hex(),
            'matches': received == payload,
        }
        if before is not None:
            trial['before'] = before
        trials.append(trial)
        save_report(path, report)
        if client is not None:
            trial['after'] = diagnostics(client)
            save_report(path, report)
        print(
            f'{name}: sent {len(payload)}, received {len(received)}, '
            f'matches={trial["matches"]}',
            flush=True,
        )
        if client is None and not trial['matches']:
            break
        midi.data.clear()
    return trials


def run_message_probe(
    midi: Midi,
    destination: int,
    client: SetupClient | None,
    report: dict[str, object],
    path: Path | None,
    selected: Trial | None = None,
) -> None:
    report['host_loopback'] = host = probe_virtual_source(midi)
    if client is None and not host['matches']:
        save_report(path, report)
        raise ValueError('Host loopback mismatch; see the captured probe bytes')
    report['trials'] = trials = probe_short_messages(
        midi, destination, client, report, path, selected
    )
    report['success'] = host['matches'] and all(t['matches'] for t in trials)
    save_report(path, report)
    if not report['success']:
        raise ValueError('Probe mismatch; see trial bytes and available counters')


def run(options: Options) -> None:
    if options.trace_build is not None:
        if sys.platform != 'darwin':
            sys.exit('This qualification uses macOS CoreMIDI.')
        capture_transport(options)
        return
    if options.analyze_report is not None:
        analysis = analyze_report(options.analyze_report)
        save_report(options.report, analysis)
        print(tomlkit.dumps(analysis), end='')
        return
    if options.watchdog_test:
        watchdog_test(options)
        return
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
            ports = [
                {
                    k: v
                    for k, v in {
                        'port': p.device,
                        'product': p.product,
                        'manufacturer': p.manufacturer,
                        'serial': p.serial_number,
                        'vid': p.vid,
                        'pid': p.pid,
                        'location': p.location,
                        'interface': p.interface,
                    }.items()
                    if v is not None
                }
                for p in comports()
            ]
            for p in ports:
                print(f'Serial: {p}')
            save_report(
                options.report,
                {
                    'mode': 'endpoints',
                    'midi_sources': list(sources),
                    'midi_destinations': list(destinations),
                    'serial_ports': ports,
                },
            )
            return
        destination, source = destinations[options.destination], sources[options.source]
        midi.check(midi.core.MIDIPortConnectSource(midi.input, source, None))
        report = {
            'success': False,
            'options': options.model_dump(mode='json', exclude_none=True),
            'timing': (
                'CoreMIDI receiver bytes only; no wire timestamps '
                'or clock jitter measurement'
            ),
        }
        save_report(options.report, report)
        if options.midi_only:
            run_message_probe(
                midi, destination, None, report, options.report, options.probe_trial
            )
            return
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
            report['before'] = before_diagnostics
            save_report(options.report, report)
            armed = False
            try:
                if 'enabled=1' in before_diagnostics['qualification'].split():
                    armed = True
                    client.ok('CNTRL QUAL START')
                if options.probe_short_messages:
                    run_message_probe(
                        midi,
                        destination,
                        client,
                        report,
                        options.report,
                        options.probe_trial,
                    )
                    return
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
