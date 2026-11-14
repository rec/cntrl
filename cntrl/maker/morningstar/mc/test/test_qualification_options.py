from pathlib import Path

import pytest
import tomlkit
import tyro
from pytest_regressions.file_regression import FileRegressionFixture

from cntrl.maker.morningstar.mc import qualify_usb_midi
from cntrl.maker.morningstar.mc.qualify_usb_midi import (
    Options,
    Trial,
    note_lifecycle,
    save_report,
    watchdog_evidence,
)
from cntrl.patch_switcher.core_midi import Midi


@pytest.mark.parametrize('value', ['0.5', '500 ms'])
def test_stress_duration_accepts_units_without_running_hardware(value: str) -> None:
    options = tyro.cli(Options, args=['--stress-seconds', value])
    assert options.stress_seconds == 0.5


def test_stress_only_requires_an_explicit_positive_duration_run() -> None:
    options = Options(run=True, stress_only=True, stress_seconds=0.5)
    assert options.stress_only and options.stress_seconds == 0.5
    for values in (
        {'stress_only': True, 'stress_seconds': 0.5},
        {'run': True, 'stress_only': True},
        {'run': True, 'stress_only': True, 'stress_seconds': -1},
    ):
        with pytest.raises(ValueError, match='stress-only requires'):
            Options.model_validate(values)


def test_inspection_is_a_separate_mode_without_midi_run_options() -> None:
    options = tyro.cli(Options, args=['--inspect'])
    assert options.inspect and not options.run
    assert not options.destination and not options.source
    with pytest.raises(ValueError, match='select only one'):
        Options(inspect=True, run=True)


def test_watchdog_selftest_is_explicit_and_cannot_run_midi_traffic() -> None:
    options = tyro.cli(Options, args=['--watchdog-test'])
    assert options.watchdog_test and not options.inspect and not options.run
    for values in ({'run': True}, {'inspect': True}):
        with pytest.raises(ValueError, match='select only one'):
            Options.model_validate({'watchdog_test': True, **values})


def test_watchdog_proof_requires_the_next_boot_reset_cause_and_retained_phase() -> None:
    before = {'device': 'CNTRL READY 40 starts=147 starts_status=ready'}
    after = {
        'device': 'CNTRL READY 40 starts=148 starts_status=ready',
        'qualification': (
            'CNTRL QUAL STATUS enabled=1 reset=10 breadcrumbs=3f b1=15 fault=0 pc=0'
        ),
    }
    assert watchdog_evidence(before, after) == {
        'starts_before': 147,
        'starts_after': 148,
        'reset': 16,
        'phase': 21,
    }
    for field, wrong in (
        ('device', 'CNTRL READY 40 starts=147 starts_status=ready'),
        ('qualification', after['qualification'].replace('reset=10', 'reset=1')),
        (
            'qualification',
            after['qualification'].replace('breadcrumbs=3f', 'breadcrumbs=0'),
        ),
        ('qualification', after['qualification'].replace('b1=15', 'b1=1')),
        (
            'qualification',
            after['qualification'].replace('fault=0 pc=0', 'fault=1 pc=1234'),
        ),
    ):
        with pytest.raises(ValueError):
            watchdog_evidence(before, after | {field: wrong})


def test_burst_cap_requires_a_positive_count_and_a_timed_run() -> None:
    options = tyro.cli(
        Options,
        args=['--run', '--stress-seconds', '30 seconds', '--stress-bursts', '1'],
    )
    assert options.stress_bursts == 1 and options.stress_seconds == 30
    assert Options(run=True, stress_seconds=30).stress_bursts is None
    for values in (
        {'stress_bursts': 1, 'stress_seconds': 30},
        {'run': True, 'stress_bursts': 1},
        {'run': True, 'stress_bursts': 0, 'stress_seconds': 30},
        {'run': True, 'stress_bursts': -1, 'stress_seconds': 30},
    ):
        with pytest.raises(ValueError):
            Options.model_validate(values)


