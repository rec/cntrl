"""Portable song libraries with inline selections and live setup references."""

import struct
from copy import deepcopy
from pathlib import Path
from typing import Annotated
from zlib import crc32

import tomlkit
import tyro
from pydantic import (
    BaseModel,
    BeforeValidator,
    ConfigDict,
    Field,
    ValidationError,
    field_validator,
)

from . import compiler, inputs, models, resolved, setup_bundle, transport
from .data_files import validate_name
from .usb_frames import Kind


class Song(BaseModel, frozen=True):
    name: str
    patches: list[
        Annotated[dict[str, object], BeforeValidator(models.infer_patch_type)]
    ] = Field(min_length=1)

    @field_validator('patches')
    @classmethod
    def validate_patches(
        cls, values: list[dict[str, object]]
    ) -> list[dict[str, object]]:
        result = []
        errors: list[str] = []
        for index, value in enumerate(values):
            fields = dict(value)
            try:
                patch = models.patch_adapter.validate_python(fields)
            except ValidationError as error:
                errors.append(f'patches[{index}]: {error}')
                continue
            fields = patch.model_dump(exclude_none=True)
            result.append(fields)
        if errors:
            raise ValueError('\n'.join(errors))
        return result

    model_config = ConfigDict(extra='forbid')


class Setlist(BaseModel, frozen=True):
    name: str
    songs: list[str] = Field(default_factory=list)
    model_config = ConfigDict(extra='forbid')


class Library(BaseModel, frozen=True):
    instruments: dict[str, object] = Field(default_factory=dict)
    definitions: models.Definitions | None = None
    patches: dict[str, object] = Field(default_factory=dict)
    midi: dict[str, object] | None = None
    fonts: dict[str, str] = Field(default_factory=dict)
    tag_styles: dict[str, models.Style] = Field(default_factory=dict)
    songs: list[Song] = Field(default_factory=list)
    setlists: list[Setlist] = Field(default_factory=list)
    model_config = ConfigDict(extra='forbid')


class Copy(BaseModel, frozen=True):
    document: Annotated[Path, tyro.conf.Positional]
    port: Path
    overwrite: bool = False
    target: Path | None = None


class Download(BaseModel, frozen=True):
    directory: Annotated[Path, tyro.conf.Positional]
    port: Path


def text(value: str, size: int) -> bytes:
    if not value or len(value) >= size or any(not 32 <= ord(c) <= 126 for c in value):
        raise ValueError(
            f'expected 1..{size - 1} printable ASCII characters: {value!r}'
        )
    return value.encode().ljust(size, b'\0')


def label_text(value: str) -> bytes:
    encoded = value.encode('utf-8')
    if (
        not encoded
        or len(encoded) >= 64
        or any(ord(c) < 32 or ord(c) == 127 for c in value)
    ):
        raise ValueError('song Patch labels must contain 1..63 UTF-8 bytes')
    return encoded.ljust(64, b'\0')


