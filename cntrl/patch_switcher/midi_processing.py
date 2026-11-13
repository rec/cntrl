"""Validate and compile the portable MIDI stream graph."""

import struct
from collections.abc import Callable, Sequence
from typing import Annotated, Literal, Protocol

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator
from reccy.configuration import units

from . import lfos


class Endpoint(Protocol):
    @property
    def name(self) -> str: ...

    @property
    def route(self) -> int: ...

    @property
    def input(self) -> bool: ...

    @property
    def output(self) -> bool: ...


class Match(BaseModel, frozen=True):
    type: str | list[str] | None = None
    channel: int | list[int] | None = None
    note: int | list[int] | None = None
    velocity: int | list[int] | None = None
    controller: int | list[int] | None = None
    value: int | list[int] | None = None
    program: int | list[int] | None = None
    pressure: int | list[int] | None = None
    origin: str | list[str] | None = None
    parameter: int | list[int] | None = None

    @model_validator(mode='before')
    @classmethod
    def predicates(cls, value: object) -> object:
        if not isinstance(value, dict):
            raise ValueError('match must be a table')
        for name, raw in value.items():
            if name not in cls.model_fields:
                raise ValueError(f'unknown match field {name!r}')
            values = raw if isinstance(raw, list) else [raw]
            if not values or len(values) > 255:
                raise ValueError(f'{name}: expected distinct nonempty alternatives')
            for item in values:
                if name == 'type':
                    if not isinstance(item, str) or item not in TYPES:
                        raise ValueError(f'unknown MIDI type {item!r}')
                elif name == 'origin':
                    if not isinstance(item, str) or not item:
                        raise ValueError('origin must name an input')
                elif name in RANGES:
                    low, high = RANGES[name]
                    if type(item) is not int or not low <= item <= high:
                        raise ValueError(f'{name} must be an integer in {low}..{high}')
            if len(set(values)) != len(values):
                raise ValueError(f'{name}: duplicate alternative')
        return value

    model_config = ConfigDict(extra='forbid')


class Scale(BaseModel, frozen=True):
    field: Literal['note', 'velocity', 'value', 'program', 'pressure']
    output: list[Annotated[int, Field(strict=True)]] = Field(min_length=2, max_length=2)
    input: list[Annotated[int, Field(strict=True)]] | None = Field(
        default=None, min_length=2, max_length=2
    )
    match: Match = Field(default_factory=Match)

    @model_validator(mode='after')
    def bounds(self) -> 'Scale':
        low, high = RANGES[self.field]
        for interval in (self.input, self.output):
            if interval is not None and any(
                type(i) is not int or not low <= i <= high for i in interval
            ):
                raise ValueError(f'{self.field} range must be within {low}..{high}')
        if self.input is not None and self.input[0] >= self.input[1]:
            raise ValueError('scale input range must increase')
        return self

    model_config = ConfigDict(extra='forbid')


class FieldCopy(BaseModel, frozen=True):
    source: str = Field(alias='from')
    offset: Annotated[int, Field(strict=True, ge=-16383, le=16383)] = 0

    @field_validator('source')
    @classmethod
    def numeric_source(cls, value: str) -> str:
        if value not in RANGES:
            raise ValueError('field copy requires a numeric MIDI field')
        return value

    model_config = ConfigDict(extra='forbid')


class Map(BaseModel, frozen=True):
    match: Match = Field(default_factory=Match)
    set: dict[str, object] = Field(min_length=1)

    @model_validator(mode='after')
    def assignments(self) -> 'Map':
        for name, raw in self.set.items():
            if name == 'type':
                if not isinstance(raw, str) or raw not in TYPES:
                    raise ValueError('unknown destination MIDI type')
                continue
            if name not in RANGES:
                raise ValueError(f'unknown destination field {name!r}')
            if isinstance(raw, dict):
                FieldCopy.model_validate(raw)
            else:
                low, high = RANGES[name]
                if type(raw) is not int or not low <= raw <= high:
                    raise ValueError(f'{name} must be an integer in {low}..{high}')
        destination = self.set.get('type')
        if destination is not None:
            if not isinstance(self.match.type, str):
                raise ValueError('type conversion requires one matched source type')
            source = self.match.type
            if source != destination and 'sysex' in (source, destination):
                raise ValueError('SysEx frames cannot be converted to another type')
            if source in ('note-on', 'note-off') and source != destination:
                raise ValueError('conversion cannot discard a paired note release')
            if destination in ('note-on', 'note-off') and source != destination:
                raise ValueError(
                    'conversion cannot create a note without a paired release'
                )
            available = MESSAGE_FIELDS[source] | set(self.set)
            if set(self.set) - {'type'} - MESSAGE_FIELDS[str(destination)]:
                raise ValueError('assignment field is absent on destination type')
            if not MESSAGE_FIELDS[str(destination)] <= available:
                raise ValueError('conversion omits required destination fields')
        return self

    model_config = ConfigDict(extra='forbid')


