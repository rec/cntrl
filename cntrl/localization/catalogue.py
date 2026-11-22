"""Validate reference messages and per-locale UI translations."""

import re
import struct
from pathlib import Path
from string import Formatter
from zlib import crc32

import tomlkit
from pydantic import (
    BaseModel,
    ConfigDict,
    ValidationError,
    field_validator,
    model_validator,
)
from tomlkit.exceptions import ParseError

from ..fonts import assets


class CatalogueError(ValueError):
    """A language catalogue, pack or generated output could not be used."""


class IncompatibleCatalogueError(CatalogueError):
    """An intact pack was built against a different message catalogue."""


class Message(BaseModel, frozen=True):
    en: str
    context: str
    short_en: str = ''

    @field_validator('en', 'context')
    @classmethod
    def nonempty(cls, value: str) -> str:
        if not value:
            raise ValueError('message text and context must not be empty')
        return value

    @model_validator(mode='after')
    def consistent_fields(self) -> 'Message':
        expected = dict(fields(self.en))
        if len(expected) != len(fields(self.en)):
            raise ValueError('duplicate reference placeholder')
        if self.short_en and dict(fields(self.short_en)) != expected:
            raise ValueError('short caption placeholders differ from reference')
        if self.short_en and len(fields(self.short_en)) != len(expected):
            raise ValueError('duplicate short caption placeholder')
        return self

    model_config = ConfigDict(extra='forbid')


class Catalogue(BaseModel, frozen=True):
    messages: dict[str, Message]

    @field_validator('messages')
    @classmethod
    def valid_ids(cls, value: dict[str, Message]) -> dict[str, Message]:
        if not value or any(not re.fullmatch('[a-z][a-z0-9_]*', n) for n in value):
            raise ValueError('message IDs must be lowercase C++ identifiers')
        return value

    model_config = ConfigDict(extra='forbid')


class TranslationText(BaseModel, frozen=True):
    text: str
    short: str = ''

    @field_validator('text')
    @classmethod
    def nonempty(cls, value: str) -> str:
        if not value:
            raise ValueError('translation text must not be empty')
        return value

    model_config = ConfigDict(extra='forbid')


class Translation(BaseModel, frozen=True):
    language: str
    name: str
    messages: dict[str, TranslationText]

    @field_validator('language')
    @classmethod
    def valid_language(cls, value: str) -> str:
        if (
            not re.fullmatch('[a-z]{2,3}(?:-[A-Za-z0-9]{2,8})*', value)
            or len(value) > 15
        ):
            raise ValueError('invalid locale tag')
        return value

    @field_validator('name')
    @classmethod
    def valid_name(cls, value: str) -> str:
        if not value or '\0' in value or len(value.encode()) > 31:
            raise ValueError('native language name must occupy 1 to 31 UTF-8 bytes')
        return value

    @model_validator(mode='after')
    def complete(self) -> 'Translation':
        reference = load()
        if self.messages.keys() != reference.messages.keys():
            missing = sorted(reference.messages.keys() - self.messages.keys())
            extra = sorted(self.messages.keys() - reference.messages.keys())
            raise ValueError(
                f'{self.language}: missing messages {missing}; unknown messages {extra}'
            )
        for name, message in self.messages.items():
            expected = fields(reference.messages[name].en)
            if (
                not expected
                and len(message.text.encode()) > 63
                and (not message.short or len(message.short.encode()) > 63)
            ):
                raise ValueError(
                    f'{self.language}.{name}: static caption needs '
                    'a short form within 63 UTF-8 bytes'
                )
            for text in (message.text, message.short):
                if not text:
                    continue
                actual = fields(text)
                if dict(actual) != dict(expected) or len(actual) != len(expected):
                    raise ValueError(
                        f'{self.language}.{name}: translation placeholders '
                        'differ from reference'
                    )
                if len(text.encode()) > 511:
                    raise ValueError(
                        f'{self.language}.{name}: translation exceeds 511 bytes'
                    )
        return self

    model_config = ConfigDict(extra='forbid')


class Pack(BaseModel, frozen=True):
    language: str
    name: str
    messages: int
    font_offset: int
    font_length: int


