"""Songs serialize both selection kinds and exchange with the real C++ receiver."""

import io
import struct
import sys
from contextlib import nullcontext
from pathlib import Path
from unittest.mock import Mock
from zlib import crc32

import pytest
from native_peer import NativePeer
from PIL import Image

from cntrl.patch_chooser import (
    binary_format,
    cli,
    compiler_errors,
    compiler_inputs,
    data_files,
    formatting,
    global_config,
    graphics,
    models,
    song_artwork,
    songs,
    transport,
)
from scripts import native_tests


@pytest.mark.parametrize(
    ('title_background', 'mode'),
    [
        (0xFFFF, song_artwork.TitleContrast.black),
        (0x0000, song_artwork.TitleContrast.white),
    ],
)
def test_automatic_title_color_uses_the_pixels_under_the_letters(
    title_background: int, mode: song_artwork.TitleContrast
) -> None:
    outer = struct.pack('<H', title_background ^ 0xFFFF)
    inner = struct.pack('<H', title_background)
    row = outer * 48 + inner * 64 + outer * 48
    bitmap = outer * (160 * 20) + row * 24 + outer * (160 * 20)
    assert (
        song_artwork.title_contrast('Title', 160, 64, bitmap, None, compact=False)
        == mode
    )


def test_new_song_reuses_scene_definitions(
    tmp_path: Path, target: models.Target
) -> None:
    path = tmp_path / 'First Light.toml'
    command = songs.New(document=path, scenes=['Verse', 'Chorus', 'Verse'])
    songs.run(command)
    library = songs.Library.model_validate(compiler_inputs.read_document(path))
    assert list(library.patches) == ['Verse', 'Chorus']
    restored = songs.decode(songs.encode(library, target))
    assert restored.songs[0].patches[0] == restored.songs[0].patches[2]
    with pytest.raises(songs.SongLibraryError, match='exists'):
        songs.run(command)


def test_repeated_scenes_keep_song_presentation_and_navigation(
    tmp_path: Path, target: models.Target
) -> None:
    source = Path('examples/patch-chooser/scenes.toml')
    values = compiler_inputs.read_document(source)
    values['songs'][0].update(
        display_name='Light', image='art.png', navigation={'page_forward': 'D+E'}
    )
    values['patches']['chorus']['navigation'] = {'page_back': 'A+B'}
    image = Image.new('RGB', (20, 8), 'red')
    image.save(tmp_path / 'art.png')
    encoded = songs.encode(
        songs.Library.model_validate(values),
        target,
        authoring=data_files.DataFile(name='Scenes', paths=[tmp_path / source.name]),
    )
    recovered = songs.decode(encoded)
    song = recovered.songs[0]
    assert song.display_name == 'Light'
    assert song.image and song.image.endswith('.png')
    assert song.navigation == models.PageNavigation(page_forward='D+E')
    assert song.patches[0] == song.patches[2]
    assert recovered.patches['chorus']['navigation'] == {'page_back': 'A+B'}


def library() -> songs.Library:
    return songs.Library(
        instruments={
            'piano': {
                'instrument': 'Piano',
                'profile': 'default',
                'channel': 2,
                'output': 'din',
            }
        },
        definitions=models.Definitions(
            profiles=[models.InstrumentProfile(instrument='Piano', name='default')]
        ),
        songs=[
            songs.Song(
                name='Simple',
                patches=[
                    {
                        'type': 'program-change',
                        'instrument': 'piano',
                        'program': 34,
                    }
                ],
            ),
            songs.Song(
                name='Linked',
                patches=[
                    {
                        'document': 'pman',
                        'patch': 'clean',
                    }
                ],
            ),
        ],
        setlists=[songs.Setlist(name='Tonight', songs=['Simple', 'Linked', 'Simple'])],
    )


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler_inputs.read_document(
            Path(__file__).resolve().parents[1] / 'firmware/test/target.json'
        )
    )


def test_inline_and_reference_selections_and_repeated_occurrences_round_trip(
    target: models.Target,
) -> None:
    data = songs.encode(
        library(),
        target,
        COMPANIONS,
    )
    restored = songs.decode(data)
    assert restored.songs == library().songs
    assert restored.setlists[0].songs == ['Simple', 'Linked', 'Simple']
    assert (
        songs.encode(
            restored,
            target,
            COMPANIONS,
        )
        == data
    )


