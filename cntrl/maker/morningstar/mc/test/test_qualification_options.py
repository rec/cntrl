from pathlib import Path

import pytest
import tomlkit
import tyro

from cntrl.maker.morningstar.mc.qualify_usb_midi import (
    Options,
    note_lifecycle,
    save_report,
)


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
