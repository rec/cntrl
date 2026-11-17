"""Prepare translation packs that reuse the embedded UI fonts."""

from pathlib import Path

from pydantic import BaseModel

from ..fonts import ui
from ..fonts.catalogue import bundled_sources
from ..fonts.outline import Outline
from . import catalogue


class Build(BaseModel, frozen=True):
    output: Path = Path(__file__).with_name('packs')
    header: Path = Path('firmware/include/cntrl/ui/messages.h')


def build(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    characters = set(ui.supported_characters(Outline(bundled_sources()['Arimo'])))
    for locale in catalogue.locales():
        translated = catalogue.translation(locale)
        text = translated.name + ''.join(
            m.text + m.short for m in translated.messages.values()
        )
        missing = sorted(set(text) - characters - {'\n'})
        if missing:
            raise ValueError(f'{locale}: missing shared UI glyphs {missing}')
        data = catalogue.compile_catalogue(locale, b'')
        catalogue.inspect(data)
        (output / f'{locale}.bin').write_bytes(data)
        print(f'{locale}: {len(data)} bytes, using flash UI fonts')