def test_short_message_probe_requires_an_explicit_separate_run() -> None:
    assert Options(run=True, probe_short_messages=True).probe_short_messages
    for values in (
        {'probe_short_messages': True},
        {'run': True, 'probe_short_messages': True, 'inspect': True},
        {'run': True, 'probe_short_messages': True, 'stress_seconds': 1},
        {'run': True, 'probe_short_messages': True, 'stress_only': True},
    ):
        with pytest.raises(ValueError, match='probe-short-messages requires'):
            Options.model_validate(values)


def test_midi_only_probe_requires_enabled_forwarding_and_explicit_run() -> None:
    options = tyro.cli(
        Options,
        args=['--run', '--probe-short-messages', '--expect-enabled', '--midi-only'],
    )
    assert options.midi_only
    for values in (
        {},
        {'run': True},
        {'run': True, 'probe_short_messages': True},
        {'run': True, 'expect_enabled': True},
    ):
        with pytest.raises(ValueError, match='midi-only requires'):
            Options.model_validate({'midi_only': True, **values})


def test_single_trial_requires_the_explicit_probe_mode() -> None:
    options = tyro.cli(
        Options, args=['--run', '--probe-short-messages', '--probe-trial', 'notes']
    )
    assert options.probe_trial == Trial.notes
    with pytest.raises(ValueError, match='probe-trial requires'):
        Options(run=True, probe_trial=Trial.notes)


@pytest.mark.parametrize(
    'host_matches, drop, selected',
    [
        (True, False, None),
        (True, True, None),
        (False, False, None),
        (True, False, Trial.sysex),
        (True, False, Trial.notes),
        (True, True, Trial.notes),
    ],
)
def test_probe_without_serial_retains_bytes_and_stops_after_a_mismatch(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    host_matches: bool,
    drop: bool,
    selected: Trial | None,
) -> None:
    class Loopback(Midi):
        def __init__(self) -> None:
            self.data = bytearray()
            self.sent: list[bytes] = []

        def send(self, endpoint: int, data: bytes) -> None:
            self.sent.append(data)
            if not drop:
                self.data.extend(data)

    monkeypatch.setattr(qualify_usb_midi.time, 'sleep', lambda seconds: None)
    monkeypatch.setattr(
        qualify_usb_midi, 'probe_virtual_source', lambda midi: {'matches': host_matches}
    )
    midi = Loopback()
    report: dict[str, object] = {'success': False}
    path = tmp_path / 'midi-only.toml'
    if host_matches and not drop:
        qualify_usb_midi.run_message_probe(midi, 1, None, report, path, selected)
        assert report['success'] is True
    else:
        with pytest.raises(ValueError, match='mismatch'):
            qualify_usb_midi.run_message_probe(midi, 1, None, report, path, selected)
    saved = tomlkit.parse(path.read_text()).unwrap()
    if not host_matches:
        assert not midi.sent and not saved['success']
        assert saved['host_loopback']['matches'] is False
        return
    trials = saved['trials']
    expected = (
        [selected.value]
        if selected is not None
        else ['paced']
        if drop
        else ['paced', 'burst', 'sysex', 'notes']
    )
    assert [t['name'] for t in trials] == expected
    assert all(t['matches'] == (not drop) for t in trials)
    assert all('before' not in t and 'after' not in t for t in trials)
    if drop:
        assert b''.join(midi.sent).hex() == trials[0]['sent_hex']
        assert trials[0]['received_hex'] == ''


def test_report_option_preserves_diagnostics_as_toml(tmp_path: Path) -> None:
    path = tmp_path / 'run' / 'report.toml'
    options = tyro.cli(Options, args=['--report', str(path)])
    assert options.report == path and not options.run
    report: dict[str, object] = {
        'success': True,
        'before': {'heap': 'CNTRL ERROR invalid-command'},
        'after': {'heap': 'CNTRL HEAP actual=256 checked=128'},
        'stress': {'bursts': 32, 'elapsed_seconds': 0.5},
    }
    save_report(options.report, report)
    assert tomlkit.parse(path.read_text()).unwrap() == report


