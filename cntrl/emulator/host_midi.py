"""Explicit host MIDI connections backed by middy."""

from middy import codecs, events, midi

MIDI_ERRORS = (midi.MidiError, OSError, TypeError, ValueError)


class HostMidi:
    def __init__(self, input_enabled: bool, output_enabled: bool) -> None:
        self.input_enabled = input_enabled
        self.output_enabled = output_enabled
        self.input: midi.Input[events.ReceivedPacket] | None = None
        self.output: midi.Output | None = None
        self.input_ports: list[midi.Port] = []
        self.output_ports: list[midi.Port] = []
        self.input_connected = False
        self.output_connected = False
        self.decoder = codecs.Decoder(sysex_limit=65536)
        self.held_notes: dict[tuple[int, int], int] = {}
        self.held_pedals: set[tuple[int, int]] = set()

    def refresh(self) -> None:
        if self.input_enabled:
            if self.input is None:
                self.input = midi.Input(raw=True)
            self.input_ports = self.input.ports()
        if self.output_enabled:
            if self.output is None:
                self.output = midi.Output()
            self.output_ports = self.output.ports()

    def connect_input(self, index: int) -> None:
        if self.input is None or not 0 <= index < len(self.input_ports):
            raise ValueError('refresh and select a host MIDI input first')
        if self.input_connected:
            raise ValueError('disconnect the current host input before reconnecting')
        self.input.connect(self.input_ports[index])
        self.input_connected = True

    def connect_output(self, index: int) -> None:
        if self.output is None or not 0 <= index < len(self.output_ports):
            raise ValueError('refresh and select a host MIDI output first')
        if self.output_connected:
            raise ValueError('disconnect the current host output before reconnecting')
        self.output.connect(self.output_ports[index])
        self.output_connected = True

    def receive(self) -> list[bytes]:
        packets: list[bytes] = []
        if self.input is not None and self.input_connected:
            for _ in range(64):
                received = self.input.receive(timeout=0)
                if received is None:
                    break
                if not isinstance(received.packet, bytes):
                    raise TypeError(
                        'the cntrl MIDI backend accepts MIDI 1 byte packets'
                    )
                packets.append(received.packet)
        return packets

    def send(self, data: bytes) -> None:
        if self.output is not None and self.output_connected:
            for packet in self.packets(data):
                self.output.send(packet)
                self.track_output(packet)

    def track_output(self, packet: bytes) -> None:
        status = packet[0]
        if status == 0xFF:
            self.held_notes.clear()
            self.held_pedals.clear()
        if len(packet) != 3:
            return
        kind, channel = status & 0xF0, status & 0x0F
        number, value = packet[1:]
        key = (channel, number)
        if kind in (0x80, 0x90):
            count = self.held_notes.get(key, 0)
            if kind == 0x90 and value:
                self.held_notes[key] = count + 1
            elif count > 1:
                self.held_notes[key] = count - 1
            else:
                self.held_notes.pop(key, None)
        elif kind == 0xB0:
            if 64 <= number <= 69:
                if value >= 64:
                    self.held_pedals.add(key)
                else:
                    self.held_pedals.discard(key)
            elif number == 121:
                self.held_pedals = {p for p in self.held_pedals if p[0] != channel}
            elif number == 120 or number >= 123:
                self.held_notes = {
                    n: c for n, c in self.held_notes.items() if n[0] != channel
                }

    def packets(self, data: bytes) -> list[bytes]:
        decoded = self.decoder.feed(data)
        diagnostics = [m.kind for m in decoded if isinstance(m, codecs.Diagnostic)]
        if diagnostics:
            self.decoder.reset()
            raise ValueError(f'invalid outgoing MIDI stream: {diagnostics[0]}')
        return [
            codecs.encode(m) for m in decoded if not isinstance(m, codecs.Diagnostic)
        ]

    def close(self) -> None:
        input_port, output_port = self.input, self.output
        connected = self.output_connected
        self.input = self.output = None
        self.decoder.reset()
        self.input_connected = self.output_connected = False
        self.input_ports.clear()
        self.output_ports.clear()
        try:
            if output_port is not None:
                try:
                    if connected:
                        self.release_output(output_port)
                finally:
                    output_port.close()
        finally:
            self.held_notes.clear()
            self.held_pedals.clear()
            if input_port is not None:
                input_port.close()

    def release_output(self, output: midi.Output) -> None:
        releases = [
            (bytes((0x80 | channel, note, 0)), count)
            for (channel, note), count in self.held_notes.items()
        ] + [
            (bytes((0xB0 | channel, pedal, 0)), 1)
            for channel, pedal in sorted(self.held_pedals)
        ]
        failure: Exception | None = None
        for packet, count in releases:
            for _ in range(count):
                try:
                    output.send(packet)
                except MIDI_ERRORS as error:
                    failure = failure or error
                    break
        if failure is not None:
            raise failure
