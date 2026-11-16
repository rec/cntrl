"""Prepare the shipped language packs using the existing host font pipeline."""

from pathlib import Path
from unicodedata import normalize

import tyro
from pydantic import BaseModel

from ..fonts import ui
from . import catalogue


class Build(BaseModel, frozen=True):
    output: Path = Path(__file__).with_name('packs')


def build(output: Path) -> None:
    messages = catalogue.load()
    texts = ['English', 'Français', '...']
    for message in messages.messages.values():
        texts.extend([message.en, message.fr, message.short_en, message.short_fr])
    characters = ''.join(
        sorted(
            set(
                ''.join(chr(i) for i in range(32, 127))
                + normalize('NFC', ''.join(texts))
            )
        )
    )
    font = ui.prepare(
        'Arimo', [8, 10, 12, 14, 16, 20], characters=characters, hinting='native'
    )
    output.mkdir(parents=True, exist_ok=True)
    for locale in ('en', 'fr'):
        data = catalogue.compile_catalogue(locale, font)
        catalogue.inspect(data)
        (output / f'{locale}.bin').write_bytes(data)
        print(f'{locale}: {len(data)} bytes, UI font {len(font)} bytes')


def main() -> None:
    build(tyro.cli(Build).output)


if __name__ == '__main__':
    main()
