from pathlib import Path
p=Path('cntrl/patch_switcher/models.py');s=p.read_text();i=s.index('class StockFirmware(');s=s[:i]+'''class PlayMidiFile(Presentation, frozen=True):
    operation: Literal['play-midi-file']
    filename: Annotated[str, Field(max_length=95)] = ''
    outputs: Annotated[list[str], Field(min_length=1)] = Field(default_factory=lambda: ['din'])
    loops: Annotated[int, Field(strict=True, ge=0, le=4294967295)] = 1
    pre_delay: Annotated[int, Field(strict=True, ge=0, le=4294967295), units.unit_validator('second')] = 0
    post_delay: Annotated[int, Field(strict=True, ge=0, le=4294967295), units.unit_validator('second')] = 0
    speed: Annotated[float, Field(gt=0, allow_inf_nan=False)] = 1.0
    reverse: bool = False
    transpose: Annotated[int, Field(strict=True, ge=-127, le=127)] = 0

    @field_validator('filename')
    @classmethod
    def basename(cls, value: str) -> str:
        if any(c in value for c in ('/', '\\\\', '\\0')) or value in ('.', '..') or not value.isascii():
            raise ValueError('filename must be an ASCII basename in /cntrl/midi/')
        return value

    @field_validator('outputs')
    @classmethod
    def unique_outputs(cls, value: list[str]) -> list[str]:
        if len(value) != len(set(value)):
            raise ValueError('duplicate MIDI output')
        return value


class RecordMidiFile(Presentation, frozen=True):
    operation: Literal['record-midi-file']
    channels: Annotated[list[Annotated[int, Field(strict=True, ge=1, le=16)]], Field(min_length=1)] = Field(default_factory=lambda: list(range(1, 17)))
    inputs: Annotated[list[Literal['din', 'trs', 'usb-device', 'usb-host']], Field(min_length=1)] = Field(default_factory=lambda: ['din', 'trs', 'usb-device', 'usb-host'])

    @model_validator(mode='after')
    def unique_filters(self) -> 'RecordMidiFile':
        if len(self.channels) != len(set(self.channels)) or len(self.inputs) != len(set(self.inputs)):
            raise ValueError('duplicate MIDI recording filter')
        return self


'''+s[i:];s=s.replace('Brightness | UsbMidiInterface | Navigation | StockFirmware,','Brightness | UsbMidiInterface | Navigation | StockFirmware | PlayMidiFile | RecordMidiFile,');p.write_text(s)
p=Path('cntrl/patch_switcher/compiler.py');s=p.read_text().replace("default_label = value.operation\n", "default_label = value.operation\n",1).replace('elif isinstance(value, models.StockFirmware):\n            default_label', 'elif isinstance(value, (models.StockFirmware, models.PlayMidiFile, models.RecordMidiFile)):\n            default_label');i=s.index('        elif isinstance(value, models.ProgramChange):',s.index('    def add('));s=s[:i]+'''        elif isinstance(value, models.PlayMidiFile):
            for output in value.outputs:
                if output not in target_outputs(self.show, self.target).outputs or output not in utility_pages.CLOCK_ROUTES:
                    self.errors.append(f'{path}: unavailable MIDI file output {output!r}')
            kind = 8
            payload = struct.pack('<96sHIIIdBb', value.filename.encode('ascii'), sum(1 << utility_pages.CLOCK_ROUTES[n] for n in value.outputs if n in utility_pages.CLOCK_ROUTES), value.loops, value.pre_delay, value.post_delay, value.speed, value.reverse, value.transpose)
        elif isinstance(value, models.RecordMidiFile):
            kind = 9
            routes = {'din': 0, 'trs': 1, 'usb-device': 6, 'usb-host': 8}
            payload = struct.pack('<HH', sum(1 << (c - 1) for c in value.channels), sum(1 << routes[n] for n in value.inputs))
'''+s[i:];# target filtered compiler constructor already filtered? use self.target
s=s.replace('target_outputs(self.show, self.target).outputs','self.target.outputs');p.write_text(s)
p=Path('cntrl/patch_switcher/resolved.py');s=p.read_text();i=s.index('    elif kind == 7:',s.index('def _patch('));s=s[:i]+'''    elif kind == 8:
        filename, outputs, loops, pre, post, speed, reverse, transpose = struct.unpack_from('<96sHIIIdBb', data, cursor)
        value.update(operation='play-midi-file', filename=filename.split(b'\\0')[0].decode('ascii'), outputs=[n for n, i in utility_pages.CLOCK_ROUTES.items() if outputs & (1 << i)], loops=loops, pre_delay=pre, post_delay=post, speed=speed, reverse=bool(reverse), transpose=transpose)
    elif kind == 9:
        channels, inputs = struct.unpack_from('<HH', data, cursor)
        value.update(operation='record-midi-file', channels=[i + 1 for i in range(16) if channels & (1 << i)], inputs=[n for n, i in {'din': 0, 'trs': 1, 'usb-device': 6, 'usb-host': 8}.items() if inputs & (1 << i)])
'''+s[i:];p.write_text(s)
