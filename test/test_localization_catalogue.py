"""Language packs preserve semantic messages and validated typed placeholders."""

import struct
import subprocess
from pathlib import Path
from zlib import crc32

import pytest

from cntrl.fonts import assets, status
from cntrl.localization import catalogue
from scripts import native_tests


def test_french_catalogue_is_complete_and_header_is_reproducible(
    tmp_path: Path,
) -> None:
    source = catalogue.load()
    assert all(m.fr and m.context for m in source.messages.values())
    header = tmp_path / 'messages.h'
    catalogue.generate(header)
    assert (
        header.read_bytes()
        == (ROOT / 'firmware/include/cntrl/ui/messages.h').read_bytes()
    )


@pytest.mark.parametrize(
    'french',
    ['{unknown:u}', '{value:s}', '{value:u} {value:u}', '{value!r}', ''],
)
def test_invalid_translation_templates_are_rejected(french: str) -> None:
    with pytest.raises(ValueError):
        catalogue.Message(en='{value:u}', fr=french, context='A count')


def test_translation_can_reorder_typed_values() -> None:
    message = catalogue.Message(
        en='{name:s}: {count:u}', fr='{count:u} : {name:s}', context='A count'
    )
    assert catalogue.fields(message.fr) == [('count', 'u'), ('name', 's')]


@pytest.mark.parametrize('damage', ['crc', 'offset', 'schema', 'template', 'version'])
def test_damaged_or_incompatible_pack_is_rejected(damage: str) -> None:
    packed = bytearray((ROOT / 'cntrl/localization/packs/fr.bin').read_bytes())
    catalogue.inspect(bytes(packed))
    if damage == 'crc':
        packed[-1] ^= 1
    elif damage == 'offset':
        struct.pack_into('<I', packed, catalogue.HEADER.size, 0)
    elif damage == 'schema':
        struct.pack_into('<I', packed, 64, 0)
    elif damage == 'version':
        struct.pack_into('<H', packed, 4, 1)
    else:
        source = catalogue.load()
        index = list(source.messages).index('tempo_slot')
        offset = struct.unpack_from('<I', packed, catalogue.HEADER.size + index * 8)[0]
        packed[offset : offset + 3] = b'{x}'
    if damage != 'crc':
        struct.pack_into('<I', packed, 12, 0)
        struct.pack_into('<I', packed, 12, crc32(packed))
    with pytest.raises(ValueError):
        catalogue.inspect(bytes(packed))


@pytest.mark.parametrize('language', ['en', 'fr'])
def test_shipped_packs_match_their_translation_sources(language: str) -> None:
    data = (ROOT / f'cntrl/localization/packs/{language}.bin').read_bytes()
    packed = catalogue.inspect(data)
    assert packed.language == language
    assert catalogue.compile_catalogue(language, data[packed.font_offset :]) == data
    faces = assets.decode(data[packed.font_offset :], language, 0).sizes
    if language == 'en':
        assert 13 not in faces
    else:
        assert set(faces[13].characters) == {'’'}
        assert not set(faces[13].characters) & set(status.BASE_CHARACTERS)


def test_native_formatter_reorders_values_and_preserves_utf8(tmp_path: Path) -> None:
    executable = tmp_path / 'messages'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{ROOT / "firmware/include"}',
            str(ROOT / 'firmware/test/test_messages.cpp'),
            '-o',
            str(executable),
        ]
    )
    subprocess.run([str(executable)], check=True)


ROOT = Path(__file__).resolve().parents[1]