def test_names_are_unique(target: models.Target) -> None:
    value = library()
    for field, items in (('songs', value.songs), ('setlists', value.setlists)):
        with pytest.raises(ValueError, match='unique'):
            songs.encode(value.model_copy(update={field: [items[0], items[0]]}), target)


def test_songs_fill_missing_styles_with_the_cycle_formatter(
    target: models.Target,
) -> None:
    device = target.model_copy(
        update={
            'fonts': [
                *target.fonts,
                target.fonts[0].model_copy(update={'name': 'CourierPrime', 'id': 8}),
            ]
        }
    )
    value = library().model_copy(update={'seed': 42})
    first = value.songs[0].model_copy(
        update={'style': models.Style(foreground_color='red'), 'seed': 7}
    )
    value = value.model_copy(update={'songs': [first, value.songs[1]]})
    expected = formatting.styles(
        models.Setup(seed=42), [(s.name, s.seed) for s in value.songs], device, 'song'
    )
    data = songs.encode(value, device, COMPANIONS)
    restored = songs.decode(data)
    assert restored.seed == 42 and restored.fill_in_styles
    assert restored.songs == value.songs
    core = binary_format.core(data, binary_format.Kind.songs)
    offset = 8
    graphics_by_name: dict[str, tuple[int, int, int]] = {}
    for original in value.songs:
        header = songs.SONG.unpack_from(core, offset + songs.SELECTION.size)
        graphics_by_name[original.name] = header[5:8]
        generated = expected[original.name, original.seed]
        assert header[7] == next(
            f.id for f in device.fonts if f.name == generated.font_name
        )
        assert header[6] == graphics.colors(generated, 0)[1]
        assert header[5] == graphics.rgb565(
            graphics.color(
                original.style.foreground_color
                if original.style is not None
                else generated.foreground_color
            )
        )
        offset += songs.SELECTION.size * len(original.patches) + songs.SONG.size
    assert songs.encode(restored, device, COMPANIONS) == data
    regrouped = value.model_copy(
        update={
            'songs': list(reversed(value.songs)),
            'setlists': [songs.Setlist(name='Other', songs=['Simple'])],
        }
    )
    reordered = binary_format.core(
        songs.encode(regrouped, device, COMPANIONS), binary_format.Kind.songs
    )
    offset = 8
    for original in regrouped.songs:
        header = songs.SONG.unpack_from(reordered, offset + songs.SELECTION.size)
        assert header[5:8] == graphics_by_name[original.name]
        offset += songs.SELECTION.size * len(original.patches) + songs.SONG.size
    changed = restored.model_copy(update={'background_color': '0xE8F0FF'})
    updated = binary_format.core(
        songs.encode(changed, device, COMPANIONS), binary_format.Kind.songs
    )
    header = songs.SONG.unpack_from(updated, 8 + songs.SELECTION.size)
    assert header[5] == graphics.rgb565(graphics.color('red'))
    assert header[6] == graphics.rgb565(graphics.color('0xE8F0FF'))


def test_song_styles_can_disable_automatic_formatting(target: models.Target) -> None:
    value = library().model_copy(update={'fill_in_styles': False})
    data = songs.encode(value, target, COMPANIONS)
    restored = songs.decode(data)
    assert not restored.fill_in_styles
    assert restored.songs == value.songs
    core = binary_format.core(data, binary_format.Kind.songs)
    assert songs.SONG.unpack_from(core, 8 + songs.SELECTION.size)[5:8] == (0, 65535, 17)
    assert songs.encode(restored, target, COMPANIONS) == data


def test_song_random_seed_is_preserved_for_recompilation(target: models.Target) -> None:
    data = songs.encode(library().model_copy(update={'seed': None}), target, COMPANIONS)
    restored = songs.decode(data)
    assert restored.seed is not None
    assert songs.encode(restored, target, COMPANIONS) == data


