"""Explicit, macOS-only qualification of the MC USB-to-DIN MIDI path."""

import sys
import time
from pathlib import Path
from typing import Annotated

import tyro
from pydantic import BaseModel, ConfigDict
from reccy.configuration.tyro import unit_spec
from reccy.configuration.units import Seconds

from ....patch_switcher.core_midi import Midi, exchange
from ....patch_switcher.transport import SerialConnection, SetupClient


class Options(BaseModel, frozen=True):
    """List endpoints by default; --run sends test traffic to the chosen output."""

    run: bool = False
    destination: str = ''
    source: str = ''
    port: Path = Path('/dev/cu.usbmodem144682801')
    expect_enabled: bool = False
    stress_seconds: Annotated[Seconds, unit_spec(Seconds, 'DURATION')] = 0

    model_config = ConfigDict(extra='forbid')


def main() -> None:
    options = tyro.cli(Options)
    if sys.platform != 'darwin':
        sys.exit('This qualification uses macOS CoreMIDI.')
    midi = Midi()
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
        with SerialConnection(options.port, timeout=3) as connection:
            client = SetupClient(connection)
            before = client.request('CNTRL MIDI STATUS 0')
            print(before, flush=True)
            if before.split()[2] != str(int(options.expect_enabled)):
                raise ValueError(
                    'Bridge enabled state differs from requested qualification'
                )
            client.ok('CNTRL MIDI RESET')
            time.sleep(0.1)
            # Program changes and benign modulation controls, not note events.
            messages = b''.join(bytes((0xC0 | i % 16, i)) for i in range(128))
            messages += b''.join(bytes((0xB0 | i, 1, 0)) for i in range(16))
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
            # Non-commercial SysEx; stream slowly enough for conventional MIDI.
            sysex = b'\xf0\x7d' + bytes(i % 128 for i in range(4096)) + b'\xf7'
            received = exchange(
                midi,
                destination,
                sysex,
                sysex if options.expect_enabled else b'',
                chunk_bytes=60,
                interval_seconds=0.025,
                settle_seconds=0.2,
                label='SysEx',
            )
            print(f'Streamed SysEx: {len(received)} bytes verified', flush=True)
            if options.stress_seconds:
                peers = [
                    v for k, v in destinations.items() if 'cntrl experimental' in k
                ]
                deadline = time.monotonic() + options.stress_seconds
                calls = 0
                while time.monotonic() < deadline:
                    for p in peers:
                        midi.send(p, messages)
                    calls += 1
                    time.sleep(0.001)
                time.sleep(0.5)
                print(f'Overload: {calls} bursts to {len(peers)} outputs', flush=True)
            for i in range(9):
                print(client.request(f'CNTRL MIDI STATUS {i}'), flush=True)
    finally:
        midi.close()


if __name__ == '__main__':
    main()
