"""Prepare the shipped language packs using the existing host font pipeline."""

from pathlib import Path
from unicodedata import normalize

import tyro
from pydantic import BaseModel

from ..fonts import assets, status, ui
from . import catalogue


class Build(BaseModel, frozen=True):
    output: Path = Path(__file__).with_name('packs')


def build(output: Path) -> None:
    messages = catalogue.load()
    output.mkdir(parents=True, exist_ok=True)
    for locale in ('en', 'fr'):
        texts = ['English', 'Français', '...']
        for message in messages.messages.values():
            texts.extend(
                [getattr(message, locale), getattr(message, f'short_{locale}')]
            )
        characters = ''.join(
            sorted(
                set(
                    ''.join(chr(c) for c in range(32, 127))
                    + normalize('NFC', ''.join(texts))
                )
            )
        )
        font = ui.prepare(
            'Arimo', [8, 10, 12, 14, 16, 20], characters=characters, hinting='native'
        )
        extra = ''.join(c for c in characters if c not in status.BASE_CHARACTERS)
        if extra:
            # Size 13 in a language pack is the sparse Tamzen supplement;
            # the other sizes belong to the scalable localized UI face.
            supplement = status.prepare(extra)
            _, count = assets.HEADER.unpack_from(font)
            font = assets.HEADER.pack(b'CFN2', count + 1) + font[8:] + supplement[8:]
        data = catalogue.compile_catalogue(locale, font)
        catalogue.inspect(data)
        (output / f'{locale}.bin').write_bytes(data)
        print(f'{locale}: {len(data)} bytes, UI font {len(font)} bytes')


def main() -> None:
    build(tyro.cli(Build).output)


if __name__ == '__main__':
    main()