def test_song_authoring_inherits_the_rig_without_selecting_an_inherited_cycle(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, target: models.Target
) -> None:
    defaults = tmp_path / 'global.toml'
    defaults.write_text(
        'seed=912\n[instruments.piano]\noutput="din"\nchannel=3\n'
        '[patch_cycles]\nEarlier=[{instrument="piano",program=12}]\n'
    )
    monkeypatch.setattr(global_config, 'user_path', lambda: defaults)
    source = tmp_path / 'songs.toml'
    source.write_text(
        '[[songs]]\nname="Live"\n'
        'patches=[{instrument="piano",program=34,display_name="Chosen"}]\n'
    )
    value = songs.Library.model_validate(compiler_inputs.read_document(source))
    result = songs.decode(
        songs.encode(
            value,
            target,
            authoring=data_files.DataFile(name='Songs', paths=[source]),
            trace=data_files.Trace(files={source: source.read_bytes()}),
        )
    )
    assert result.songs[0].name == 'Live'
    assert result.songs[0].patches[0]['program'] == 34
    assert result.songs[0].patches[0]['display_name'] == 'Chosen'
    assert result.instruments['piano']['channel'] == 3
    assert result.seed == 912
    # Low-level encoding of an already resolved library must not read host defaults.
    with pytest.raises(
        compiler_errors.CompilationError, match='unknown or invalid instrument'
    ):
        songs.encode(value, target)


def test_setlists_store_names_without_resolving_them(target: models.Target) -> None:
    value = library().model_copy(
        update={'setlists': [songs.Setlist(name='Later', songs=['Missing'])]}
    )
    assert (
        songs.decode(songs.encode(value, target, COMPANIONS)).setlists == value.setlists
    )


@pytest.mark.parametrize('alias', ['piano', 'p' * 31])
def test_captured_sources_with_colliding_instrument_aliases_export_independently(
    target: models.Target,
    alias: str,
) -> None:
    first = library().model_copy(
        update={
            'instruments': {alias: library().instruments['piano']},
            'songs': [
                songs.Song(
                    name='First',
                    patches=[
                        {
                            'type': 'program-change',
                            'instrument': alias,
                            'program': 34,
                        }
                    ],
                )
            ],
            'setlists': [],
        }
    )
    second = first.model_copy(
        update={
            'instruments': {
                alias: {
                    'instrument': 'Piano',
                    'profile': 'default',
                    'channel': 3,
                    'output': 'din',
                }
            },
            'songs': [
                songs.Song(
                    name='Other',
                    patches=[
                        {
                            'type': 'program-change',
                            'instrument': alias,
                            'program': 35,
                        }
                    ],
                )
            ],
        }
    )
    left, right = (
        binary_format.core(songs.encode(v, target), binary_format.Kind.songs)
        for v in (first, second)
    )
    end = 8 + songs.SELECTION.size + songs.SONG.size
    position = list(songs.SELECTION.unpack_from(right, 8))
    position[3] = 1
    merged = (
        struct.pack('<II', 2, 0)
        + left[8:end]
        + songs.SELECTION.pack(*position)
        + right[8 + songs.SELECTION.size : end]
        + b'CSP9'
        + left[end + 4 :]
        + right[end + 4 :]
    )
    restored = songs.decode(merged)
    keys = list(restored.instruments)
    assert all(len(k) <= 31 for k in keys)
    assert restored.instruments[keys[0]]['channel'] == 2
    assert restored.instruments[keys[1]]['channel'] == 3
    assert restored.songs[0].patches[0]['instrument'] == keys[0]
    assert restored.songs[1].patches[0]['instrument'] == keys[1]
    assert songs.decode(songs.encode(restored, target)).songs == restored.songs