def test_failed_report_preserves_the_exact_observed_byte_trace(tmp_path: Path) -> None:
    # A shortened trace is evidence of missing bytes, not proof that a suffix
    # was lost. Preserve its actual contents without reconstructing positions.
    received = b'\x90\x3c\x64\x80\x40\x17'
    report: dict[str, object] = {
        'success': False,
        'error': 'Notes and sustain mismatch: expected 396, got 6 bytes',
        'received_hex': received.hex(),
    }
    path = tmp_path / 'failed.toml'
    save_report(path, report)
    saved = tomlkit.parse(path.read_text()).unwrap()
    assert saved == report
    assert bytes.fromhex(saved['received_hex']) == received


@pytest.mark.parametrize('change', ['identical', 'extra-prefix', 'missing-middle'])
def test_capture_analysis_preserves_extra_and_missing_bytes(
    tmp_path: Path, file_regression: FileRegressionFixture, change: str
) -> None:
    sent = b''.join(bytes((0xC0 | i, i)) for i in range(10))
    received = sent
    if change == 'extra-prefix':
        received = b'\xf0\xf7' + sent
    elif change == 'missing-middle':
        received = sent[:5] + sent[12:]
    source = tmp_path / 'capture.toml'
    save_report(
        source,
        {
            'trials': [
                {
                    'name': 'short',
                    'sent_hex': sent.hex(),
                    'received_hex': received.hex(),
                }
            ]
        },
    )
    original = source.read_bytes()
    result = qualify_usb_midi.analyze_report(source)
    assert source.read_bytes() == original
    assert result.pop('source') == str(source)
    file_regression.check(tomlkit.dumps(result), extension='.toml')


def test_saved_analysis_runs_without_opening_midi_or_serial(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    def unexpected_device(*args: object, **kwargs: object) -> None:
        pytest.fail('saved analysis opened a device')

    monkeypatch.setattr(qualify_usb_midi, 'Midi', unexpected_device)
    monkeypatch.setattr(qualify_usb_midi, 'SerialConnection', unexpected_device)
    source, output = tmp_path / 'capture.toml', tmp_path / 'analysis.toml'
    save_report(
        source,
        {'trials': [{'name': 'short', 'sent_hex': 'c001', 'received_hex': 'c001'}]},
    )
    options = tyro.cli(
        Options, args=['--analyze-report', str(source), '--report', str(output)]
    )
    qualify_usb_midi.run(options)
    saved = tomlkit.parse(output.read_text()).unwrap()
    assert saved['trials'][0]['matches'] is True


def test_capture_analysis_cannot_run_hardware_or_overwrite_its_source() -> None:
    source = Path('capture.toml')
    for values in ({'run': True}, {'inspect': True}, {'watchdog_test': True}):
        with pytest.raises(ValueError, match='select only one'):
            Options.model_validate({'analyze_report': source, **values})
    with pytest.raises(ValueError, match='must differ'):
        Options(analyze_report=source, report=source)


def test_note_trace_releases_every_channel_note_before_releasing_sustain() -> None:
    active: set[tuple[int, int]] = set()
    sustained: set[int] = set()
    notes: dict[int, set[int]] = {0: set(), 1: set()}
    trace = note_lifecycle()
    assert len(trace) % 3 == 0
    for offset in range(0, len(trace), 3):
        status, key, value = trace[offset : offset + 3]
        channel, kind = status & 15, status >> 4
        assert channel in notes
        if kind == 9:
            assert value > 0 and channel in sustained and (channel, key) not in active
            active.add((channel, key))
            notes[channel].add(key)
        elif kind == 8:
            assert (channel, key) in active
            active.remove((channel, key))
        else:
            assert kind == 11 and key == 64
            if value:
                assert channel not in sustained
                sustained.add(channel)
            else:
                assert not active
                sustained.remove(channel)
    assert not active and not sustained
    assert all(len(n) == 32 for n in notes.values())
