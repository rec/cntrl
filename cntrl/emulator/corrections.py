"""Translation fields and report serialization for Correction mode."""

import re
from functools import cache
from typing import Literal
from urllib.parse import urlencode

import tomlkit
from pydantic import BaseModel, Field

from ..fonts.catalogue import bundled_sources
from ..fonts.outline import Outline
from ..fonts.ui import supported_characters
from ..localization import catalogue


class TranslationField(BaseModel, frozen=True):
    message: str
    field: Literal['text', 'short']
    original: str


class Correction(TranslationField, frozen=True):
    replacement: str


class TranslationRegion(TranslationField, frozen=True):
    index: int
    screen: int
    left: int
    top: int
    right: int
    bottom: int
    short_length: int = 0
    glyphs: list[list[int]] = Field(default_factory=list)
    literal: int | None = None

    def editor_text(self, template: str) -> str:
        if self.literal is None:
            return template
        return re.split(r'(\{[^{}]+\})', template)[2 * self.literal].strip()

    def replace_literal(self, template: str, replacement: str) -> str:
        if self.literal is None:
            return replacement
        pieces = re.split(r'(\{[^{}]+\})', template)
        index = 2 * self.literal
        literal = pieces[index]
        left = len(literal) - len(literal.lstrip())
        right = len(literal.rstrip())
        pieces[index] = literal[:left] + replacement + literal[right:]
        return ''.join(pieces)

    def valid_replacement(self, text: str) -> bool:
        if not text.strip() or not text.isprintable():
            return False
        length = len(text.encode())
        if length >= 512 or (
            self.literal is None
            and length > 63
            and (self.field == 'short' or not 1 <= self.short_length <= 63)
        ):
            return False
        try:
            return (
                catalogue.fields(text) == catalogue.fields(self.original)
                and set(text) <= correction_characters()
            )
        except ValueError:
            return False


@cache
def correction_characters() -> frozenset[str]:
    return frozenset(supported_characters(Outline(bundled_sources()['Arimo'])))


class CorrectionBatch:
    def __init__(self, language: str, language_name: str) -> None:
        self.language = language
        self.language_name = language_name
        self.entries: dict[tuple[str, str], Correction] = {}

    def accept(self, field: TranslationField, replacement: str) -> None:
        key = field.message, field.field
        if replacement == field.original:
            self.entries.pop(key, None)
        else:
            self.entries[key] = Correction(
                message=field.message,
                field=field.field,
                original=field.original,
                replacement=replacement,
            )

    def report(self, draft: Correction | None = None) -> str:
        entries = self.entries.copy()
        if draft is not None:
            key = draft.message, draft.field
            if draft.replacement == draft.original:
                entries.pop(key, None)
            else:
                entries[key] = draft
        return tomlkit.dumps(
            {
                'language': self.language,
                'language_name': self.language_name,
                'corrections': [entries[k].model_dump() for k in sorted(entries)],
            }
        )

    def issue_url(self, body: str = '') -> str:
        fields = {'title': f'Translation correction for {self.language_name}'}
        if body:
            fields['body'] = body
        return 'https://github.com/rec/cntrl/issues/new?' + urlencode(fields)


def issue_body(report: str) -> str:
    # A proposed translation may contain a Markdown fence of its own.
    fence = '```'
    while fence in report:
        fence += '`'
    return f'{fence}toml\n{report}{fence}\n'