def encode(
    library: Library,
    target: models.Target | None = None,
    documents: dict[str, object] | None = None,
    assets: dict[int, bytes] | None = None,
) -> bytes:
    for items in (library.songs, library.setlists):
        names = [s.name for s in items]
        if len(set(names)) != len(names):
            raise ValueError(
                'song and setlist names must be unique within each collection'
            )
    positions: list[list[int]] = []
    values_for_source: list[dict[str, object]] = []
    paths: list[str] = []
    for song_index, song in enumerate(library.songs):
        values = []
        for patch_index, position in enumerate(song.patches):
            value = dict(position)
            position = len(values_for_source)
            values_for_source.append(value)
            paths.append(f'songs[{song_index}].patches[{patch_index}]')
            values.append(position)
        positions.append(values)
    source = b''
    roots: list[int] = []
    if values_for_source or library.patches or library.midi is not None:
        if target is None:
            raise ValueError(
                'song patches require --target to compile their setup context'
            )
        port_modes: dict[str, models.TrsMode] = {
            j: next(m for m in modes if m in ('midi-a', 'midi-b'))
            for j, modes in target.trs_ports.items()
            if 'midi-a' in modes or 'midi-b' in modes
        }
        context: dict[str, object] = {
            # This is an execution container, not an authored browser cycle.
            'fill_in_styles': False,
            'inline_patches': False,
            # Song sources validate possible destinations. The selected setup
            # owns the physical jack modes when the song plays.
            'trs_ports': port_modes,
            'auxiliary_switches': {},
            'instruments': library.instruments,
            'patches': library.patches,
            **({'midi': library.midi} if library.midi is not None else {}),
            'patch_cycles': {'Songs': values_for_source} if values_for_source else {},
            'fonts': library.fonts,
            'tag_styles': {
                k: v.model_dump(exclude_none=True)
                for k, v in library.tag_styles.items()
            },
        }
        if library.definitions is not None:
            context['definitions'] = library.definitions.model_dump(exclude_none=True)
        pending = list(library.patches.values()) + values_for_source
        while pending:
            value = pending.pop()
            if isinstance(value, dict):
                bindings = value.get('bindings')
                if isinstance(bindings, dict):
                    for key, binding in bindings.items():
                        if key not in target.buttons and key in inputs.labels(target):
                            port, contact = key.rsplit('.', 1)
                            port_modes[port] = 'aux'
                            if contact == 'both':
                                context['auxiliary_switches'][port] = 'combined'
                        if isinstance(
                            models.parse_binding(binding), models.ExpressionAssignment
                        ):
                            port_modes[key] = 'expression'
                pending.extend(value.values())
            elif isinstance(value, list):
                pending.extend(value)
        try:
            source = compiler.compile_show(context, {}, [], target, assets, documents)
        except compiler.CompilationError as error:
            messages = error.errors
            for index, path in enumerate(paths):
                messages = [
                    m.replace(f'patch_cycles.Songs[{index}]', path) for m in messages
                ]
            raise compiler.CompilationError(messages) from error
        graph = resolved.graph_offset(source)
        _, _, count, _, _, _ = compiler.GRAPH.unpack_from(source, graph)
        offset = graph + compiler.GRAPH.size + count * 4
        roots = [
            struct.unpack_from('<H', source, offset + i * 2)[0]
            for i in range(len(values_for_source))
        ]
    records = []
    browser = 0
    for song, values in zip(library.songs, positions, strict=True):
        selections = []
        for position in values:
            document, reference, origin, node = (
                bytes(64),
                bytes(64),
                0,
                roots[position],
            )
            header = compiler.HEADER.unpack_from(source)
            label_offset = 76 + header[4] * 68 + header[5] * 68 + browser * 82 + 12
            label = (
                source[label_offset : label_offset + 64].split(b'\0')[0].decode('utf-8')
            )
            browser += 1
            selections.append(
                SELECTION.pack(document, reference, label_text(label), origin, node)
            )
        records.append(
            selections[0]
            + SONG.pack(text(song.name, 64), len(selections))
            + b''.join(selections[1:])
        )
    sets = []
    for s in library.setlists:
        entries = [text(n, 64) for n in s.songs]
        sets.append(SET.pack(text(s.name, 64), len(entries)) + b''.join(entries))
    payload = (
        struct.pack('<II', len(library.songs), len(library.setlists))
        + b''.join(records + sets)
        + b'CSP5'
    )
    if source:
        payload += struct.pack('<I', len(source)) + source
    if len(payload) > 0xFFFFFFFF - 20:
        raise ValueError('song library exceeds the SD file format size')
    return payload


