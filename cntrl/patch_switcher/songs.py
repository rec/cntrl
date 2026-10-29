"""Portable song libraries with inline selections and live setup references."""

import struct
from copy import deepcopy
from pathlib import Path
from typing import Annotated
from zlib import crc32

import tomlkit
import tyro
from pydantic import BaseModel, ConfigDict, Field, ValidationError, field_validator

from . import compiler, models, resolved, transport
from .data_files import validate_name


class Song(BaseModel, frozen=True):
    id: int = Field(gt=0, lt=0xFFFFFFFF)
    name: str
    patches: list[dict[str, object]] = Field(min_length=1)

    @field_validator('patches')
    @classmethod
    def validate_patches(
        cls, values: list[dict[str, object]]
    ) -> list[dict[str, object]]:
        result = []
        errors: list[str] = []
        for index, value in enumerate(values):
            fields = dict(value)
            occurrence = fields.pop('id', None)
            if occurrence is not None and (
                type(occurrence) is not int or not 0 < occurrence < 0xFFFFFFFF
            ):
                errors.append(f'patches[{index}].id: expected 1..4294967294')
            try:
                patch = models.patch_adapter.validate_python(fields)
            except ValidationError as error:
                errors.append(f'patches[{index}]: {error}')
                continue
            fields = patch.model_dump(exclude_none=True)
            if occurrence is not None:
                fields['id'] = occurrence
            result.append(fields)
        if errors:
            raise ValueError('\n'.join(errors))
        return result

    model_config = ConfigDict(extra='forbid')


class Setlist(BaseModel, frozen=True):
    id: int = Field(gt=0, lt=0xFFFFFFFF)
    name: str
    songs: list[int] = Field(default_factory=list)
    entry_ids: list[int] = Field(default_factory=list)
    model_config = ConfigDict(extra='forbid')


class Library(BaseModel, frozen=True):
    next_id: int | None = Field(default=None, ge=1, le=0xFFFFFFFF)
    instruments: dict[str, object] = Field(default_factory=dict)
    definitions: models.Definitions | None = None
    patches: dict[str, object] = Field(default_factory=dict)
    fonts: dict[str, str] = Field(default_factory=dict)
    tags: dict[str, models.Tag] = Field(default_factory=dict)
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
    ids = [s.id for s in library.songs] + [s.id for s in library.setlists]
    song_ids = {s.id for s in library.songs}
    for s in library.setlists:
        if s.entry_ids and len(s.entry_ids) != len(s.songs):
            raise ValueError('entry_ids must match songs')
        ids.extend(s.entry_ids)
        if any(i not in song_ids for i in s.songs):
            raise ValueError('setlist references an unknown song')
    explicit = [
        i for s in library.songs for p in s.patches if isinstance(i := p.get('id'), int)
    ]
    if len(set(ids + explicit)) != len(ids + explicit):
        raise ValueError('song, setlist and patch occurrence IDs must be unique')
    next_id = max(ids + explicit, default=0) + 1
    if library.next_id is not None:
        if library.next_id < next_id:
            raise ValueError('next_id must exceed all existing IDs')
        next_id = library.next_id
    positions: list[list[tuple[int, dict[str, object], str]]] = []
    values_for_source: list[dict[str, object]] = []
    paths: list[str] = []
    for song_index, song in enumerate(library.songs):
        values = []
        for patch_index, position in enumerate(song.patches):
            value = dict(position)
            occurrence = value.pop('id', None)
            if occurrence is None:
                occurrence = next_id
                next_id += 1
            if not isinstance(occurrence, int):
                raise ValueError('invalid occurrence ID')
            key = f'@song-{occurrence}'
            values_for_source.append(value)
            paths.append(f'songs[{song_index}].patches[{patch_index}]')
            values.append((occurrence, value, key))
        positions.append(values)
    source = b''
    roots: dict[str, int] = {}
    if values_for_source or library.patches:
        if target is None:
            raise ValueError(
                'song patches require --target to compile their setup context'
            )
        context: dict[str, object] = {
            'instruments': library.instruments,
            'patches': library.patches,
            'patch_lists': {'Songs': values_for_source} if values_for_source else {},
            'fonts': library.fonts,
            'tags': {
                k: v.model_dump(exclude_none=True) for k, v in library.tags.items()
            },
        }
        if library.definitions is not None:
            context['definitions'] = library.definitions.model_dump(exclude_none=True)
        try:
            source = compiler.compile_show(context, {}, [], target, assets, documents)
        except compiler.CompilationError as error:
            messages = error.errors
            for index, path in enumerate(paths):
                messages = [
                    m.replace(f'patch_lists.Songs[{index}]', path) for m in messages
                ]
            raise compiler.CompilationError(messages) from error
        graph = resolved.graph_offset(source)
        _, _, count, _, _, _ = compiler.GRAPH.unpack_from(source, graph)
        offset = graph + compiler.GRAPH.size + count * 4
        for index, (occurrence, _, _) in enumerate(p for s in positions for p in s):
            roots[f'@song-{occurrence}'] = struct.unpack_from(
                '<H', source, offset + index * 2
            )[0]
    records = []
    browser = 0
    for song, values in zip(library.songs, positions, strict=True):
        selections = []
        for occurrence, _, key in values:
            document, reference, origin, node = (
                bytes(64),
                bytes(64),
                0,
                roots[key],
            )
            header = compiler.HEADER.unpack_from(source)
            label_offset = 76 + header[4] * 68 + header[5] * 68 + browser * 82 + 12
            label = (
                source[label_offset : label_offset + 64].split(b'\0')[0].decode('utf-8')
            )
            browser += 1
            selections.append(
                SELECTION.pack(
                    document, reference, label_text(label), origin, node, occurrence
                )
            )
        records.append(
            selections[0]
            + SONG.pack(song.id, text(song.name, 64), len(selections))
            + b''.join(selections[1:])
        )
    sets = []
    for s in library.setlists:
        entries = []
        for i, song in enumerate(s.songs):
            if s.entry_ids:
                occurrence = s.entry_ids[i]
            else:
                occurrence = next_id
                next_id += 1
            entries.append(struct.pack('<II', occurrence, song))
        sets.append(SET.pack(s.id, text(s.name, 64), len(entries)) + b''.join(entries))
    if next_id > 0xFFFFFFFF:
        raise ValueError('song IDs exhausted')
    payload = (
        struct.pack('<III', next_id, len(library.songs), len(library.setlists))
        + b''.join(records + sets)
        + b'CSP4'
    )
    if source:
        payload += struct.pack('<I', len(source)) + source
    if len(payload) > 0xFFFFFFFF - 20:
        raise ValueError('song library exceeds the SD file format size')
    return payload