def inspect(data: bytes) -> Pack:
    if len(data) < HEADER.size or len(data) > 262144:
        raise CatalogueError('truncated language pack')
    magic, version, count, length, checksum, tag, name, abi, font_start, font_size = (
        HEADER.unpack_from(data)
    )
    catalogue = load()
    if (
        magic != b'CLNG'
        or version != 3
        or length != len(data)
        or crc32(data[:12] + bytes(4) + data[16:]) != checksum
        or font_start < HEADER.size + count * 8
        or font_start + font_size != len(data)
        or font_size
        and not data[font_start:].startswith(b'CFN2')
        or b'\0' not in tag
        or b'\0' not in name
    ):
        raise CatalogueError('invalid or incompatible language pack')
    if count != len(catalogue.messages) or abi != schema(catalogue):
        raise IncompatibleCatalogueError('language pack uses another message catalogue')
    try:
        language = tag.split(b'\0', 1)[0].decode()
        display_name = name.split(b'\0', 1)[0].decode()
    except UnicodeDecodeError as error:
        raise CatalogueError('invalid language identity') from error
    if (
        not re.fullmatch('[a-z]{2,3}(?:-[A-Za-z0-9]{2,8})*', language)
        or not display_name
    ):
        raise CatalogueError('invalid language identity')
    for i, message in enumerate(catalogue.messages.values()):
        full, short = struct.unpack_from('<II', data, HEADER.size + i * 8)
        for kind, offset in enumerate((full, short)):
            if kind and not offset:
                continue
            if not HEADER.size + count * 8 <= offset < font_start:
                raise CatalogueError('invalid language string offset')
            end = data.find(b'\0', offset, font_start)
            if end < 0:
                raise CatalogueError('unterminated language string')
            if len(data[offset:end]) > 511:
                raise CatalogueError('language string exceeds 511 bytes')
            try:
                text = data[offset:end].decode()
            except UnicodeDecodeError as error:
                raise CatalogueError('invalid UTF-8 in language string') from error
            actual = fields(text)
            expected = fields(message.en)
            if dict(actual) != dict(expected) or len(actual) != len(expected):
                raise CatalogueError('invalid language placeholders')
    font = data[font_start:]
    if font:
        assets.decode(font, 'language pack', crc32(font))
    return Pack(
        language=language,
        name=display_name,
        messages=count,
        font_offset=font_start,
        font_length=font_size,
    )


def fields(text: str) -> list[tuple[str, str]]:
    try:
        parts = list(Formatter().parse(text))
    except ValueError as error:
        raise CatalogueError(str(error)) from error
    result: list[tuple[str, str]] = []
    for literal, name, spec, conversion in parts:
        if '\0' in literal or conversion:
            raise CatalogueError('invalid translation template')
        if name is not None:
            if (
                not isinstance(spec, str)
                or not re.fullmatch('[a-z][a-z0-9_]*', name)
                or not re.fullmatch(r'(?:[-+ #0]*\d*(?:\.\d+)?)(?:ll|l)?[sdiuxX]', spec)
            ):
                raise CatalogueError(f'invalid typed placeholder {name!r}:{spec!r}')
            result.append((name, spec))
    return result


def load() -> Catalogue:
    return _read_catalogue(Path(__file__).with_name('messages.toml'), Catalogue)


def locales() -> list[str]:
    return [
        'en',
        *sorted(
            p.stem for p in Path(__file__).with_name('translations').glob('*.toml')
        ),
    ]


def translation(locale: str) -> Translation:
    if locale == 'en':
        return Translation(
            language='en',
            name='English',
            messages={
                n: TranslationText(text=m.en, short=m.short_en)
                for n, m in load().messages.items()
            },
        )
    if locale not in locales():
        raise CatalogueError(f'unsupported UI language {locale!r}')
    source = Path(__file__).with_name('translations') / f'{locale}.toml'
    result = _read_catalogue(source, Translation)
    if result.language != locale:
        raise CatalogueError(
            f'{source}: translation identity differs from its filename'
        )
    return result


def schema(catalogue: Catalogue) -> int:
    value = '\n'.join(
        n + ':' + ','.join(k + ':' + s for k, s in fields(m.en))
        for n, m in catalogue.messages.items()
    )
    return crc32(value.encode())


def compile_catalogue(locale: str, font: bytes) -> bytes:
    translated = translation(locale)
    if font and not font.startswith(b'CFN2'):
        raise ValueError('language pack requires a CFN2 font')
    catalogue = load()
    table = bytearray()
    strings = bytearray()
    base = HEADER.size + len(catalogue.messages) * 8
    for name in catalogue.messages:
        message = translated.messages[name]
        text = message.text
        short = message.short
        first = base + len(strings)
        strings.extend(text.encode() + b'\0')
        second = base + len(strings) if short else 0
        if short:
            strings.extend(short.encode() + b'\0')
        table.extend(struct.pack('<II', first, second))
    font_offset = base + len(strings)
    header = HEADER.pack(
        b'CLNG',
        3,
        len(catalogue.messages),
        font_offset + len(font),
        0,
        locale.encode(),
        translated.name.encode(),
        schema(catalogue),
        font_offset,
        len(font),
    )
    data = header + table + strings + font
    return data[:12] + struct.pack('<I', crc32(data)) + data[16:]


def literal(text: str) -> str:
    return (
        '"' + text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'
    )