class Delay(BaseModel, frozen=True):
    time: Annotated[
        float, Field(gt=0, le=3600, allow_inf_nan=False), units.unit_validator('second')
    ]
    repeats: Annotated[int, Field(strict=True, ge=1, le=64)] = 1
    feedback: Annotated[float, Field(ge=0, le=1, allow_inf_nan=False)] = 1.0

    model_config = ConfigDict(extra='forbid')


class Reverse(BaseModel, frozen=True):
    window: (
        Annotated[
            float,
            Field(gt=0, le=3600, allow_inf_nan=False),
            units.unit_validator('second'),
        ]
        | None
    ) = None

    model_config = ConfigDict(extra='forbid')


class Clock(BaseModel, frozen=True):
    bpm: Annotated[float, Field(gt=0, le=1000, allow_inf_nan=False)] = 120

    model_config = ConfigDict(extra='forbid')


class File(BaseModel, frozen=True):
    filename: Annotated[str, Field(max_length=95)] = ''
    loops: Annotated[int, Field(strict=True, ge=0, le=4294967295)] = 1
    pre_delay: Annotated[
        int, Field(strict=True, ge=0, le=4294967295), units.unit_validator('second')
    ] = 0
    post_delay: Annotated[
        int, Field(strict=True, ge=0, le=4294967295), units.unit_validator('second')
    ] = 0
    speed: Annotated[float, Field(gt=0, allow_inf_nan=False)] = 1
    reverse: bool = False
    transpose: Annotated[int, Field(strict=True, ge=-127, le=127)] = 0

    @field_validator('filename')
    @classmethod
    def basename(cls, value: str) -> str:
        if value in ('.', '..') or any(
            c in '/\\' or not 32 <= ord(c) <= 126 for c in value
        ):
            raise ValueError('filename must be an ASCII basename')
        return value

    model_config = ConfigDict(extra='forbid')


class Route(BaseModel, frozen=True):
    source: str | list[str] | None = Field(default=None, alias='from')
    to: str | list[str] | None = None
    via: list[dict[str, object]] = Field(default_factory=list, max_length=65535)
    generate: dict[str, object] | None = None

    @field_validator('source', 'to', mode='before')
    @classmethod
    def distinct_names(cls, value: object) -> object:
        if value is None:
            return value
        names = value if isinstance(value, list) else [value]
        if not names or any(not isinstance(n, str) or not n for n in names):
            raise ValueError('expected an endpoint name or nonempty list of names')
        if len(names) > 65535:
            raise ValueError('route exceeds the encoded input or output count')
        if len(set(names)) != len(names):
            raise ValueError('duplicate endpoint or route reference')
        return value

    @model_validator(mode='after')
    def processors(self) -> 'Route':
        if (self.source is None) == (self.generate is None):
            raise ValueError('supply exactly one of from and generate')
        if self.generate is not None:
            if len(self.via) == 65535:
                raise ValueError('generator and processing exceed 65535 stages')
            if len(self.generate) != 1:
                raise ValueError('generate requires one generator')
            for name, argument in self.generate.items():
                if (
                    name in REGISTERED_PROCESSORS
                    and REGISTERED_PROCESSORS[name].generator
                ):
                    REGISTERED_PROCESSORS[name].configuration.model_validate(argument)
                elif name in GENERATORS:
                    GENERATORS[name].model_validate(argument)
                else:
                    raise ValueError(f'unknown generator {name!r}')
        for step in self.via:
            validate_step(step)
        return self

    model_config = ConfigDict(extra='forbid')


