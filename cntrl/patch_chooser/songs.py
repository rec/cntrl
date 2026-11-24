"""Portable song libraries with inline selections and live setup references."""

import struct
from copy import deepcopy
from json import JSONDecodeError
from pathlib import Path
from random import Random
from typing import Annotated, cast
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
    model_validator,
)
from tomlkit.exceptions import ParseError

from . import (
    binary_format,
    compiler,
    compiler_errors,
    compiler_inputs,
    compiler_wire,
    data_files,
    device_globals,
    device_settings,
    display,
    formatting,
    global_config,
    graphics,
    inputs,
    midi_assets,
    models,
    resolved,
    setup_bundle,
    song_artwork,
    source_archive,
    transport,
)
from .data_files import validate_name
from .usb_frames import Kind


class SongLibraryError(ValueError):
    """A song library could not be read, validated, or written."""


class Song(models.BindingScope, frozen=True):
    fast_switches: Annotated[str, Field(strict=True)] | None = None
    navigation: models.PageNavigation | None = None
    bindings: dict[str, object] = Field(default_factory=dict)
    midi: dict[str, object] | None = None
    name: str
    display_name: str | None = None
    style: models.Style | None = None
    seed: Annotated[int, Field(strict=True)] | None = None
    enter: (
        Annotated[dict[str, object], BeforeValidator(models.infer_patch_type)] | None
    ) = None
    exit: (
        Annotated[dict[str, object], BeforeValidator(models.infer_patch_type)] | None
    ) = None
    patches: list[
        Annotated[dict[str, object], BeforeValidator(models.infer_patch_type)]
    ] = Field(min_length=1)

    @model_validator(mode='before')
    @classmethod
    def artwork_field(cls, value: object) -> object:
        if isinstance(value, dict) and 'image' in value:
            raise ValueError('move song image to style.image')
        return value

    @field_validator('enter', 'exit')
    @classmethod
    def validate_hook(cls, value: dict[str, object] | None) -> dict[str, object] | None:
        return (
            models.patch_adapter.validate_python(value).model_dump(exclude_none=True)
            if value is not None
            else None
        )

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
            raise SongLibraryError('\n'.join(errors))
        return result

    @field_validator('style')
    @classmethod
    def supported_style(cls, value: models.Style | None) -> models.Style | None:
        if value is not None and value.horizontal_scale is not None:
            raise SongLibraryError(
                'song styles support image, foreground_color, background_color '
                'and font_name'
            )
        return value

    model_config = ConfigDict(extra='forbid')


class Setlist(BaseModel, frozen=True):
    name: str
    songs: list[str] = Field(default_factory=list)
    display_name: str | None = None
    style: models.Style | None = None
    model_config = ConfigDict(extra='forbid')


class Library(models.BindingScope, frozen=True):
    fast_switches: Annotated[str, Field(strict=True)] | None = None
    bindings: dict[str, object] = Field(default_factory=dict)
    instruments: dict[str, object] = Field(default_factory=dict)
    definitions: models.Definitions | None = None
    patches: dict[str, object] = Field(default_factory=dict)
    midi: dict[str, object] | None = None
    fonts: dict[str, str] = Field(default_factory=dict)
    tag_styles: dict[str, models.Style] = Field(default_factory=dict)
    fill_in_styles: Annotated[bool, Field(strict=True)] = True
    seed: Annotated[int, Field(strict=True)] | None = 23
    background_color: str = '0xFFF8D8'
    songs: list[Song] = Field(default_factory=list)
    setlists: list[Setlist] = Field(default_factory=list)
    model_config = ConfigDict(extra='forbid')