def test_nested_stock_return_finishes_its_position_before_advancing(
    tmp_path: Path, target: models.Target
) -> None:
    target = target.model_copy(
        update={'functions': ['stock-firmware'], 'buttons': list('ABCDEF')}
    )
    selection = songs.Library(
        songs=[
            songs.Song(
                name='Nested visit',
                patches=[
                    {
                        'patches': [
                            {
                                'type': 'control-change',
                                'output': 'din',
                                'channel': 1,
                                'controller': 64,
                                'value': 1,
                            },
                            {
                                'patches': [
                                    {
                                        'operation': 'brightness',
                                        'value': 150,
                                    },
                                    {
                                        'operation': 'stock-firmware',
                                        'exit_buttons': 'A+F',
                                    },
                                    {
                                        'type': 'control-change',
                                        'output': 'din',
                                        'channel': 1,
                                        'controller': 65,
                                        'value': 2,
                                    },
                                ],
                            },
                            {
                                'type': 'control-change',
                                'output': 'din',
                                'channel': 1,
                                'controller': 66,
                                'value': 3,
                            },
                        ],
                    },
                    {
                        'type': 'control-change',
                        'output': 'din',
                        'channel': 1,
                        'controller': 67,
                        'value': 4,
                    },
                ],
            )
        ],
        setlists=[songs.Setlist(name='Set', songs=['Nested visit'])],
    )
    data = songs.encode(selection, target)
    assert songs.encode(songs.decode(data), target) == data
    image = tmp_path / 'songs.bin'
    image.write_bytes(data)
    root = Path(__file__).resolve().parents[1]
    executable = tmp_path / 'song-executor'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_song_executor.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    native_tests.run_native([str(executable), str(image)], check=True)
    original = selection.songs[0]
    hooked = original.model_copy(
        update={
            'enter': original.patches[0],
            'patches': original.patches[1:],
            'exit': {
                'patches': [
                    {'output': 'din', 'channel': 1, 'controller': 68, 'value': 1},
                    {'operation': 'stock-firmware', 'exit_buttons': 'A+F'},
                    {'output': 'din', 'channel': 1, 'controller': 69, 'value': 1},
                ]
            },
        }
    )
    image.write_bytes(
        songs.encode(selection.model_copy(update={'songs': [hooked]}), target)
    )
    native_tests.run_native([str(executable), str(image), '--hooks'], check=True)


@pytest.mark.parametrize('large', [False, True])
def test_song_library_transfer_against_cpp_receiver(
    tmp_path: Path, large: bool, target: models.Target
) -> None:
    root = Path(__file__).resolve().parents[1]
    peer = tmp_path / 'songs-peer'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/songs_server.cpp'),
            '-o',
            str(peer),
        ],
        check=True,
    )
    with NativePeer([str(peer)]) as port:
        value = library()
        if large:
            value = songs.Library(
                songs=[
                    songs.Song(
                        name=f'Song {i}',
                        patches=[
                            {
                                'type': 'control-change',
                                'output': 'din',
                                'channel': 1,
                                'controller': 64,
                                'value': 127,
                            }
                        ],
                    )
                    for i in range(130)
                ],
                setlists=[
                    songs.Setlist(
                        name=f'Set {i}', songs=[f'Song {j}' for j in range(130)]
                    )
                    for i in range(20)
                ],
            )
        data = songs.encode(
            value,
            target,
            COMPANIONS,
        )
        assert (
            songs.encode(
                songs.decode(data),
                target,
                COMPANIONS,
            )
            == data
        )
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            client.ok(f'CNTRL SONGS BEGIN {len(data)} {crc32(data):08x} 0')
            for i in range(0, len(data), 128):
                client.send_data(transport.Kind.songs, data[i : i + 128])
            client.ok('CNTRL SONGS END')
            assert songs.download(client) == data
            with pytest.raises(ValueError, match='songs'):
                client.ok('CNTRL SONGS END')
            with pytest.raises(ValueError, match='songs'):
                client.ok(f'CNTRL SONGS BEGIN {len(data)} {crc32(data):08x} 0')
            assert songs.download(client) == data
            # Reject counts that would read beyond the payload.
            malformed = bytearray(data)
            struct.pack_into('<I', malformed, 8 + songs.SELECTION.size + 64, 0xFFFFFFFF)
            client.ok(f'CNTRL SONGS BEGIN {len(malformed)} {crc32(malformed):08x} 1')
            for i in range(0, len(malformed), 128):
                client.send_data(transport.Kind.songs, malformed[i : i + 128])
            with pytest.raises(ValueError, match='songs'):
                client.ok('CNTRL SONGS END')
            assert songs.download(client) == data
            # An incomplete replacement cannot change the committed library.
            client.ok(f'CNTRL SONGS BEGIN {len(data)} 00000000 1')
            client.send_data(transport.Kind.songs, data[:128])
            with pytest.raises(ValueError, match='songs'):
                client.ok('CNTRL SONGS END')
            assert songs.download(client) == data


COMPANIONS = {'pman': {'patches': {'clean': {'type': 'start', 'output': 'din'}}}}