class ProcessorRegistration(BaseModel, frozen=True):
    opcode: Annotated[int, Field(strict=True, ge=128, le=255)]
    configuration: type[BaseModel]
    encode: Callable[[BaseModel], bytes]
    ram_bytes: Annotated[int, Field(strict=True, ge=0, le=524288)] = 0
    version: Annotated[int, Field(strict=True, ge=1, le=255)] = 1
    generator: Annotated[bool, Field(strict=True)] = False

    model_config = ConfigDict(extra='forbid')


def register_processor(
    name: str,
    opcode: int,
    configuration: type[BaseModel],
    encode: Callable[[BaseModel], bytes],
    *,
    ram_bytes: int = 0,
    version: int = 1,
    generator: bool = False,
) -> None:
    """Register one statically installed processor's configuration and codec."""
    if not name or name in STEPS or name in REGISTERED_PROCESSORS:
        raise ValueError(f'duplicate or reserved MIDI processor {name!r}')
    if any(p.opcode == opcode for p in REGISTERED_PROCESSORS.values()):
        raise ValueError(f'duplicate MIDI processor opcode {opcode}')
    REGISTERED_PROCESSORS[name] = ProcessorRegistration(
        opcode=opcode,
        configuration=configuration,
        encode=encode,
        ram_bytes=ram_bytes,
        version=version,
        generator=generator,
    )


def validate_step(step: dict[str, object]) -> None:
    if len(step) != 1:
        raise ValueError('each via step must name exactly one processor')
    name, argument = next(iter(step.items()))
    if name in ('transpose', 'invert'):
        low, high = (-127, 127) if name == 'transpose' else (0, 127)
        if type(argument) is not int or not low <= argument <= high:
            raise ValueError(f'{name} must be an integer in {low}..{high}')
    elif name in REGISTERED_PROCESSORS:
        if REGISTERED_PROCESSORS[name].generator:
            raise ValueError('a generator must be declared in generate, with no input')
        REGISTERED_PROCESSORS[name].configuration.model_validate(argument)
    elif name in PROCESSORS:
        if name == 'delay' and not isinstance(argument, dict):
            argument = {'time': argument}
        PROCESSORS[name].model_validate(argument)
    else:
        raise ValueError(f'unknown MIDI processor {name!r}')