def generate(path: Path) -> None:
    catalogue = load()
    lines = [
        '// Generated by cntrl language build. Do not edit.',
        '#pragma once',
        '#include "message_format.h"',
        'namespace cntrl::ui::i18n {',
        'enum class Message : uint16_t {',
        *[f'    {n},' for n in catalogue.messages],
        '};',
        f'inline constexpr unsigned message_count = {len(catalogue.messages)};',
        f'inline constexpr uint32_t catalogue_schema = 0x{schema(catalogue):08x}u;',
        'inline const char *(*lookup)(Message, bool) = nullptr;',
        'inline bool translated_ui = false;',
    ]
    for name, message in catalogue.messages.items():
        parameters = fields(message.en)
        lines.append(
            f'inline const char {name}_text[] CNTRL_I18N_STORAGE = '
            f'{literal(message.en)};'
        )
        lines.append(
            f'inline const char {name}_short[] CNTRL_I18N_STORAGE = '
            f'{literal(message.short_en)};'
        )
        if parameters:
            for key, spec in parameters:
                lines.append(
                    f'inline const char {name}_{key}_name[] CNTRL_I18N_STORAGE = '
                    f'{literal(key)};'
                )
                lines.append(
                    f'inline const char {name}_{key}_spec[] CNTRL_I18N_STORAGE = '
                    f'{literal(spec)};'
                )
            lines.append(f'inline const Field {name}_fields[] CNTRL_I18N_STORAGE = {{')
            for key, _ in parameters:
                lines.append(f'    {{{name}_{key}_name, {name}_{key}_spec}},')
            lines.append('};')
    lines.append('inline const Definition definitions[] CNTRL_I18N_STORAGE = {')
    for name, message in catalogue.messages.items():
        parameters = fields(message.en)
        lines.append(
            f'    {{{name}_text, {name}_short, '
            f'{name + "_fields" if parameters else "nullptr"}, {len(parameters)}}},'
        )
    lines.extend(
        [
            '};',
            '#ifdef CNTRL_FLASH_ROUTINE',
            'CNTRL_FLASH_ROUTINE(i18n_text)',
            '#endif',
            'inline const char *text(Message message) {',
            '    const unsigned index = unsigned(message);',
            '    if (index >= message_count) return "";',
            '    if (lookup) if (const char *value = lookup(message, false))',
            '        return value;',
            '    return definitions[index].text;',
            '}',
            '#ifdef CNTRL_FLASH_ROUTINE',
            'CNTRL_FLASH_ROUTINE(i18n_short_text)',
            '#endif',
            'inline const char *short_text(Message message) {',
            '    const unsigned index = unsigned(message);',
            '    if (index >= message_count) return "";',
            '    if (lookup) if (const char *value = lookup(message, true))',
            '        return value;',
            '    return definitions[index].short_text;',
            '}',
            '#ifdef CNTRL_FLASH_ROUTINE',
            'CNTRL_FLASH_ROUTINE(i18n_identity)',
            '#endif',
            'inline unsigned identity(const char *value) {',
            '    for (unsigned i = 0; i < message_count; ++i)',
            '        if (value == definitions[i].text || value == text(Message(i)))',
            '            return i + 1;',
            '    return 0;',
            '}',
            'inline const char *stable(const char *value) {',
            '    const unsigned id = value ? identity(value) : 0;',
            '    return id ? definitions[id - 1].text : value;',
            '}',
            'inline const char *translated(const char *value) {',
            '    const unsigned id = value ? identity(value) : 0;',
            '    return id ? text(Message(id - 1)) : value;',
            '}',
            '#ifdef CNTRL_FLASH_ROUTINE',
            'CNTRL_FLASH_ROUTINE(i18n_valid_template)',
            '#endif',
            'inline bool valid_template(Message message, const char *value) {',
            '    return unsigned(message) < message_count',
            '        && validate(definitions[unsigned(message)], value);',
            '}',
            'template<class... Args>',
            '#ifdef CNTRL_FLASH_ROUTINE',
            'CNTRL_FLASH_ROUTINE(i18n_format)',
            '#endif',
            'bool format(char *destination, unsigned capacity, Message message,',
            '            const Args &...args) {',
            '    const Argument values[] = {Argument(args)..., Argument()};',
            '    if (unsigned(message) >= message_count) return false;',
            '    return render(destination, capacity, definitions[unsigned(message)],',
            '                  text(message), values, sizeof...(Args));',
            '}',
            '}',
        ]
    )
    try:
        path.write_text('\n'.join(lines) + '\n')
    except OSError as error:
        raise CatalogueError(str(error)) from error


def _read_catalogue[Document: BaseModel](path: Path, model: type[Document]) -> Document:
    try:
        data = tomlkit.parse(path.read_text()).unwrap()
    except (OSError, UnicodeDecodeError, ParseError) as error:
        raise CatalogueError(f'{path}: {error}') from error
    try:
        return model.model_validate(data)
    except ValidationError as error:
        raise CatalogueError(f'{path}: {error}') from error


HEADER = struct.Struct('<4sHHII16s32sIII')
