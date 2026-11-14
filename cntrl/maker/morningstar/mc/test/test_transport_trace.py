import hashlib
from pathlib import Path

import pytest
import tomlkit
from pytest_regressions.file_regression import FileRegressionFixture

from cntrl.maker.morningstar.mc import qualify_usb_midi, transport_trace


def snapshot(
    drops: int, *, active: int = 0, starts: int = 90
) -> transport_trace.TraceSnapshot:
    trace = transport_trace.parse_trace(TRACE)
    trace['din_drops'] = drops
    trace['active'] = active
    return transport_trace.TraceSnapshot(
        device=f'CNTRL READY 41 starts={starts} starts_status=ready',
        trace=trace,
        din_route='CNTRL MIDI 1 0 100 100 1 0',
        usb_route='CNTRL MIDI 1 6 99 99 0 0',
    )


def test_transport_comparison_separates_validation_and_uart_losses(
    file_regression: FileRegressionFixture,
) -> None:
    before = snapshot(0xFFFFFFFD, active=1)
    after = snapshot(4)
    before.trace['din_overruns'] = 12
    result = transport_trace.compare_transport(before, after)
    file_regression.check(tomlkit.dumps(result), extension='.toml')


def test_trace_manifest_rejects_an_image_that_changed(tmp_path: Path) -> None:
    image = b':00000001FF\n'
    (tmp_path / 'firmware.hex').write_bytes(image)
    (tmp_path / 'target.toml').write_text('name = "control"\n')
    (tmp_path / 'build.toml').write_text(
        tomlkit.dumps(
            {
                'image': 'firmware.hex',
                'target': 'target.toml',
                'trace_id': '0123456789abcdef',
                'image_sha256': hashlib.sha256(image).hexdigest(),
            }
        )
    )
    assert transport_trace.expected_build(tmp_path).trace_id == '0123456789abcdef'
    (tmp_path / 'firmware.hex').write_bytes(image + b'changed')
    with pytest.raises(ValueError, match='image differs'):
        transport_trace.expected_build(tmp_path)


def test_incomplete_trace_cannot_be_used_as_zero_loss_evidence() -> None:
    with pytest.raises(ValueError, match='incomplete MIDI transport trace'):
        transport_trace.parse_trace('CNTRL MIDI TRACE version=1 build=0123456789abcdef')


@pytest.mark.parametrize('failure', ['none', 'midi', 'serial', 'reboot'])
def test_transport_capture_keeps_raw_evidence_and_closes_each_phase(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    failure: str,
) -> None:
    events: list[str] = []
    expected = transport_trace.TraceBuild(
        trace_id='0123456789abcdef', image_sha256='0' * 64
    )
    monkeypatch.setattr(transport_trace, 'expected_build', lambda export: expected)

    def read_snapshot(
        port: Path, identity: str, command: str
    ) -> transport_trace.TraceSnapshot:
        assert identity == expected.trace_id
        events.append(command)
        if command == 'STOP' and failure == 'serial':
            raise OSError('USB disappeared')
        return snapshot(
            7 if command == 'STOP' else 0,
            active=int(command == 'RESET'),
            starts=91 if command == 'STOP' and failure == 'reboot' else 90,
        )

    def midi_run(options: qualify_usb_midi.Options) -> None:
        assert events == ['RESET']
        assert options.trace_build is None and options.midi_only
        events.append('MIDI')
        received = 'f07df7' if failure == 'midi' else 'f07d01f7'
        qualify_usb_midi.save_report(
            options.report,
            {
                'success': failure != 'midi',
                'trials': [
                    {'name': 'sysex', 'sent_hex': 'f07d01f7', 'received_hex': received}
                ],
            },
        )
        if failure == 'midi':
            raise ValueError('byte mismatch')

    monkeypatch.setattr(transport_trace, 'snapshot', read_snapshot)
    monkeypatch.setattr(qualify_usb_midi, 'run', midi_run)
    report = tmp_path / 'capture.toml'
    options = qualify_usb_midi.Options(
        run=True,
        probe_short_messages=True,
        midi_only=True,
        expect_enabled=True,
        probe_trial=qualify_usb_midi.Trial.sysex,
        trace_build=tmp_path,
        report=report,
    )
    if failure == 'none':
        qualify_usb_midi.capture_transport(options)
    else:
        with pytest.raises(
            ValueError,
            match={
                'midi': 'byte mismatch',
                'serial': 'USB disappeared',
                'reboot': 'rebooted',
            }[failure],
        ):
            qualify_usb_midi.capture_transport(options)
    assert events == ['RESET', 'MIDI', 'STOP']
    saved = tomlkit.parse(report.read_text()).unwrap()
    assert saved['trials'][0]['received_hex'] == (
        'f07df7' if failure == 'midi' else 'f07d01f7'
    )
    assert saved['transport_before']['trace']['build'] == expected.trace_id
    assert (tmp_path / 'capture.midi.toml').is_file()
    if failure in ('serial', 'reboot'):
        assert saved['success'] is False and 'transport_error' in saved
    else:
        assert saved['transport_captured'] is True
        assert (
            qualify_usb_midi.analyze_report(report)['transport_comparison']
            == saved['transport_comparison']
        )


TRACE = (
    'CNTRL MIDI TRACE version=2 build=0123456789abcdef active=0 '
    'usb_raw_n=26 usb_raw_crc=0x1111 usb_valid_n=25 usb_valid_crc=0x2222 '
    'din_tx_n=25 din_tx_crc=0x2222 din_rx_n=18 din_rx_crc=0x3333 '
    'usb_return_n=18 usb_return_crc=0x3333 rejects=1 reject_packet=0xf705 '
    'reject_source=96 reject_route=0 reject_state=0 reject_ms=100 '
    'reject_offset=2 reject_valid_offset=2 '
    'din_drops=4 din_overruns=13 trs_drops=0 trs_overruns=0 '
    'drop_gap_us=23000 drop_available=63 drop_delta=7 '
    'overrun_gap_us=25000 overrun_available=63 overrun_delta=1 '
    'drop_phase=display drop_phase_us=22990 max_phase=display max_phase_us=33000'
)