def decode(data: bytes) -> Library:
    if len(data) < 12:
        raise ValueError('invalid song library size')
    ns, nl = struct.unpack_from('<II', data)
    if ns > (len(data) - 12) // (SONG.size + SELECTION.size):
        raise ValueError('invalid song library counts')
    offset = 8
    positions = []
    song_headers = []
    for _ in range(ns):
        if offset + SELECTION.size + SONG.size > len(data):
            raise ValueError('truncated song')
        first = SELECTION.unpack_from(data, offset)
        offset += SELECTION.size
        name, count = SONG.unpack_from(data, offset)
        offset += SONG.size
        if count < 1 or count - 1 > (len(data) - offset) // SELECTION.size:
            raise ValueError('invalid song patch count')
        values = [first]
        for _ in range(count - 1):
            values.append(SELECTION.unpack_from(data, offset))
            offset += SELECTION.size
        positions.append(values)
        song_headers.append(_string(name))
    sets = []
    for _ in range(nl):
        if offset + SET.size > len(data):
            raise ValueError('truncated setlist')
        name, count = SET.unpack_from(data, offset)
        offset += SET.size
        if count > (len(data) - offset) // 64:
            raise ValueError('invalid setlist count')
        entries = [
            _string(data[offset + j * 64 : offset + (j + 1) * 64]) for j in range(count)
        ]
        offset += count * 64
        sets.append(
            Setlist(
                name=_string(name),
                songs=entries,
            )
        )
    if data[offset : offset + 4] != b'CSP5':
        raise ValueError('missing song Patch sources')
    offset += 4
    sources = []
    while offset < len(data):
        if offset + 4 > len(data):
            raise ValueError('truncated song Patch source')
        length = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        if length > len(data) - offset:
            raise ValueError('truncated song Patch source')
        value = data[offset : offset + length]
        resolved.graph_offset(value)
        sources.append(value)
        offset += length
    context: dict[str, object] = {'instruments': {}, 'patches': {}}
    instruments: dict[str, object] = {}
    definitions: dict[str, object] = {}
    profiles: list[object] = []
    programs: list[object] = []
    fonts: dict[str, str] = {}
    source_fonts: list[dict[str, str]] = []
    tags: dict[str, object] = {}
    source_tags: list[set[str]] = []
    system_routes: dict[str, object] | None = None
    for index, source in enumerate(sources):
        exported = tomlkit.parse(resolved.export(source)).unwrap()
        prefix = f'source-{index}-' if len(sources) > 1 else ''
        if 'midi' in exported:
            if system_routes is None:
                system_routes = {}
            system_routes.update(_rebind_routes(exported['midi'], prefix, system=True))
        renamed = {k: prefix + k for k in exported.get('fonts', {})}
        source_fonts.append(renamed)
        fonts.update({renamed[k]: v for k, v in exported.get('fonts', {}).items()})
        tag_names = set(exported.get('tag_styles', {}))
        source_tags.append(tag_names)
        for key, value in exported.get('tag_styles', {}).items():
            tags[prefix + key] = _rebind(value, prefix, renamed, tag_names)
        for key, value in exported.get('instruments', {}).items():
            value = dict(value)
            value['instrument'] = _qualified(prefix, value['instrument'], 31)
            instruments[_qualified(prefix, key, 31)] = value
        for value in exported.get('definitions', {}).get('profiles', []):
            value = dict(value)
            value['instrument'] = _qualified(prefix, value['instrument'], 31)
            profiles.append(value)
        for value in exported.get('definitions', {}).get('programs', []):
            value = dict(value)
            value['instrument'] = _qualified(prefix, value['instrument'], 31)
            programs.append(_rebind(value, prefix, renamed, tag_names))
        for key, value in exported.get('patches', {}).items():
            definitions[_qualified(prefix, key, 63)] = _rebind(
                value, prefix, renamed, tag_names
            )
    context['instruments'] = instruments
    context['patches'] = definitions
    context['fonts'] = fonts
    context['tag_styles'] = tags
    if system_routes is not None:
        context['midi'] = system_routes
    if sources:
        context['definitions'] = {'profiles': profiles, 'programs': programs}
    songs = []
    for name, values in zip(song_headers, positions, strict=True):
        patches = []
        for document, reference, label, source, node in values:
            if document[0]:
                if source != 0xFFFFFFFF:
                    raise ValueError('invalid external song Patch')
                value = {
                    'document': _string(document),
                    'patch': _string(reference),
                }
                if label[0] and _string(label) != value['patch']:
                    value['display_name'] = _string(label)
            else:
                if source >= len(sources):
                    raise ValueError('missing song Patch source')
                value = resolved.patch(sources[source], node)
                value = _rebind(
                    value,
                    f'source-{source}-' if len(sources) > 1 else '',
                    source_fonts[source],
                    source_tags[source],
                )
            patches.append(value)
        songs.append(Song(name=name, patches=patches))
    result = Library.model_validate(context | {'songs': songs, 'setlists': sets})
    return result