@pytest.mark.parametrize('explicit', [False, True])
def test_song_can_start_with_a_derived_patch_and_reuse_its_parent(
    target: models.Target, explicit: bool
) -> None:
    base = {'type': 'program-change', 'instrument': 'piano', 'program': 7, 'volume': 80}
    variation: dict[str, object] = {'parent': 'A', 'volume': 100, 'display_name': "A'"}
    library = songs.Library.model_validate(
        {
            'instruments': {'piano': {'output': 'din', 'channel': 2}},
            'patches': {
                'A′': variation | ({'type': 'derived'} if explicit else {}),
                'A': base,
            },
            'songs': [
                {
                    'name': 'Example',
                    'patches': [{'patch': k} for k in ('A′', 'A', 'A′')],
                }
            ],
        }
    )
    data = songs.encode(library, target)
    restored = songs.decode(data)
    assert restored.patches['A′'] == variation
    assert [p['patch'] for p in restored.songs[0].patches] == ['A′', 'A', 'A′']
    assert songs.encode(restored, target) == data

    for program in (7, 12):
        base['program'] = program
        restored.patches['A'] = base
        data = songs.encode(restored, target)
        # The browser records in the embedded setup are the executable selections.
        source = data[data.index(b'CSW1') :]
        start = 76 + 68 + 68  # One instrument and one patch cycle.
        assert [
            (source[start + i * 82 + 4] + 1, source[start + i * 82 + 7])
            for i in range(3)
        ] == [(program, 100), (program, 80), (program, 100)]


def test_empty_song_library_downloads() -> None:
    data = songs.encode(songs.Library())
    client = Mock(spec=transport.SetupClient)
    client.connection = Mock(usb_block_size=4096)
    client.request.return_value = f'CNTRL SONGS {len(data)} {crc32(data):08x}'
    client.request_data.return_value = data
    assert songs.download(client) == data
    assert not songs.decode(data).songs


@pytest.mark.parametrize(
    'reply', ['CNTRL SONGS n/a 0', 'CNTRL SONGS 64 xx', 'CNTRL SONGS 64 -1']
)
def test_bad_song_library_header_stops_before_reading_payload(reply: str) -> None:
    client = Mock(spec=transport.SetupClient)
    client.request.return_value = reply
    with pytest.raises(transport.UsbReplyError, match='invalid song library response'):
        songs.download(client)
    client.request_data.assert_not_called()


@pytest.mark.parametrize('bad_document', [None, '[invalid'])
def test_song_copy_reports_a_bad_source_before_opening_usb(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, bad_document: str | None
) -> None:
    path = tmp_path / 'library.toml'
    if bad_document is not None:
        path.write_text(bad_document)
    connect = Mock(side_effect=AssertionError('unexpected USB open'))
    monkeypatch.setattr(transport, 'SerialConnection', connect)
    monkeypatch.setattr(
        sys, 'argv', ['cntrl', 'songs', 'copy', str(path), '--port', '/unused']
    )
    with pytest.raises(SystemExit, match='library.toml'):
        cli.main()
    connect.assert_not_called()


@pytest.mark.parametrize('error_type', [ValueError, OSError])
def test_song_cli_preserves_unexpected_failures(
    monkeypatch: pytest.MonkeyPatch, error_type: type[Exception]
) -> None:
    failure = error_type('unexpected song library failure')
    monkeypatch.setattr(songs, 'run', Mock(side_effect=failure))
    monkeypatch.setattr(
        sys, 'argv', ['cntrl', 'songs', 'download', 'output', '--port', '/unused']
    )
    with pytest.raises(error_type) as caught:
        cli.main()
    assert caught.value is failure


def test_downloaded_song_source_preserves_unicode(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    from cntrl.patch_chooser import cli

    monkeypatch.setattr(
        io, 'text_encoding', lambda encoding, *args: encoding or 'cp1252'
    )
    original = library()
    value = original.model_copy(
        update={
            'songs': [
                songs.Song(
                    name='Simple',
                    patches=[original.songs[0].patches[0] | {'display_name': 'Café А'}],
                )
            ],
            'setlists': [],
        }
    )
    monkeypatch.setattr(
        cli.transport, 'SerialConnection', lambda *a, **k: nullcontext(Mock())
    )
    data = songs.encode(songs.Library())
    monkeypatch.setattr(songs, 'download', lambda client: data)
    monkeypatch.setattr(songs, 'decode', lambda data: value)
    songs.run(songs.Download(directory=tmp_path, port=Path('unused')))
    path = tmp_path / 'songs.toml'
    assert 'Café А'.encode() in path.read_bytes()
    assert songs.Library.model_validate(compiler_inputs.read_document(path)) == value