def compile_routes(
    system: dict[str, object] | None,
    patches: dict[int, dict[str, object]],
    endpoints: Sequence[Endpoint],
) -> bytes:
    """Return a self-contained indexed CSTR section, or no bytes if unused."""
    if system is None and not patches:
        return b''
    ports = {e.name: e for e in endpoints}
    if any(n.startswith('system/') for n in ports):
        raise ValueError('endpoint aliases cannot use the system/ prefix')
    definitions: dict[tuple[int, str], Route] = {}
    for scope, routes in [(65535, system or {}), *patches.items()]:
        if not 0 <= scope <= 65535:
            raise ValueError('invalid patch scope')
        for name, raw in routes.items():
            if not name or '/' in name or name in ports:
                raise ValueError(f'invalid or reserved MIDI route name {name!r}')
            if len(name.encode()) > 63 or any(ord(c) < 32 for c in name):
                raise ValueError(
                    'MIDI route name exceeds 63 bytes or contains controls'
                )
            definitions[scope, name] = Route.model_validate(raw)
    if len(definitions) > 65535:
        raise ValueError('MIDI graph exceeds the encoded 65535 routes')
    references: dict[tuple[int, str], list[tuple[int, str]]] = {}
    consumed: set[tuple[int, str]] = set()
    for key, route in definitions.items():
        scope, name = key
        references[key] = []
        for source in names(route.source):
            if source in ports:
                if not ports[source].input:
                    raise ValueError(f'{name}: {source!r} is not an input')
                continue
            dependency = (
                (65535, source[7:]) if source.startswith('system/') else (scope, source)
            )
            if source.startswith('system/') and scope == 65535:
                raise ValueError('system/ qualification is only valid inside patches')
            if dependency not in definitions:
                raise ValueError(f'{name}: unknown input or route {source!r}')
            references[key].append(dependency)
            consumed.add(dependency)
        for output in names(route.to):
            if output not in ports or not ports[output].output:
                raise ValueError(f'{name}: unknown or invalid output {output!r}')
    ordered: list[tuple[int, str]] = []
    visiting: set[tuple[int, str]] = set()
    complete: set[tuple[int, str]] = set()

    for root in definitions:
        if root in complete:
            continue
        pending = [(root, 0)]
        while pending:
            key, next_dependency = pending[-1]
            visiting.add(key)
            if next_dependency < len(references[key]):
                dependency = references[key][next_dependency]
                pending[-1] = (key, next_dependency + 1)
                if dependency in visiting:
                    raise ValueError(f'MIDI route cycle at {dependency[1]!r}')
                if dependency not in complete:
                    pending.append((dependency, 0))
                continue
            pending.pop()
            visiting.remove(key)
            complete.add(key)
            ordered.append(key)
    validate_lfo_routes(definitions, ordered, references)
    clock_sources: dict[tuple[int, str], int] = {}
    clock_outputs: dict[str, dict[int, int]] = {}
    for key in ordered:
        route = definitions[key]
        owners = sum(clock_sources[d] for d in references[key])
        if route.generate is not None and 'clock' in route.generate:
            owners += 1
        for step in route.via:
            if 'keep' in step:
                match = Match.model_validate(step['keep'])
                if any(f != 'type' for f in match.model_dump(exclude_none=True)) or (
                    match.type is not None and 'clock' not in names(match.type)
                ):
                    owners = 0
            elif 'drop' in step:
                match = Match.model_validate(step['drop'])
                if not match.model_dump(exclude_none=True) or (
                    match.type is not None
                    and 'clock' in names(match.type)
                    and len(match.model_dump(exclude_none=True)) == 1
                ):
                    owners = 0
        clock_sources[key] = owners
        for output in names(route.to):
            scopes = clock_outputs.setdefault(output, {})
            scopes[key[0]] = scopes.get(key[0], 0) + owners
    for output, scopes in clock_outputs.items():
        system_owners = scopes.pop(65535, 0)
        if system_owners > 1 or any(
            system_owners + owners > 1 for owners in scopes.values()
        ):
            raise ValueError(f'{output}: multiple active clock generators')
    indexes = {k: i for i, k in enumerate(ordered)}
    data = bytearray()
    for key in ordered:
        scope, name = key
        route = definitions[key]
        if route.to is None and key not in consumed:
            raise ValueError(f'{name}: unused route has no output')
        steps = [_pack_step(s, ports) for s in route.via]
        if route.generate is not None:
            steps.insert(0, _pack_generator(route.generate))
        if any(
            'reverse' in s and not Reverse.model_validate(s['reverse']).window
            for s in route.via
        ):
            if route.generate is None or 'file' not in route.generate:
                raise ValueError(f'{name}: live reverse requires a window')
        inputs = names(route.source)
        mask = sum(1 << ports[o].route for o in names(route.to))
        data.extend(ROUTE.pack(scope, len(inputs), mask, len(steps), name.encode()))
        for source in inputs:
            if source in ports:
                data.extend(INPUT.pack(0, ports[source].route))
            else:
                dependency = (
                    (65535, source[7:])
                    if source.startswith('system/')
                    else (scope, source)
                )
                data.extend(INPUT.pack(1, indexes[dependency]))
        data.extend(b''.join(steps))
    return (
        HEADER.pack(
            b'CSTR', HEADER.size + len(data), len(ordered), int(system is not None)
        )
        + data
    )


def names(value: str | list[str] | None) -> list[str]:
    return [] if value is None else [value] if isinstance(value, str) else value


