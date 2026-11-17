"""Prepare translation packs that reuse the embedded UI fonts."""

from pathlib import Path

import tyro
from pydantic import BaseModel

from . import catalogue


class Build(BaseModel, frozen=True):
    output: Path = Path(__file__).with_name('packs')


def build(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    for locale in ('en', 'fr'):
        data = catalogue.compile_catalogue(locale, b'')
        catalogue.inspect(data)
        (output / f'{locale}.bin').write_bytes(data)
        print(f'{locale}: {len(data)} bytes, using flash UI fonts')


def main() -> None:
    build(tyro.cli(Build).output)


if __name__ == '__main__':
    main()