def _string(value: bytes) -> str:
    return value.split(b'\0')[0].decode('utf-8')


def _rebind(
    value: dict[str, object],
    prefix: str,
    fonts: dict[str, str],
    declared_tags: set[str],
) -> dict[str, object]:
    result = deepcopy(value)
    work = [result]
    while work:
        item = work.pop()
        if isinstance(routes := item.get('midi'), dict):
            item['midi'] = _rebind_routes(routes, prefix)
        if 'parent' in item:
            item['parent'] = _qualified(prefix, str(item['parent']), 63)
        if 'instrument' in item and (
            item.get('type') == 'program-change' or 'parent' in item
        ):
            item['instrument'] = _qualified(prefix, str(item['instrument']), 31)
        elif 'patch' in item and not item.get('document'):
            item['patch'] = _qualified(prefix, str(item['patch']), 63)
        children = item.get('patches', [])
        if isinstance(children, list):
            work.extend(c for c in children if isinstance(c, dict))
        tags = item.get('tags')
        if isinstance(tags, list):
            item['tags'] = [
                prefix + c if isinstance(c, str) and c in declared_tags else c
                for c in tags
            ]
        style = item.get('style')
        if (
            isinstance(style, dict)
            and isinstance(font := style.get('font_name'), str)
            and font in fonts
        ):
            style['font_name'] = fonts[font]
    return result


def _rebind_routes(
    routes: dict[str, object], prefix: str, *, system: bool = False
) -> dict[str, object]:
    result = deepcopy(routes)
    for value in result.values():
        if not isinstance(value, dict) or 'from' not in value:
            continue
        sources = value['from']
        names = sources if isinstance(sources, list) else [sources]
        rebound = []
        for name in names:
            if isinstance(name, str) and name.startswith('system/'):
                name = 'system/' + _qualified(prefix, name[7:], 63)
            elif system and name in routes:
                name = _qualified(prefix, name, 63)
            rebound.append(name)
        value['from'] = rebound if isinstance(sources, list) else rebound[0]
    return {_qualified(prefix, k, 63) if system else k: v for k, v in result.items()}


def _qualified(prefix: str, name: str, maximum: int) -> str:
    value = prefix + name
    if len(value.encode()) <= maximum:
        return value
    suffix = f'-{crc32(name.encode()):08x}'
    stem = name.encode()[: maximum - len(prefix) - len(suffix)].decode(
        'utf-8', errors='ignore'
    )
    return prefix + stem + suffix


def references(library: Library) -> list[str]:
    work = [p for s in library.songs for p in s.patches] + list(
        library.patches.values()
    )
    result: set[str] = set()
    while work:
        item = work.pop()
        if not isinstance(item, dict):
            continue
        if 'patch' in item and isinstance(document := item.get('document'), str):
            validate_name(document)
            result.add(document)
        children = item.get('patches', [])
        if isinstance(children, list):
            work.extend(children)
    return sorted(result)


def download(client: transport.SetupClient) -> bytes:
    response = client.request('CNTRL SONGS SIZE').split()
    if (
        len(response) != 4
        or response[:2] != ['CNTRL', 'SONGS']
        or not 12 <= int(response[2]) <= 0xFFFFFFFF - 20
    ):
        raise ValueError('invalid song library response')
    length = int(response[2])
    result = bytearray()
    while len(result) < length:
        chunk = client.request_data(f'CNTRL SONGS READ {len(result)}')
        if not 1 <= len(chunk) <= client.connection.usb_block_size:
            raise ValueError('invalid song library chunk')
        result.extend(chunk)
    if len(result) != length or crc32(result) != int(response[3], 16):
        raise ValueError('song library checksum mismatch')
    return bytes(result)