def validate_lfo_routes(
    definitions: dict[tuple[int, str], Route],
    ordered: list[tuple[int, str]],
    references: dict[tuple[int, str], list[tuple[int, str]]],
) -> None:
    bindings: list[tuple[int, str, lfos.Target]] = []
    for root in ordered:
        generator = definitions[root].generate
        if generator is None or 'lfo' not in generator:
            continue
        lfo = lfos.Lfo.model_validate(generator['lfo'])
        reached: dict[tuple[int, str], lfos.Target] = {root: lfo.target}
        for key in ordered[ordered.index(root) :]:
            sources = [reached[s] for s in references[key] if s in reached]
            if key != root and not sources:
                continue
            if len(sources) > 1:
                raise ValueError(f'{key[1]}: LFO paths cannot reconverge')
            target = sources[0] if sources else lfo.target
            for step in definitions[key].via:
                name, raw = next(iter(step.items()))
                if name in ('transpose', 'invert') and target.note is not None:
                    assert isinstance(raw, int)
                    note = (
                        target.note + raw
                        if name == 'transpose'
                        else 2 * raw - target.note
                    )
                    target = lfos.Target.model_validate(
                        target.model_dump() | {'note': note}
                    )
                elif name == 'scale':
                    scale = Scale.model_validate(raw)
                    field = (
                        'program'
                        if target.type == 'program-change'
                        else 'pressure'
                        if 'pressure' in target.type
                        else 'value'
                    )
                    if scale.field != field:
                        raise ValueError(
                            'LFO scale must preserve its parameter address'
                        )
                    low, high = target.bounds
                    if any(not low <= v <= high for v in scale.output):
                        raise ValueError('LFO scale exceeds destination resolution')
                elif name == 'map':
                    mapping = Map.model_validate(raw)
                    updates: dict[str, object] = {}
                    field = (
                        'program'
                        if target.type == 'program-change'
                        else 'pressure'
                        if 'pressure' in target.type
                        else 'value'
                    )
                    for n, v in mapping.set.items():
                        if n == field:
                            continue
                        if (
                            isinstance(v, dict)
                            or mapping.match.model_dump(exclude_none=True)
                            or (n == 'type' and v != target.type)
                        ):
                            raise ValueError(
                                'LFO mapping requires a fixed target type and address'
                            )
                        updates[n] = v
                    target = lfos.Target.model_validate(target.model_dump() | updates)
                elif name == 'delay':
                    delay = Delay.model_validate(
                        raw if isinstance(raw, dict) else {'time': raw}
                    )
                    if delay.repeats != 1 or delay.feedback != 1:
                        raise ValueError(
                            'LFO delay must move one stream, without overlapping echoes'
                        )
                elif name not in ('keep', 'drop', 'transpose', 'invert', 'reverse'):
                    raise ValueError(
                        f'{name}: packet plug-ins do not support typed LFO values'
                    )
            reached[key] = target
            if (
                lfo.off.value is not None
                and not target.bounds[0] <= lfo.off.value <= target.bounds[1]
            ):
                raise ValueError('LFO off value is outside the final destination range')
            for output in names(definitions[key].to):
                scope = key[0] if key[0] != 65535 else root[0]
                for other_scope, other_output, other in bindings:
                    if output != other_output or target.channel != other.channel:
                        continue
                    coexist = scope == other_scope or 65535 in (scope, other_scope)
                    same = (
                        target.type == other.type
                        and target.address_number == other.address_number
                    )
                    cc_pair = target.type == other.type == 'control-change' and (
                        target.bits == 14
                        and target.address_number + 32 == other.address_number
                        or other.bits == 14
                        and other.address_number + 32 == target.address_number
                    )
                    if coexist and (same or cc_pair):
                        raise ValueError(
                            f'{output}: overlapping active LFO parameter writers'
                        )
                bindings.append((scope, output, target))


def section_end(data: bytes, offset: int) -> int:
    if data[offset : offset + 4] != b'CSTR':
        return offset
    if offset + HEADER.size > len(data):
        raise ValueError('truncated MIDI stream header')
    _, size, _, flags = HEADER.unpack_from(data, offset)
    if size < HEADER.size or offset + size > len(data) or flags > 1:
        raise ValueError('invalid MIDI stream size or flags')
    return offset + size


