"""The shared Status repertoire and native Tamzen language supplements."""

from pathlib import Path

from . import bdf
from .characters import printable_characters


def prepare(characters: str = '') -> bytes:
    """Prepare the base, or exactly the requested language-only glyphs."""
    directory = Path(__file__).with_name('bitmap_samples')
    source = (directory / 'Tamzen7x13b.bdf').read_bytes()
    additions = (directory / 'Tamzen7x13b-additions.bdf').read_bytes()
    data = source + additions
    if not characters:
        font = bdf.Bdf(data)
        characters = ''.join(c for c in printable_characters() if ord(c) in font.glyphs)
    return bdf.prepare(data, 13, trim=True, characters=characters)


def main() -> None:
    directory = Path(__file__).with_name('bitmap_samples')
    data = prepare()
    (directory / 'TamzenBold7x13.cfont').write_bytes(data)
    print(f'Status font: {len(data)} bytes')


BASE_CHARACTERS = ''.join(chr(c) for c in range(32, 127)) + (
    'ÀÂÆÇÈÉÊËÎÏÔŒÙÛÜŸàâæçèéêëîïôœùûüÿÄÅÑÖĀŌäåñöāō'
)


if __name__ == '__main__':
    main()
