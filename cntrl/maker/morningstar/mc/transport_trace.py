"""Read and compare the MC control build's bounded MIDI transport evidence."""

import hashlib
from pathlib import Path

import tomlkit
from pydantic import BaseModel, Field

from ....build import read_artifacts
from ....patch_switcher.transport import SerialConnection, SetupClient


class TraceBuild(BaseModel, frozen=True):
    trace_id: str = Field(pattern=r'^[0-9a-f]{16}$')
    image_sha256: str = Field(pattern=r'^[0-9a-f]{64}$')


class TraceSnapshot(BaseModel, frozen=True):
    device: str
    trace: dict[str, int | str]
    din_route: str
    usb_route: str


def expected_build(export: Path) -> TraceBuild:
    artifacts = read_artifacts(export)
    metadata = TraceBuild.model_validate(
        tomlkit.parse((export / 'build.toml').read_text()).unwrap()
    )
    if (
        hashlib.sha256((export / artifacts.image).read_bytes()).hexdigest()
        != metadata.image_sha256
    ):
        raise ValueError('trace image differs from its build manifest')
    return metadata


def parse_trace(response: str) -> dict[str, int | str]:
    if not response.startswith('CNTRL MIDI TRACE '):
        raise ValueError(f'not a MIDI transport trace response: {response}')
    fields = dict(p.split('=', 1) for p in response.split()[3:])
    if 'build' not in fields:
        raise ValueError('incomplete MIDI transport trace: build is missing')
    build = fields.pop('build')
    result: dict[str, int | str] = {k: int(v, 0) for k, v in fields.items()}
    result['build'] = build
    if missing := REQUIRED_FIELDS - result.keys():
        raise ValueError(
            'incomplete MIDI transport trace: ' + ', '.join(sorted(missing))
        )
    if result['version'] != 1:
        raise ValueError('unsupported MIDI transport trace version')
    return result


def snapshot(port: Path, expected: str, command: str) -> TraceSnapshot:
    with SerialConnection(port, timeout=3, retry=False) as connection:
        client = SetupClient(connection)
        initial = parse_trace(client.request('CNTRL MIDI TRACE STATUS'))
        if initial['build'] != expected:
            raise ValueError(
                f'trace build mismatch: expected {expected}, got {initial["build"]}'
            )
        client.ok(f'CNTRL MIDI TRACE {command}')
        trace = parse_trace(client.request('CNTRL MIDI TRACE STATUS'))
        if trace['build'] != expected or trace['active'] != int(command == 'RESET'):
            raise ValueError(
                'trace operation did not preserve the expected build and state'
            )
        return TraceSnapshot(
            device=client.request('CNTRL STATUS'),
            trace=trace,
            din_route=client.request('CNTRL MIDI STATUS 0'),
            usb_route=client.request('CNTRL MIDI STATUS 6'),
        )


def compare_transport(before: TraceSnapshot, after: TraceSnapshot) -> dict[str, object]:
    trace = after.trace
    boundaries: list[dict[str, object]] = []
    for first, second in BOUNDARIES:
        boundaries.append(
            {
                'input': first,
                'output': second,
                'input_bytes': trace[f'{first}_n'],
                'output_bytes': trace[f'{second}_n'],
                'counts_match': trace[f'{first}_n'] == trace[f'{second}_n'],
                'crc_match': trace[f'{first}_crc'] == trace[f'{second}_crc'],
            }
        )
    uart = {
        k: (int(after.trace[k]) - int(before.trace[k])) & 0xFFFFFFFF
        for k in ('din_drops', 'din_overruns', 'trs_drops', 'trs_overruns')
    }
    result: dict[str, object] = {
        'scope': (
            'All measured DIN-route bytes from TRACE RESET until TRACE STOP. '
            'Driver acceptance is not a wire acknowledgement.'
        ),
        'boundaries': boundaries,
        'uart_deltas': uart,
        'uart_interpretation': (
            'Software drops count discarded ring-buffer bytes. Hardware overrun '
            'latches are observations, not an exact lost-byte count.'
        ),
        'rejections': trace['rejects'],
        'first_uart_loss_observations': {
            'software_drop_poll_gap_us': trace['drop_gap_us'],
            'software_drop_available': trace['drop_available'],
            'software_drop_delta': trace['drop_delta'],
            'overrun_poll_gap_us': trace['overrun_gap_us'],
            'overrun_available': trace['overrun_available'],
            'overrun_latch_delta': trace['overrun_delta'],
        },
    }
    if int(trace['rejects']):
        packet = int(trace['reject_packet'])
        result['first_rejection'] = {
            'usb_event_hex': packet.to_bytes(4, 'little').hex(),
            'cin': packet & 15,
            'cable': (packet >> 4) & 15,
            'source': trace['reject_source'],
            'output_route': trace['reject_route'],
            'parser_sysex': bool(int(trace['reject_state']) & 1),
            'parser_discarding': bool(int(trace['reject_state']) & 2),
            'milliseconds': trace['reject_ms'],
            'raw_byte_offset': trace['reject_offset'],
            'accepted_byte_offset': trace['reject_valid_offset'],
            'capture_limit': (
                'Only the first rejected event is retained; rejections counts '
                'all events on the measured DIN route.'
            ),
        }
    return result


BOUNDARIES = (
    ('usb_raw', 'usb_valid'),
    ('usb_valid', 'din_tx'),
    ('din_tx', 'din_rx'),
    ('din_rx', 'usb_return'),
)
REQUIRED_FIELDS = set(
    (
        'version active rejects reject_packet reject_source reject_route '
        'reject_state reject_ms reject_offset reject_valid_offset '
        'din_drops din_overruns trs_drops trs_overruns '
        'drop_gap_us drop_available drop_delta '
        'overrun_gap_us overrun_available overrun_delta'
    ).split()
) | {
    f'{n}_{s}'
    for n in ('usb_raw', 'usb_valid', 'din_tx', 'din_rx', 'usb_return')
    for s in ('n', 'crc')
}