def estimate_ram(section: bytes, *, endpoint_count: int) -> int:
    """Count resident ARM32 graph storage and reusable traversal workspace.

    Live notes, echoes, captures and deliveries acquire checked storage when
    admitted. Include 16 bytes per configured allocation for heap alignment and
    allocator metadata, rather than reserving fixed traffic pools.
    """
    # Parser 7, two Connection 24 records, quarantine 2, output bookkeeping 61.
    memory = endpoint_count * 118 + (176 if endpoint_count else 0)
    if not section:
        return memory
    if section_end(section, 0) != len(section):
        raise ValueError('invalid MIDI stream section')
    count = HEADER.unpack_from(section)[2]
    cursor = HEADER.size
    scratch = 1 + count
    paths: list[int] = []
    by_opcode = {p.opcode: p for p in REGISTERED_PROCESSORS.values()}
    for _ in range(count):
        _, inputs, _, steps, _ = ROUTE.unpack_from(section, cursor)
        cursor += ROUTE.size
        root = not inputs
        owners = 0
        for _ in range(inputs):
            kind, index = INPUT.unpack_from(section, cursor)
            cursor += INPUT.size
            if kind:
                scratch += 1
                owners += paths[index]
            else:
                root = True
        paths.append(owners + root)
        memory += (inputs * 4 + 16 if inputs else 0) + (steps * 96 + 16 if steps else 0)
        for _ in range(steps):
            opcode, _, size = STEP.unpack_from(section, cursor)
            cursor += STEP.size
            payload = section[cursor : cursor + size]
            cursor += size
            position = 0
            if opcode in (0, 1, 4, 5):
                fields = payload[position]
                position += 1
                memory += fields * 16 + (16 if fields else 0)
                for _ in range(fields):
                    alternatives = payload[position + 1]
                    position += 2 + alternatives * 4
                    memory += alternatives * 4 + 16
            if opcode == 5:
                memory += payload[position] * 8 + 16
            elif opcode == 6:
                scratch += struct.unpack_from('<H', payload, 8)[0]
            elif opcode == 9:
                memory += size + 16
            elif opcode == 10:
                memory += 120 + 16
            if opcode in by_opcode:
                memory += by_opcode[opcode].ram_bytes + 16
    # Route 184 plus target 8/owner 8/reached 1; offsets 4 and OwnerPath 8.
    if count:
        if sum(paths) > 4294967295 // 8:
            raise ValueError('MIDI ownership paths exceed addressable storage')
        memory += count * 201 + 64 + (count + 1) * 4 + 16
        memory += sum(paths) * 8 + 16 + 48 + 16
    # CheckedList<Processing> nodes are 144 bytes on ARM32.
    return memory + scratch * 160


def _pack_match(match: Match, ports: dict[str, Endpoint]) -> bytes:
    values = match.model_dump(exclude_none=True)
    data = bytearray([len(values)])
    for name, raw in values.items():
        alternatives = raw if isinstance(raw, list) else [raw]
        data.extend(bytes([FIELDS.index(name), len(alternatives)]))
        for value in alternatives:
            if name == 'type':
                value = TYPES[value]
            elif name == 'origin':
                if value not in ports or not ports[value].input:
                    raise ValueError(f'unknown origin input {value!r}')
                value = ports[value].route
            data.extend(struct.pack('<i', value))
    return bytes(data)


def _pack_step(step: dict[str, object], ports: dict[str, Endpoint]) -> bytes:
    name, raw = next(iter(step.items()))
    if name in ('transpose', 'invert'):
        payload = struct.pack('<i', raw)
    elif name in ('keep', 'drop'):
        payload = _pack_match(Match.model_validate(raw), ports)
    elif name == 'scale':
        scale = Scale.model_validate(raw)
        lower, upper = scale.input or (-1, -1)
        payload = _pack_match(scale.match, ports) + struct.pack(
            '<Biiii', FIELDS.index(scale.field), lower, upper, *scale.output
        )
    elif name == 'map':
        mapping = Map.model_validate(raw)
        payload = _pack_match(mapping.match, ports) + bytes([len(mapping.set)])
        for field in FIELDS:
            if field not in mapping.set:
                continue
            value = mapping.set[field]
            source = 255
            if isinstance(value, dict):
                copy = FieldCopy.model_validate(value)
                source, value = FIELDS.index(copy.source), copy.offset
            elif field == 'type':
                value = TYPES[str(value)]
            payload += struct.pack('<BBi', FIELDS.index(field), source, value)
    elif name == 'delay':
        delay = Delay.model_validate(raw if isinstance(raw, dict) else {'time': raw})
        payload = struct.pack(
            '<QHf', round(delay.time * 1_000_000), delay.repeats, delay.feedback
        )
    elif name == 'reverse':
        reverse = Reverse.model_validate(raw)
        payload = struct.pack('<Q', round((reverse.window or 0) * 1_000_000))
    elif name in REGISTERED_PROCESSORS:
        registration = REGISTERED_PROCESSORS[name]
        payload = registration.encode(registration.configuration.model_validate(raw))
        if not isinstance(payload, bytes) or len(payload) > 65535:
            raise ValueError('processor encoder must return at most 65535 bytes')
        return (
            STEP.pack(registration.opcode, registration.version, len(payload)) + payload
        )
    else:
        raise ValueError(f'unknown processor {name!r}')
    return STEP.pack(STEPS.index(name), 0, len(payload)) + payload