def decode(data: bytes) -> Library:
    if len(data) < 16:
        raise ValueError('invalid song library size')
    next_id, ns, nl = struct.unpack_from('<III', data)
    if ns > (len(data) - 16) // (SONG.size + SELECTION.size):
        raise ValueError('invalid song library counts')
    offset = 12
    positions = []
    song_headers = []
    for _ in range(ns):
        if offset + SELECTION.size + SONG.size > len(data):
            raise ValueError('truncated song')
        first = SELECTION.unpack_from(data, offset)
        offset += SELECTION.size
        key, name, count = SONG.unpack_from(data, offset)
        offset += SONG.size
        if count < 1 or count - 1 > (len(data) - offset) // SELECTION.size:
            raise ValueError('invalid song patch count')
        values = [first]
        for _ in range(count - 1):
            values.append(SELECTION.unpack_from(data, offset))
            offset += SELECTION.size
        positions.append(values)
        song_headers.append((key, _string(name)))
    sets = []
    for _ in range(nl):
        if offset + SET.size > len(data):
            raise ValueError('truncated setlist')
        key, name, count = SET.unpack_from(data, offset)
        offset += SET.size
        if count > (len(data) - offset) // 8:
            raise ValueError('invalid setlist count')
        entries = [
            struct.unpack_from('<II', data, offset + j * 8) for j in range(count)
        ]
        offset += count * 8
        sets.append(
            Setlist(
                id=key,
                name=_string(name),
                songs=[e[1] for e in entries],
                entry_ids=[e[0] for e in entries],
            )
        )
    if data[offset : offset + 4] != b'CSP4':
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
    setups: list[object] = []
    programs: list[object] = []
    fonts: dict[str, str] = {}
    source_fonts: list[dict[str, str]] = []
    tags: dict[str, object] = {}
    source_tags: list[set[str]] = []
    for index, source in enumerate(sources):
        exported = tomlkit.parse(resolved.export(source)).unwrap()
        prefix = f'source-{index}-' if len(sources) > 1 else ''
        renamed = {k: prefix + k for k in exported.get('fonts', {})}
        source_fonts.append(renamed)
        fonts.update({renamed[k]: v for k, v in exported.get('fonts', {}).items()})
        tag_names = set(exported.get('tags', {}))
        source_tags.append(tag_names)
        for key, value in exported.get('tags', {}).items():
            tags[prefix + key] = _rebind(value, prefix, renamed, tag_names)
        for key, value in exported.get('instruments', {}).items():
            value = dict(value)
            value['instrument'] = _qualified(prefix, value['instrument'], 31)
            instruments[_qualified(prefix, key, 31)] = value
        for value in exported.get('definitions', {}).get('setups', []):
            value = dict(value)
            value['instrument'] = _qualified(prefix, value['instrument'], 31)
            setups.append(value)
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
    context['tags'] = tags
    if sources:
        context['definitions'] = {'setups': setups, 'programs': programs}
    songs = []
    for (key, name), values in zip(song_headers, positions, strict=True):
        patches = []
        for document, reference, label, source, node, occurrence in values:
            if document[0]:
                if source != 0xFFFFFFFF:
                    raise ValueError('invalid external song Patch')
                value = {
                    'type': 'reference',
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
            value['id'] = occurrence
            patches.append(value)
        songs.append(Song(id=key, name=name, patches=patches))
    result = Library.model_validate(
        context | {'next_id': next_id, 'songs': songs, 'setlists': sets}
    )
    return result


def _string(value: bytes) -> str:
    return value.split(b'\0')[0].decode('ascii')


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
        if item.get('type') == 'program-change':
            item['instrument'] = _qualified(prefix, str(item['instrument']), 31)
        elif item.get('type') == 'reference' and not item.get('document'):
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


def _qualified(prefix: str, name: str, maximum: int) -> str:
    value = prefix + name
    if len(value) <= maximum:
        return value
    suffix = f'-{crc32(name.encode()):08x}'
    return prefix + name[: maximum - len(prefix) - len(suffix)] + suffix


def references(library: Library) -> list[str]:
    work = [p for s in library.songs for p in s.patches] + list(
        library.patches.values()
    )
    result: set[str] = set()
    while work:
        item = work.pop()
        if not isinstance(item, dict):
            continue
        if item.get('type') == 'reference' and isinstance(
            document := item.get('document'), str
        ):
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
        or not 16 <= int(response[2]) <= 0xFFFFFFFF - 20
    ):
        raise ValueError('invalid song library response')
    length = int(response[2])
    result = bytearray()
    while len(result) < length:
        response_line = client.request(f'CNTRL SONGS READ {len(result)}')
        if not response_line.startswith('CNTRL DATA '):
            raise ValueError(response_line)
        chunk = bytes.fromhex(response_line[11:])
        if not 1 <= len(chunk) <= 128:
            raise ValueError('invalid song library chunk')
        result.extend(chunk)
    if len(result) != length or crc32(result) != int(response[3], 16):
        raise ValueError('song library checksum mismatch')
    decode(bytes(result))
    return bytes(result)


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
        data = encode(library, target, companions, assets)
        if not command.overwrite:
            with transport.SerialConnection(command.port) as connection:
                existing = decode(download(transport.SetupClient(connection)))
            if existing.songs or existing.setlists:
                raise ValueError('song library exists; use --overwrite')
        if companion_names:
            if command.target is None:
                raise ValueError('reference imports require --target')
            # The companion setup documents are copied with their font assets.
            cli.run_setup(
                cli.Copy(
                    document=[
                        f'{n.replace("=", "==")}='
                        f'{command.document.parent / (n + ".toml")}'
                        for n in companion_names
                    ],
                    target=[str(command.target)],
                    port=command.port,
                    overwrite=command.overwrite,
                )
            )
        with transport.SerialConnection(command.port) as connection:
            client = transport.SetupClient(connection)
            for key, font in assets.items():
                client.copy_font(key, font)
            client.ok(
                f'CNTRL SONGS BEGIN {len(data)} {crc32(data):08x} '
                f'{int(command.overwrite)}'
            )
            for i in range(0, len(data), 128):
                client.ok('CNTRL SONGS DATA ' + data[i : i + 128].hex())
            client.ok('CNTRL SONGS END')
            if download(client) != data:
                raise ValueError('song library read-back mismatch')
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
                cli.save_font(command.directory, key, client.download_font(key))
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


SELECTION = struct.Struct('<64s64s64sIII')
SONG = struct.Struct('<I64sI')
SET = struct.Struct('<I64sI')