class New(BaseModel, frozen=True):
    """Create a song TOML file with one definition per distinct scene."""

    document: Annotated[Path, tyro.conf.Positional]
    scenes: Annotated[list[str], Field(min_length=1)] = Field(
        default_factory=lambda: ['Verse']
    )


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
        raise SongLibraryError(
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
        raise SongLibraryError('song Patch labels must contain 1..63 UTF-8 bytes')
    return encoded.ljust(64, b'\0')


def encode(
    library: Library,
    target: models.Target | None = None,
    documents: dict[str, object] | None = None,
    assets: dict[int, bytes] | None = None,
    *,
    authoring: data_files.DataFile | None = None,
    trace: data_files.Trace | None = None,
    movie_assets: dict[int, bytes] | None = None,
) -> bytes:
    for items in (library.songs, library.setlists):
        names = [s.name for s in items]
        if len(set(names)) != len(names):
            raise SongLibraryError(
                'song and setlist names must be unique within each collection'
            )
    positions: list[list[int]] = []
    values_for_source: list[dict[str, object]] = []
    paths: list[str] = []
    song_scenes: dict[str, tuple[str, str, str | None]] = {}
    for song_index, song in enumerate(library.songs):
        values = []
        for patch_index, position in enumerate(song.patches):
            value = dict(position)
            position = len(values_for_source)
            values_for_source.append(value)
            paths.append(f'songs[{song_index}].patches[{patch_index}]')
            song_scenes[f'patch_cycles.Songs[{position}]'] = (
                paths[-1],
                song.name,
                song.fast_switches,
            )
            values.append(position)
        positions.append(values)
    hooks: dict[tuple[str, str], int] = {}
    for song_index, song in enumerate(library.songs):
        for name in ('enter', 'exit'):
            if (value := getattr(song, name)) is not None:
                hooks[song.name, name] = len(values_for_source)
                values_for_source.append(value)
                paths.append(f'songs[{song_index}].{name}')
    source = b''
    roots: list[int] = []
    scene_images: dict[int, tuple[str, models.Style, int]] = {}
    if (
        values_for_source
        or library.patches
        or library.midi is not None
        or library.bindings
    ):
        if target is None:
            raise SongLibraryError(
                'song patches require --target to compile their setup context'
            )
        port_modes: dict[str, models.TrsMode] = {
            j: next(m for m in modes if m in ('midi-a', 'midi-b'))
            for j, modes in target.trs_ports.items()
            if 'midi-a' in modes or 'midi-b' in modes
        }
        context: dict[str, object] = {
            **library.model_dump(
                include={
                    'instruments',
                    'patches',
                    'midi',
                    'fonts',
                    'tag_styles',
                    'definitions',
                    'bindings',
                    'fast_switches',
                },
                exclude_unset=True,
                exclude_none=True,
            ),
            # This is an execution container, not an authored browser cycle.
            'fill_in_styles': False,
            'inline_patches': False,
            # Song sources validate possible destinations. The selected setup
            # owns the physical jack modes when the song plays.
            'trs_ports': port_modes,
            'auxiliary_switches': {},
            'patch_cycles': {'Songs': values_for_source} if values_for_source else {},
        }
        if authoring is not None:
            from .source_archive import session_context

            bindings, relocations = session_context(authoring)
            errors: list[str] = []
            context = global_config.resolve_setup(
                context,
                authoring,
                errors,
                trace=trace,
                defaults=trace.defaults if trace is not None else bindings,
                overrides=trace.files if trace is not None else None,
                relocations=relocations,
            )
            if errors:
                raise compiler_errors.CompilationError(errors)
            if 'seed' not in library.model_fields_set and 'seed' in context:
                # The embedded setup compiler validates the inherited value.
                library = library.model_copy(update={'seed': context['seed']})
        port_modes = cast(dict[str, models.TrsMode], context['trs_ports'])
        auxiliary_switches = cast(dict[str, str], context['auxiliary_switches'])
        scopes = [
            (
                2,
                s.name,
                s.model_dump(
                    include={'bindings', 'navigation', 'midi'}, exclude_none=True
                ),
            )
            for s in library.songs
            if s.bindings or s.navigation or s.midi
        ]
        pending: list[object] = [context, *(b for _, _, b in scopes)]
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
                                auxiliary_switches[port] = 'combined'
                        if isinstance(
                            models.parse_binding(binding), models.ExpressionAssignment
                        ):
                            port_modes[key] = 'expression'
                pending.extend(value.values())
            elif isinstance(value, list):
                pending.extend(value)
        try:
            source = compiler.compile_show(
                context,
                {},
                [],
                target,
                assets,
                documents,
                movie_assets,
                source_snapshots=trace.files if trace is not None else None,
                binding_scopes=scopes,
                scene_images=scene_images,
                song_scenes=song_scenes,
            )
        except compiler_errors.CompilationError as error:
            messages = error.errors
            for index, path in enumerate(paths):
                messages = [
                    m.replace(f'patch_cycles.Songs[{index}]', path) for m in messages
                ]
            raise compiler_errors.CompilationError(messages) from error
        graph = resolved.graph_offset(source)
        _, _, count, _, _, _ = compiler_wire.GRAPH.unpack_from(source, graph)
        offset = graph + compiler_wire.GRAPH.size + count * 4
        first = 0
        if values_for_source:
            instruments, cycles = compiler_wire.HEADER.unpack_from(source)[4:6]
            for index in range(cycles):
                at = 76 + instruments * 68 + index * 68
                if source[at : at + 64].split(b'\0')[0] == b'Songs':
                    first = struct.unpack_from('<H', source, at + 64)[0]
                    break
        roots = [
            struct.unpack_from('<H', source, offset + (first + i) * 2)[0]
            for i in range(len(values_for_source))
        ]
    if library.fill_in_styles and library.seed is None:
        library = library.model_copy(update={'seed': Random().getrandbits(63)})
    preferences = models.Setup(
        fill_in_styles=library.fill_in_styles,
        seed=library.seed,
        background_color=library.background_color,
    )
    styles = (
        formatting.styles(
            preferences,
            ((s.name, s.seed) for s in library.songs),
            target.model_copy(
                update={'fonts': [f for f in target.fonts if f.id != 255]}
            ),
            'song',
        )
        if target is not None
        else {}
    )
    records = []
    scope_roots = (
        {(k, n): r for k, n, r in resolved.binding_scopes(source)} if source else {}
    )
    for song, values in zip(library.songs, positions, strict=True):
        style = display.merge_style(
            models.Style(
                foreground_color='black', background_color='white', font_name='Arimo'
            ),
            styles.get((song.name, song.seed)),
            song.style,
        )
        font = (
            next(
                (f for f in target.fonts if f.name == style.font_name and f.id != 255),
                None,
            )
            if target is not None
            else None
        )
        if font is None:
            raise SongLibraryError(
                f'song {song.name!r}: font {style.font_name!r} '
                'is not embedded in the controller'
            )
        foreground, background = graphics.colors(style, 0)
        selections = []
        for position in values:
            document, reference, origin, node = (
                bytes(64),
                bytes(64),
                0,
                roots[position],
            )
            header = compiler_wire.HEADER.unpack_from(source)
            label_offset = (
                76 + header[4] * 68 + header[5] * 68 + (first + position) * 82 + 12
            )
            label = (
                source[label_offset : label_offset + 64].split(b'\0')[0].decode('utf-8')
            )
            selections.append(
                SELECTION.pack(document, reference, label_text(label), origin, node)
            )
        records.append(
            selections[0]
            + SONG.pack(
                text(song.name, 64),
                len(selections),
                scope_roots.get((2, song.name), 0xFFFFFFFF),
                *(
                    roots[hooks[song.name, n]]
                    if (song.name, n) in hooks
                    else 0xFFFFFFFF
                    for n in ('enter', 'exit')
                ),
                foreground,
                background,
                font.id,
            )
            + b''.join(selections[1:])
        )
    sets = []
    for s in library.setlists:
        entries = [text(n, 64) for n in s.songs]
        sets.append(
            SET.pack(text(s.name, 64), len(entries), 0xFFFFFFFF) + b''.join(entries)
        )
    payload = (
        struct.pack('<II', len(library.songs), len(library.setlists))
        + b''.join(records + sets)
        + b'CSP9'
    )
    if source:
        source = (
            device_settings.attach(source, {}, target)
            if authoring is not None and target is not None
            else binary_format.wrap(source)
        )
        payload += struct.pack('<I', len(source)) + source
    if len(payload) > 0xFFFFFFFF - 20:
        raise SongLibraryError('song library exceeds the SD file format size')
    data = binary_format.wrap(payload, binary_format.Kind.songs)
    preferences = library.model_dump(
        include={'fill_in_styles', 'seed', 'background_color'},
        exclude_defaults=True,
        exclude_none=True,
    )
    for collection, field in (
        (library.songs, 'authored_styles'),
        (library.setlists, 'setlist_styles'),
    ):
        authored_styles = [
            {
                k: v
                for k, v in s.style.model_dump(exclude_none=True).items()
                if k != 'image' or v == ''
            }
            if s.style is not None
            else {}
            for s in collection
        ]
        if any(authored_styles):
            preferences[field] = authored_styles
    if song_seeds := {
        str(i): s.seed for i, s in enumerate(library.songs) if s.seed is not None
    }:
        preferences['song_seeds'] = song_seeds
    if library.fast_switches is not None:
        preferences['fast_switches'] = library.fast_switches
    if song_fast_switches := {
        str(i): s.fast_switches
        for i, s in enumerate(library.songs)
        if s.fast_switches is not None
    }:
        preferences['song_fast_switches'] = song_fast_switches
    if scene_images:
        preferences['scene_image_sources'] = {
            str(i): s.image for i, (_, s, _) in scene_images.items()
        }
    if preferences:
        document = binary_format.unpack(data, binary_format.Kind.songs)
        data = document.model_copy(
            update={
                'records': [
                    *document.records,
                    binary_format.Record(
                        tag=binary_format.SONG_FORMATTING,
                        data=tomlkit.dumps(preferences).encode(),
                    ),
                ]
            }
        ).pack()
    scene_entries = [
        scene_images.get(i, ('', None, 0))
        for i in range(max(scene_images, default=-1) + 1)
    ]
    for collection, tag, contrast_tag, display_index in (
        (
            [(s.display_name, s.style, 0) for s in library.songs],
            binary_format.SONG_ARTWORK,
            binary_format.SONG_TITLE_CONTRAST,
            1,
        ),
        (
            [(s.display_name, s.style, 0) for s in library.setlists],
            binary_format.SETLIST_ARTWORK,
            binary_format.SETLIST_TITLE_CONTRAST,
            2,
        ),
        (
            scene_entries,
            binary_format.SCENE_ARTWORK,
            binary_format.SCENE_TITLE_STYLE,
            0,
        ),
    ):
        if not any(
            (s and (s.image or display_index == 2)) or n for n, s, _ in collection
        ):
            continue
        if target is None and any(s and s.image for _, s, _ in collection):
            raise SongLibraryError('artwork requires --target')
        directory = bytearray(struct.pack('<I', len(collection)))
        pixels = bytearray()
        contrast = bytearray()
        start = 4 + song_artwork.ENTRY.size * len(collection)
        for caption, style, node_crc in collection:
            width = height = 0
            bitmap = b''
            if style and style.image:
                assert target is not None
                root = authoring.paths[0].parent if authoring else Path.cwd()
                width, height, bitmap = song_artwork.prepare(
                    root / style.image,
                    target,
                    trace.files if trace else None,
                    display=display_index,
                )
            label = label_text(caption) if caption else bytes(64)
            foreground = style.foreground_color if style else None
            mode = (
                song_artwork.title_contrast(
                    caption,
                    width,
                    height,
                    bitmap,
                    graphics.rgb565(graphics.color(foreground)) if foreground else None,
                    compact=target is not None and len(target.displays) == 1,
                )
                if bitmap and caption
                else 0
            )
            if display_index != 1:
                background = style.background_color if style else None
                contrast.extend(
                    struct.pack(
                        '<BBHH',
                        mode,
                        int(bool(foreground)) | int(bool(background)) << 1,
                        graphics.rgb565(graphics.color(foreground))
                        if foreground
                        else 0,
                        graphics.rgb565(graphics.color(background))
                        if background
                        else 0,
                    )
                )
                if display_index == 0:
                    contrast.extend(struct.pack('<I', node_crc))
            else:
                contrast.append(mode)
            directory.extend(
                song_artwork.ENTRY.pack(label, width, height, start + len(pixels))
            )
            pixels.extend(bitmap)
        document = binary_format.unpack(data, binary_format.Kind.songs)
        document.records.append(
            binary_format.Record(tag=tag, data=bytes(directory + pixels))
        )
        if any(contrast):
            document.records.append(
                binary_format.Record(tag=contrast_tag, data=bytes(contrast))
            )
        data = document.pack()
    return data


def upgrade(
    data: bytes,
    fonts: dict[int, bytes],
    *,
    target: models.Target,
    preserve_memory_estimates: bool = False,
) -> bytes:
    """Upgrade embedded setups while preserving song and node selections."""
    original = data
    data = _current_core(data)
    if data != binary_format.core(original, binary_format.Kind.songs):
        binary_format.require_structural_edit(original, binary_format.Kind.songs)
    prefix, _, _, _, sources = _parse(data)
    result = bytearray(data[:prefix])
    for source in sources:
        value = resolved.prepare_restore(
            source,
            fonts,
            target=target,
            preserve_memory_estimates=preserve_memory_estimates,
        )
        if not preserve_memory_estimates:
            value = binary_format.wrap(value)
        if len(value) != len(source):
            binary_format.require_structural_edit(original, binary_format.Kind.songs)
        result += struct.pack('<I', len(value)) + value
    upgraded = binary_format.wrap(
        binary_format.replace_core(original, bytes(result), binary_format.Kind.songs),
        binary_format.Kind.songs,
    )
    decode(upgraded)
    return upgraded


def decode(data: bytes) -> Library:
    try:
        return _decode(data)
    except (ValidationError, UnicodeDecodeError, ParseError, struct.error) as error:
        raise SongLibraryError(f'invalid song library: {error}') from error


def _decode(data: bytes) -> Library:
    _, song_headers, positions, sets, sources = _parse(data)
    context: dict[str, object] = {'instruments': {}, 'patches': {}}
    authored_styles: list[dict[str, object]] = []
    setlist_styles: list[dict[str, object]] = []
    song_seeds: dict[str, int] = {}
    song_fast_switches: dict[str, str] = {}
    artwork: list[tuple[str, str, bytes]] = []
    setlist_artwork: list[tuple[str, str, bytes]] = []
    scene_artwork: list[tuple[str, str, bytes]] = []
    scene_image_sources: dict[str, str] = {}
    if data.startswith(binary_format.MAGIC):
        for record in binary_format.unpack(data, binary_format.Kind.songs).records:
            if record.tag == binary_format.SONG_ARTWORK:
                artwork = song_artwork.unpack(record.data)
            if record.tag == binary_format.SETLIST_ARTWORK:
                setlist_artwork = song_artwork.unpack(record.data)
            if record.tag == binary_format.SCENE_ARTWORK:
                scene_artwork = song_artwork.unpack(record.data)
            if record.tag == binary_format.SONG_FORMATTING:
                preferences = tomlkit.parse(record.data.decode()).unwrap()
                authored_styles = preferences.pop('authored_styles', [])
                setlist_styles = preferences.pop('setlist_styles', [])
                scene_image_sources = preferences.pop('scene_image_sources', {})
                song_seeds = preferences.pop('song_seeds', {})
                song_fast_switches = preferences.pop('song_fast_switches', {})
                context.update(preferences)
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
        for kind, _name, root in resolved.binding_scopes(source):
            if kind == 0:
                context['bindings'] = _rebind(
                    {'bindings': resolved.binding_scope(source, root)['bindings']},
                    prefix,
                    renamed,
                    tag_names,
                )['bindings']
    context['instruments'] = instruments
    context['patches'] = definitions
    context['fonts'] = fonts
    context['tag_styles'] = tags
    if system_routes is not None:
        context['midi'] = system_routes
    if sources:
        context['definitions'] = {'profiles': profiles, 'programs': programs}
    songs = []
    for index, ((name, binding, enter, exit), values) in enumerate(
        zip(song_headers, positions, strict=True)
    ):
        patches = []
        for document, reference, label, source, node in values:
            if document[0]:
                if source != 0xFFFFFFFF:
                    raise SongLibraryError('invalid external song Patch')
                value = {
                    'document': _string(document),
                    'patch': _string(reference),
                }
                if label[0] and _string(label) != value['patch']:
                    value['display_name'] = _string(label)
            else:
                if source >= len(sources):
                    raise SongLibraryError('missing song Patch source')
                value = resolved.patch(sources[source], node)
                value = _rebind(
                    value,
                    f'source-{source}-' if len(sources) > 1 else '',
                    source_fonts[source],
                    source_tags[source],
                )
            patches.append(value)
        scope = (
            resolved.binding_scope(sources[0], binding) if binding != 0xFFFFFFFF else {}
        )
        bindings = scope.get('bindings', {})
        scope_patches = cast(list[dict[str, object]], scope.get('patches', []))
        bindings = (
            _rebind(
                {'bindings': bindings},
                'source-0-' if len(sources) > 1 else '',
                source_fonts[0],
                source_tags[0],
            )['bindings']
            if bindings
            else {}
        )
        style = (
            dict(authored_styles[index] or {}) if index < len(authored_styles) else {}
        )
        if index < len(artwork) and artwork[index][1]:
            style['image'] = artwork[index][1]
        songs.append(
            Song.model_validate(
                {
                    'name': name,
                    'seed': song_seeds.get(str(index)),
                    'fast_switches': song_fast_switches.get(str(index)),
                    'navigation': scope.get('navigation'),
                    'midi': scope_patches[0].get('midi') if scope_patches else None,
                    'display_name': artwork[index][0] or None
                    if index < len(artwork)
                    else None,
                    'patches': patches,
                    'bindings': bindings,
                    **{
                        n: _rebind(
                            resolved.patch(sources[0], r),
                            'source-0-' if len(sources) > 1 else '',
                            source_fonts[0],
                            source_tags[0],
                        )
                        for n, r in (('enter', enter), ('exit', exit))
                        if r != 0xFFFFFFFF
                    },
                    'style': style or None,
                }
            )
        )
    for index, item in enumerate(sets):
        style = dict(setlist_styles[index] or {}) if index < len(setlist_styles) else {}
        caption, image, _ = (
            setlist_artwork[index] if index < len(setlist_artwork) else ('', '', b'')
        )
        if image:
            style['image'] = image
        sets[index] = Setlist.model_validate(
            item.model_dump()
            | {'display_name': caption or None, 'style': style or None}
        )
    names = {
        original: name
        for source in sources
        for original, name in resolved.artwork_files(source)[0].items()
    }
    names.update(
        {
            path: scene_artwork[i][1]
            for n, path in scene_image_sources.items()
            if (i := int(n)) < len(scene_artwork) and scene_artwork[i][1]
        }
    )
    result = context | {'songs': [s.model_dump() for s in songs], 'setlists': sets}
    display.remap_artwork(result, names)
    return Library.model_validate(result)


def reindex(data: bytes, names: list[str]) -> bytes:
    """Relink captured setup references to the deployed bundle's sorted table."""
    core = _current_core(data)
    if core != binary_format.core(data, binary_format.Kind.songs):
        binary_format.require_structural_edit(data, binary_format.Kind.songs)
    prefix, _, _, _, sources = _parse(core)
    result = bytearray(core[:prefix])
    for source in sources:
        value = setup_bundle.reindex({'captured': source}, names)['captured']
        if value != source:
            binary_format.require_structural_edit(data, binary_format.Kind.songs)
        result += struct.pack('<I', len(value)) + value
    return binary_format.wrap(
        binary_format.replace_core(data, bytes(result), binary_format.Kind.songs),
        binary_format.Kind.songs,
    )


def _current_core(data: bytes) -> bytes:
    data = binary_format.core(data, binary_format.Kind.songs)
    if len(data) < 12:
        raise SongLibraryError('invalid song library size')
    ns, nl = struct.unpack_from('<II', data)
    for song_size, set_size, marker in (
        (SONG.size, SET.size, b'CSP9'),
        (78, SET.size, b'CSP8'),
        (73, SET.size, b'CSP7'),
        (72, SET.size, b'CSP6'),
        (68, SET.size - 4, b'CSP5'),
    ):
        offset = 8
        prefix = bytearray(data[:offset])
        valid = True
        for song, records in ((True, ns), (False, nl)):
            header_size = song_size if song else set_size
            for _ in range(records):
                first_size = SELECTION.size if song else 0
                if offset + first_size + header_size > len(data):
                    valid = False
                    break
                prefix += data[offset : offset + first_size]
                offset += first_size
                count = struct.unpack_from('<I', data, offset + 64)[0]
                if song and not count:
                    valid = False
                    break
                prefix += data[offset : offset + 68]
                prefix += (
                    data[offset + 68 : offset + 72]
                    if marker != b'CSP5'
                    else struct.pack('<I', 0xFFFFFFFF)
                )
                if song:
                    prefix += (
                        data[offset + 72 : offset + 80]
                        if marker == b'CSP9'
                        else struct.pack('<II', 0xFFFFFFFF, 0xFFFFFFFF)
                    )
                    prefix += (
                        data[
                            offset + (80 if marker == b'CSP9' else 73) : offset
                            + song_size
                        ]
                        if marker in (b'CSP8', b'CSP9')
                        else struct.pack('<HHB', 0, 65535, 17)
                    )
                offset += header_size
                size = (count - 1) * SELECTION.size if song else count * 64
                if size > len(data) - offset:
                    valid = False
                    break
                prefix += data[offset : offset + size]
                offset += size
            if not valid:
                break
        if valid and data[offset : offset + 4] == marker:
            return (
                data
                if marker == b'CSP9'
                else bytes(prefix) + b'CSP9' + data[offset + 4 :]
            )
    raise SongLibraryError('invalid song records or missing song Patch sources')


def _parse(
    data: bytes,
) -> tuple[
    int,
    list[tuple[str, int, int, int]],
    list[list[tuple[bytes, bytes, bytes, int, int]]],
    list[Setlist],
    list[bytes],
]:
    data = _current_core(data)
    if len(data) < 12:
        raise SongLibraryError('invalid song library size')
    ns, nl = struct.unpack_from('<II', data)
    if ns > (len(data) - 12) // (SONG.size + SELECTION.size):
        raise SongLibraryError('invalid song library counts')
    offset = 8
    positions = []
    song_headers = []
    for _ in range(ns):
        if offset + SELECTION.size + SONG.size > len(data):
            raise SongLibraryError('truncated song')
        first = SELECTION.unpack_from(data, offset)
        offset += SELECTION.size
        (
            name,
            count,
            binding,
            enter,
            exit,
            _foreground,
            _background,
            _font,
        ) = SONG.unpack_from(data, offset)
        offset += SONG.size
        if count < 1 or count - 1 > (len(data) - offset) // SELECTION.size:
            raise SongLibraryError('invalid song patch count')
        values = [first]
        for _ in range(count - 1):
            values.append(SELECTION.unpack_from(data, offset))
            offset += SELECTION.size
        positions.append(values)
        song_headers.append((_string(name), binding, enter, exit))
    sets = []
    for _ in range(nl):
        if offset + SET.size > len(data):
            raise SongLibraryError('truncated setlist')
        name, count, reserved = SET.unpack_from(data, offset)
        if reserved != 0xFFFFFFFF:
            raise SongLibraryError(
                f'setlist {_string(name)!r} has bindings. Please '
                'remove them and recompile the library'
            )
        offset += SET.size
        if count > (len(data) - offset) // 64:
            raise SongLibraryError('invalid setlist count')
        entries = [
            _string(data[offset + j * 64 : offset + (j + 1) * 64]) for j in range(count)
        ]
        offset += count * 64
        sets.append(Setlist(name=_string(name), songs=entries))
    if data[offset : offset + 4] != b'CSP9':
        raise SongLibraryError('missing song Patch sources')
    offset += 4
    prefix = offset
    sources = []
    while offset < len(data):
        if offset + 4 > len(data):
            raise SongLibraryError('truncated song Patch source')
        length = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        if length > len(data) - offset:
            raise SongLibraryError('truncated song Patch source')
        sources.append(data[offset : offset + length])
        offset += length
    if not sources and any(r != 0xFFFFFFFF for _, *v in song_headers for r in v):
        raise SongLibraryError('missing binding-map source')
    return prefix, song_headers, positions, sets, sources


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


def asset_references(data: bytes) -> dict[str, list[str]]:
    """Include assets belonging to every captured setup, not just selected nodes."""
    result: dict[str, list[str]] = {'fonts': [], 'movies': [], 'midi': []}
    for source in _parse(data)[4]:
        settings = models.Setup.model_validate(
            tomlkit.parse(resolved.export(source)).unwrap()
        )
        for category, paths in (
            ('fonts', list(settings.fonts.values())),
            ('movies', settings.splash_movie + settings.idle_movie),
            ('midi', midi_assets.referenced({'captured': source})),
        ):
            for path in paths:
                relative = f'midi/{path}' if category == 'midi' else path
                if relative not in result[category]:
                    result[category].append(relative)
    return {k: sorted(v) for k, v in result.items()}


def source_snapshots(data: bytes) -> set[str]:
    return {
        n for s in _parse(data)[4] if (n := source_archive.identifier(s)) is not None
    }


def download(client: transport.SetupClient, *, undo: bool = False) -> bytes:
    prefix = 'CNTRL SONGS UNDO' if undo else 'CNTRL SONGS'
    response = client.request(prefix + ' SIZE').split()
    if undo and response == ['CNTRL', 'SONGS', 'UNDO', 'NONE']:
        return b''
    if len(response) != 4 or response[:2] != ['CNTRL', 'SONGS']:
        raise transport.UsbReplyError('invalid song library response')
    try:
        length, checksum = int(response[2]), int(response[3], 16)
    except ValueError as error:
        raise transport.UsbReplyError('invalid song library response') from error
    if not 12 <= length <= 0xFFFFFFFF - 20 or not 0 <= checksum <= 0xFFFFFFFF:
        raise transport.UsbReplyError('invalid song library response')
    result = bytearray()
    while len(result) < length:
        chunk = client.request_data(f'{prefix} READ {len(result)}')
        if not 1 <= len(chunk) <= client.connection.usb_block_size:
            raise transport.UsbReplyError('invalid song library chunk')
        result.extend(chunk)
    if len(result) != length or crc32(result) != checksum:
        raise transport.UsbReplyError('song library checksum mismatch')
    return bytes(result)


def upload(client: transport.SetupClient, data: bytes, overwrite: bool) -> None:
    """Upload an exact current-format library and verify the stored bytes."""
    decode(data)
    data = binary_format.wrap(data, binary_format.Kind.songs)
    if data.startswith(binary_format.MAGIC):
        client.require_format(binary_format.Kind.songs)
    client.ok(f'CNTRL SONGS BEGIN {len(data)} {crc32(data):08x} {int(overwrite)}')
    for offset in range(0, len(data), client.connection.usb_block_size):
        client.send_data(
            Kind.songs, data[offset : offset + client.connection.usb_block_size]
        )
    client.ok('CNTRL SONGS END')
    if download(client) != data:
        raise transport.UsbReplyError('song library read-back mismatch')


def run(command: New | Copy | Download) -> None:
    from . import cli
    from .setup_compiler import document_references

    if isinstance(command, New):
        name = command.document.stem
        text(name, 64)
        for scene in command.scenes:
            text(scene, 64)
        source = tomlkit.dumps(
            {
                'patches': {
                    s: {'display_name': s, 'patches': [], 'bindings': {}}
                    for s in command.scenes
                },
                'songs': [{'name': name, 'patches': command.scenes}],
            }
        )
        try:
            with command.document.open('x', encoding='utf-8') as file:
                file.write(source)
        except OSError as error:
            raise SongLibraryError(f'{command.document}: {error}') from error
        return
    if isinstance(command, Copy):
        trace = data_files.Trace()
        try:
            path = command.document.resolve()
            has_session = (path.parent / 'cntrl-session.toml').exists()
        except OSError as error:
            raise SongLibraryError(f'{command.document}: {error}') from error
        authoring = data_files.DataFile(name='Songs', paths=[path])
        recorded = None
        if has_session:
            recorded = source_archive.recovered_recipe(path.parent)
            authoring = data_files.expressions(recorded.document)[0]
        bindings, relocations = source_archive.session_context(authoring)
        library = _read_authoring(path, Library, trace, relocations)
        target_path = command.target
        if target_path is None and recorded is not None:
            target_path = data_files.expressions(recorded.target)[0].paths[0]
        target = (
            _read_authoring(target_path, models.Target, trace, relocations)
            if target_path is not None
            else None
        )
        errors: list[str] = []
        context: dict[str, object] = {}
        if library.songs or library.patches or library.midi is not None:
            context = global_config.resolve_setup(
                library.model_dump(
                    exclude={'songs', 'setlists'}, exclude_unset=True, exclude_none=True
                ),
                authoring,
                errors,
                trace=trace,
                defaults=bindings,
                relocations=relocations,
            )
        companion_names = list(
            dict.fromkeys([*references(library), *document_references(context)])
        )
        original_name = authoring.name
        index = 1
        while authoring.name in companion_names:
            authoring = authoring.model_copy(update={'name': f'Songs{index}'})
            index += 1
        if authoring.name != original_name:
            trace.defaults[authoring.name] = trace.defaults.pop(original_name)
            trace.roots[authoring.name] = trace.roots.pop(original_name)
            for entries in (trace.locations, trace.inherited):
                if original_name in entries:
                    entries[authoring.name] = entries.pop(original_name)
            for consumers in trace.consumers.values():
                if original_name in consumers:
                    consumers[consumers.index(original_name)] = authoring.name
        companions: dict[str, object] = {}
        index = 0
        while index < len(companion_names):
            name = companion_names[index]
            companion = data_files.DataFile(
                name=name, paths=[path.parent / (name + '.toml')]
            )
            if recorded is not None:
                companion = next(
                    (
                        e
                        for e in data_files.expressions(recorded.document)
                        if e.name == name
                    ),
                    companion,
                )
            companions[name] = global_config.load_setup(
                companion,
                True,
                errors,
                trace=trace,
                overrides=trace.files,
                defaults={name: bindings.get(name, trace.defaults[authoring.name])},
            )
            for reference in document_references(companions[name]):
                if reference not in companion_names:
                    validate_name(reference)
                    companion_names.append(reference)
            index += 1
        if errors:
            raise compiler_errors.CompilationError(errors)
        assets: dict[int, bytes] = {}
        movies: dict[int, bytes] = {}
        if companion_names and target is None:
            raise SongLibraryError('reference imports require --target')
        with transport.SerialConnection(command.port) as connection:
            client = transport.SetupClient(connection)
            if not command.overwrite:
                existing = decode(download(client))
                if existing.songs or existing.setlists:
                    raise SongLibraryError('song library exists; use --overwrite')
            current_bundle = client.download_bundle() if companion_names else b''
            compiled = setup_bundle.unpack(current_bundle) if current_bundle else {}
            known = {n.lower() for n in compiled}
            if not command.overwrite and any(
                n.lower() in known for n in companion_names
            ):
                raise SongLibraryError('companion setup exists; use --overwrite')
            # The execution container and its companions use the complete deployed
            # index table, including setups unrelated to this song import.
            documents: dict[str, object] = {n: {} for n in compiled} | companions
            data = encode(
                library,
                target,
                documents,
                assets,
                authoring=authoring,
                trace=trace,
                movie_assets=movies,
            )
            if companion_names:
                assert target is not None
                imported = {
                    n: compiler.compile_show(
                        s,
                        {},
                        [],
                        target,
                        assets,
                        documents,
                        movies,
                        source_snapshots=trace.files,
                        setting_overrides=trace.setting_overrides[n],
                    )
                    for n, s in companions.items()
                }
                combined = setup_bundle.reindex(compiled | imported)
                sources: dict[str, object] = {
                    n: tomlkit.parse(resolved.export(d)).unwrap()
                    for n, d in combined.items()
                }
                margin = (
                    setup_bundle.HEADER.unpack_from(
                        binary_format.core(current_bundle, binary_format.Kind.bundle)
                    )[5]
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
                    previous=current_bundle or None,
                )
            prefix, _, _, _, embedded = _parse(data)
            captured = ({authoring.name: embedded[0]} if embedded else {}) | (
                {n: bundle.setups[n] for n in companion_names}
                if companion_names
                else {}
            )
            midi = midi_assets.collect(captured, trace.files)
            archive = None
            if captured:
                expressions = [
                    f'{authoring.name}={path}',
                    *[f'{n}={trace.roots[n]}' for n in companion_names],
                ]
                recipe = data_files.Sources(
                    document=expressions,
                    target=[str(target_path.resolve())]
                    if target_path is not None
                    else [],
                    global_defaults=trace.defaults,
                )
                archive = source_archive.create(recipe, trace, captured, kind='songs')
                if embedded:
                    source = source_archive.attach(embedded[0], archive)
                    data = binary_format.replace_core(
                        data,
                        _current_core(data)[:prefix]
                        + struct.pack('<I', len(source))
                        + source,
                        binary_format.Kind.songs,
                    )
                if companion_names:
                    bundle = bundle.model_copy(
                        update={
                            'setups': {
                                n: source_archive.attach(v, archive)
                                if n in companions
                                else v
                                for n, v in bundle.setups.items()
                            }
                        }
                    )
            for key, font in assets.items():
                client.copy_font(key, font)
            for movie in movies.values():
                client.copy_movie(movie)
            for name, value in midi.items():
                client.copy_midi(name, value, overwrite=command.overwrite)
            if archive is not None:
                source_archive.upload(client, archive)
            if companion_names:
                client.copy_bundle(bundle.pack(), overwrite=True)
            upload(client, data, command.overwrite)
            if (contact := device_globals.saved_contact()) is not None:
                device_globals.sync_contact(contact, client=client)
        print('Song library copied and verified')
    else:
        with transport.SerialConnection(command.port) as connection:
            client = transport.SetupClient(connection)
            data = download(client)
            library = decode(data)
            companion_names = references(library)
            files = (
                cli.download_files(client, companion_names) if companion_names else {}
            )
            if data.startswith(binary_format.MAGIC):
                for record in binary_format.unpack(
                    data, binary_format.Kind.songs
                ).records:
                    if record.tag in (
                        binary_format.SONG_ARTWORK,
                        binary_format.SETLIST_ARTWORK,
                        binary_format.SCENE_ARTWORK,
                    ):
                        files.update(
                            {
                                Path(n): b
                                for _, n, b in song_artwork.unpack(record.data)
                                if n
                            }
                        )
            for source in _parse(data)[4]:
                files.update(resolved.artwork_files(source)[1])
                if (snapshot := source_archive.identifier(source)) is not None:
                    manifest, entries = source_archive.read(
                        client.download_source(snapshot)
                    )
                    if manifest.kind == 'songs':
                        name = data_files.expressions(manifest.recipe.document)[0].name
                    else:
                        matches = [
                            n
                            for n, p in manifest.baselines.items()
                            if resolved.export(entries[p]) == resolved.export(source)
                        ]
                        if len(matches) != 1:
                            raise SongLibraryError(
                                'cannot identify the archived setup '
                                'for this captured song source'
                            )
                        name = matches[0]
                    files.update(source_archive.download(client, {name: source}))
            for path in library.fonts.values():
                if Path(path) not in files:
                    files[Path(path)] = client.download_font(int(Path(path).stem, 16))
        content = tomlkit.dumps(library.model_dump(exclude_none=True)).encode()
        if Path('songs.toml') in files and files[Path('songs.toml')] != content:
            raise SongLibraryError(
                'songs.toml: conflicts with a downloaded setup or MIDI file'
            )
        files[Path('songs.toml')] = content
        cli.publish_download(files, command.directory)


def _read_authoring[Document: BaseModel](
    path: Path,
    model: type[Document],
    trace: data_files.Trace,
    relocations: dict[Path, Path],
) -> Document:
    try:
        data = compiler_inputs.read_document(
            path, snapshots=trace.files, relocations=relocations
        )
    except (OSError, UnicodeDecodeError, JSONDecodeError, ParseError) as error:
        raise SongLibraryError(f'{path}: {error}') from error
    try:
        if model is Library and isinstance(data, dict):
            entries = cast(dict[str, object], data).get('songs')
            if (
                isinstance(entries, list)
                and len(entries) == 1
                and isinstance(entries[0], dict)
            ):
                origin = next(
                    (p for p, local in relocations.items() if local == path.resolve()),
                    path,
                )
                cast(dict[str, object], entries[0]).setdefault('name', origin.stem)
        return model.model_validate(data)
    except ValidationError as error:
        raise compiler_errors.CompilationError(
            compiler_errors.validation_errors(str(path), error)
        ) from error


SELECTION = struct.Struct('<64s64s64sII')
SONG = struct.Struct('<64sIIIIHHB')
SET = struct.Struct('<64sII')
