"""Language packs preserve semantic messages and validated typed placeholders."""

import struct
import subprocess
from pathlib import Path
from zlib import crc32

import pytest

from cntrl.localization import catalogue
from scripts import native_tests


def test_all_requested_european_locales_are_supplied() -> None:
    assert set(catalogue.locales()) == {
        'en',
        'fr',
        'de',
        'es',
        'it',
        'pt-PT',
        'nl',
        'pl',
        'cs',
        'sk',
        'hu',
        'ro',
        'sv',
        'da',
        'nb',
        'fi',
        'tr',
    }


def test_reference_header_is_reproducible(
    tmp_path: Path,
) -> None:
    source = catalogue.load()
    assert all(m.context for m in source.messages.values())
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
    data = catalogue.translation('en').model_dump()
    data['messages']['tempo_slot']['text'] = french
    with pytest.raises(ValueError):
        catalogue.Translation.model_validate(data)


def test_translation_can_reorder_typed_values() -> None:
    data = catalogue.translation('en').model_dump()
    data['messages']['control_action']['text'] = '{action:s} {label:s}{separator:s}'
    translated = catalogue.Translation.model_validate(data)
    assert catalogue.fields(translated.messages['control_action'].text) == [
        ('action', 's'),
        ('label', 's'),
        ('separator', 's'),
    ]


@pytest.mark.parametrize(
    'change', ['missing', 'unknown', 'empty', 'overlong', 'needs_short']
)
def test_incomplete_or_invalid_translations_cannot_be_released(change: str) -> None:
    data = catalogue.translation('en').model_dump()
    if change == 'missing':
        del data['messages']['browse']
    elif change == 'unknown':
        data['messages']['not_a_reference_message'] = {'text': 'Unknown'}
    elif change == 'empty':
        data['messages']['browse']['text'] = ''
    elif change == 'overlong':
        data['messages']['browse']['text'] = 'é' * 256
    else:
        data['messages']['browse']['text'] = 'é' * 32
    with pytest.raises(ValueError):
        catalogue.Translation.model_validate(data)


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


@pytest.mark.parametrize('language', catalogue.locales())
def test_shipped_packs_match_their_translation_sources(language: str) -> None:
    data = (ROOT / f'cntrl/localization/packs/{language}.bin').read_bytes()
    packed = catalogue.inspect(data)
    assert packed.language == language
    assert catalogue.compile_catalogue(language, data[packed.font_offset :]) == data
    assert packed.font_length == 0
    assert packed.font_offset == len(data)


@pytest.mark.parametrize('language', catalogue.locales())
def test_translation_captions_have_shared_flash_glyphs(language: str) -> None:
    from cntrl.fonts import ui
    from cntrl.fonts.catalogue import bundled_sources
    from cntrl.fonts.outline import Outline

    translated = catalogue.translation(language)
    characters = set(ui.supported_characters(Outline(bundled_sources()['Arimo'])))
    captions = [
        translated.name,
        *[t for m in translated.messages.values() for t in (m.text, m.short)],
    ]
    assert not set(''.join(captions)) - characters - {'\n'}


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
