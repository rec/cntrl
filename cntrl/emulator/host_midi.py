"""Explicit host MIDI connections backed by middy."""

from middy import codecs, events, midi


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
        self.decoder.reset()
        if self.input is not None:
            self.input.close()
            self.input = None
        if self.output is not None:
            self.output.close()
            self.output = None
        self.input_connected = self.output_connected = False
        self.input_ports.clear()
        self.output_ports.clear()