def _pack_generator(value: dict[str, object]) -> bytes:
    name, raw = next(iter(value.items()))
    if name in REGISTERED_PROCESSORS:
        return _pack_step(value, {})
    if name == 'lfo':
        payload = lfos.pack(lfos.Lfo.model_validate(raw))
        return STEP.pack(10, 1, len(payload)) + payload
    if name == 'clock':
        payload = struct.pack('<d', Clock.model_validate(raw).bpm)
    else:
        file = File.model_validate(raw)
        payload = struct.pack(
            '<96sIQQdbb',
            file.filename.encode(),
            file.loops,
            round(file.pre_delay * 1_000_000),
            round(file.post_delay * 1_000_000),
            file.speed,
            int(file.reverse),
            file.transpose,
        )
    return STEP.pack(STEPS.index(name), 0, len(payload)) + payload


FIELDS = (
    'type',
    'channel',
    'note',
    'velocity',
    'controller',
    'value',
    'program',
    'pressure',
    'origin',
    'parameter',
)
RANGES = {
    'channel': (1, 16),
    'note': (0, 127),
    'velocity': (0, 127),
    'controller': (0, 127),
    'value': (-8192, 16383),
    'program': (1, 128),
    'pressure': (0, 127),
    'parameter': (0, 16383),
}
TYPES = {
    'note-off': 8,
    'note-on': 9,
    'polyphonic-key-pressure': 10,
    'control-change': 11,
    'program-change': 12,
    'channel-pressure': 13,
    'pitch-bend': 14,
    'nrpn': 16,
    'rpn': 17,
    'sysex': 240,
    'time-code-quarter-frame': 241,
    'song-position-pointer': 242,
    'song-select': 243,
    'tune-request': 246,
    'clock': 248,
    'start': 250,
    'continue': 251,
    'stop': 252,
    'active-sensing': 254,
    'system-reset': 255,
}
MESSAGE_FIELDS = {
    'note-off': {'channel', 'note', 'velocity'},
    'note-on': {'channel', 'note', 'velocity'},
    'polyphonic-key-pressure': {'channel', 'note', 'pressure'},
    'control-change': {'channel', 'controller', 'value'},
    'program-change': {'channel', 'program'},
    'channel-pressure': {'channel', 'pressure'},
    'pitch-bend': {'channel', 'value'},
    'nrpn': {'channel', 'parameter', 'value'},
    'rpn': {'channel', 'parameter', 'value'},
    **{
        n: set()
        for n in TYPES
        if n
        not in (
            'note-off',
            'note-on',
            'polyphonic-key-pressure',
            'control-change',
            'program-change',
            'channel-pressure',
            'pitch-bend',
            'nrpn',
            'rpn',
        )
    },
    'time-code-quarter-frame': {'value'},
    'song-position-pointer': {'value'},
    'song-select': {'value'},
}
PROCESSORS = {
    'keep': Match,
    'drop': Match,
    'scale': Scale,
    'map': Map,
    'delay': Delay,
    'reverse': Reverse,
}
GENERATORS = {'clock': Clock, 'file': File, 'lfo': lfos.Lfo}
STEPS = (
    'keep',
    'drop',
    'transpose',
    'invert',
    'scale',
    'map',
    'delay',
    'reverse',
    'clock',
    'file',
    'lfo',
)
HEADER = struct.Struct('<4sIHH')
ROUTE = struct.Struct('<HHHH64s')
INPUT = struct.Struct('<BH')
STEP = struct.Struct('<BBH')
REGISTERED_PROCESSORS: dict[str, ProcessorRegistration] = {}