def upload(client: transport.SetupClient, data: bytes, overwrite: bool) -> None:
    """Upload an exact current-format library and verify the stored bytes."""
    decode(data)
    client.ok(f'CNTRL SONGS BEGIN {len(data)} {crc32(data):08x} {int(overwrite)}')
    for offset in range(0, len(data), client.connection.usb_block_size):
        client.send_data(
            Kind.songs, data[offset : offset + client.connection.usb_block_size]
        )
    client.ok('CNTRL SONGS END')
    if download(client) != data:
        raise ValueError('song library read-back mismatch')


def run(command: Copy | Download) -> None:
    from . import cli

    if isinstance(command, Copy):
        library = Library.model_validate(compiler.read_document(command.document))
        target = (
            models.Target.model_validate(compiler.read_document(command.target))
            if command.target is not None
            else None
        )
        companion_names = references(library)
        companions = {
            n: compiler.read_document(command.document.parent / (n + '.toml'))
            for n in companion_names
        }
        index = 0
        while index < len(companion_names):
            for name in cli.document_references(companions[companion_names[index]]):
                if name not in companions:
                    validate_name(name)
                    companions[name] = compiler.read_document(
                        command.document.parent / (name + '.toml')
                    )
                    companion_names.append(name)
            index += 1
        assets: dict[int, bytes] = {}
        if companion_names and target is None:
            raise ValueError('reference imports require --target')
        with transport.SerialConnection(command.port) as connection:
            client = transport.SetupClient(connection)
            if not command.overwrite:
                existing = decode(download(client))
                if existing.songs or existing.setlists:
                    raise ValueError('song library exists; use --overwrite')
            current_bundle = client.download_bundle() if companion_names else b''
            compiled = setup_bundle.unpack(current_bundle) if current_bundle else {}
            known = {n.lower() for n in compiled}
            if not command.overwrite and any(
                n.lower() in known for n in companion_names
            ):
                raise ValueError('companion setup exists; use --overwrite')
            # The execution container and its companions use the complete deployed
            # index table, including setups unrelated to this song import.
            documents: dict[str, object] = {n: {} for n in compiled} | companions
            data = encode(library, target, documents, assets)
            if companion_names:
                assert target is not None
                imported = {
                    n: compiler.compile_show(s, {}, [], target, assets, documents)
                    for n, s in companions.items()
                }
                combined = setup_bundle.reindex(compiled | imported)
                sources: dict[str, object] = {
                    n: tomlkit.parse(resolved.export(d)).unwrap()
                    for n, d in combined.items()
                }
                margin = (
                    setup_bundle.HEADER.unpack_from(current_bundle)[5]
                    if current_bundle
                    else 16192
                )
                bundle = setup_bundle.build(
                    combined,
                    sources,
                    setup_bundle.memory_from_bundle(current_bundle)
                    if current_bundle
                    else client.memory(),
                    margin,
                )
            for key, font in assets.items():
                client.copy_font(key, font)
            if companion_names:
                client.copy_bundle(bundle.pack(), overwrite=True)
            upload(client, data, command.overwrite)
        print('Song library copied and verified')
    else:
        command.directory.mkdir(parents=True, exist_ok=True)
        destination = command.directory / 'songs.toml'
        if destination.exists():
            raise ValueError(f'{destination}: already exists')
        with transport.SerialConnection(command.port) as connection:
            client = transport.SetupClient(connection)
            library = decode(download(client))
            for path in library.fonts.values():
                key = int(Path(path).stem, 16)
                cli.save_asset(command.directory / path, client.download_font(key))
        companion_names = references(library)
        if companion_names:
            cli.run_setup(
                cli.Download(
                    names=companion_names,
                    directory=command.directory,
                    port=command.port,
                )
            )
        destination.write_text(tomlkit.dumps(library.model_dump(exclude_none=True)))
        print(destination)


SELECTION = struct.Struct('<64s64s64sII')
SONG = struct.Struct('<64sI')
SET = struct.Struct('<64sI')
