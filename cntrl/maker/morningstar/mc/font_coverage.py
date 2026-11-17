"""Report bundled source and packaged font coverage without downloading fonts."""

import re
import subprocess
from io import BytesIO
from pathlib import Path

import tyro
from fontTools.ttLib import TTFont
from pydantic import BaseModel

from cntrl.fonts.assets import decode
from cntrl.fonts.catalogue import FONT_IDS, bundled_sources
from cntrl.fonts.characters import CHARACTERS
from cntrl.localization import catalogue


class Report(BaseModel, frozen=True):
    output: Path = Path('plan/font-coverage.md')


def generate() -> str:
    paths = [
        Path(p)
        for p in subprocess.check_output(
            ['git', 'ls-files', 'cntrl'], text=True
        ).splitlines()
        if 'archive' not in Path(p).parts
    ]
    groups: dict[str, list[tuple[str, set[str]]]] = {
        'Outline sources': [],
        'Packaged bitmap assets': [],
        'Language-pack fonts': [],
        'Embedded portable UI fonts': [],
        'Legacy generated bitmap fonts': [],
    }
    names = {p: n for n, p in bundled_sources().items()}
    for path in paths:
        if path.suffix in ('.ttf', '.otf'):
            with TTFont(BytesIO(path.read_bytes())) as font:
                cmap = font.getBestCmap()
                if cmap is None:
                    raise ValueError(f'{path}: no Unicode character map')
                characters = {chr(c) for c in cmap.keys()}
            name = names.get(path.resolve(), path.stem)
            groups['Outline sources'].append((f'{name}: `{path}`', characters))
        elif path.suffix == '.cfont':
            groups['Packaged bitmap assets'].extend(
                coverage(path.read_bytes(), str(path))
            )
        elif path.suffix == '.bin' and path.read_bytes().startswith(b'CLNG'):
            data = path.read_bytes()
            *_, start, length = catalogue.HEADER.unpack_from(data)
            if length:
                groups['Language-pack fonts'].extend(
                    coverage(data[start : start + length], str(path))
                )
        elif path.name == 'embedded_fonts.h':
            text = path.read_text()
            arrays = {
                n: bytes(int(v) for v in b.split(',') if v.strip())
                for n, b in re.findall(
                    r'uint8_t (ui_font_\d+)\[\][^{]*\{([^}]+)\}', text
                )
            }
            identifiers = {v: k for k, v in FONT_IDS.items()}
            for identifier, symbol in re.findall(
                r'\{(\d+), (ui_font_\d+), sizeof', text
            ):
                name = identifiers[int(identifier)]
                groups['Embedded portable UI fonts'].extend(
                    coverage(arrays[symbol], f'{path}: {name}')
                )
        elif path.suffix == '.cpp' and path.name.startswith('font_'):
            declarations = re.findall(
                r'const ILI9341_t3_font_t (\w+)[^{]*\{[^}]+?,\s*'
                r'1,\s*0,\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),',
                path.read_text(),
            )
            sets: dict[frozenset[str], list[str]] = {}
            for name, first, last, second, end in declarations:
                codes = set(range(int(first), int(last) + 1))
                if int(second) or int(end):
                    codes.update(range(int(second), int(end) + 1))
                sets.setdefault(frozenset(chr(c) for c in codes), []).append(name)
            for characters, variants in sets.items():
                groups['Legacy generated bitmap fonts'].append(
                    (f'`{path}` ({len(variants)} sizes)', set(characters))
                )
    lines: list[str] = []
    lines.extend(INTRO.splitlines())
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    lines.extend(['', f'Inspected working tree based on revision: `{revision}`.'])
    missing_sets: dict[frozenset[str], str] = {}
    for heading, rows in groups.items():
        lines.extend(
            [
                '',
                f'## {heading}',
                '',
                '| Font / asset | Base covered | Of requested 373 | Missing set |',
                '| --- | ---: | ---: | --- |',
            ]
        )
        if heading == 'Language-pack fonts' and not rows:
            lines.extend(
                [
                    '',
                    'The supplied packs contain translations only '
                    'and reuse the flash fonts.',
                ]
            )
        for name, characters in rows:
            missing = frozenset(set(CHARACTERS) - characters)
            if missing:
                label = missing_sets.setdefault(missing, f'M{len(missing_sets) + 1}')
            else:
                label = 'None'
            lines.append(
                f'| {name} | {len(BASE & characters)}/139 | '
                f'{373 - len(missing)}/373 | {label} |'
            )
    lines.extend(
        [
            '',
            '## Exact missing characters',
            '',
            'Characters are ordered by Unicode code point. '
            'Each set applies to every row referencing it.',
        ]
    )
    for characters, label in missing_sets.items():
        lines.extend(
            [
                '',
                f'### {label}: {len(characters)} missing',
                '',
                ' '.join(f'`{c}`' for c in sorted(characters)),
            ]
        )
    lines.extend(
        [
            '',
            '## Additional work beyond the prompt',
            '',
            'None. Missing source glyphs have not been newly drawn. '
            'The inspection script reproduces the coverage tables.',
            '',
        ]
    )
    return '\n'.join(lines)


def coverage(data: bytes, name: str) -> list[tuple[str, set[str]]]:
    font = decode(data, name, 0)
    groups: dict[frozenset[str], list[int]] = {}
    for size, metrics in font.sizes.items():
        groups.setdefault(frozenset(metrics.characters), []).append(size)
    return [
        (
            f'`{name}` (size {s[0]}; {len(data)} bytes)'
            if len(s) == 1
            else f'`{name}` ({len(s)} sizes: {min(s)}–{max(s)}; {len(data)} bytes)',
            set(c),
        )
        for c, s in groups.items()
    ]


def main() -> None:
    options = tyro.cli(Report)
    options.output.write_text(generate())


FRENCH = 'àâæçèéêëîïôœùûüÿ'
EXTRA = 'äåñöāō'
BASE = {chr(c) for c in range(32, 127)} | set(
    FRENCH + FRENCH.upper() + EXTRA + EXTRA.upper()
)
INTRO = """# Base font coverage

This report checks the agreed **139 base characters** and the requested
**373-character comparison repertoire** against the actual tracked font files.
Soft hyphen U+00AD is a formatting control and is intentionally excluded from
generated standalone glyphs, so a complete generated face covers 372/373.
It includes current bundled sources, prepared assets, language packs and generated
font declarations. Historical archives and test-only fixtures are excluded.

## Base repertoire

* 95 printable ASCII characters, U+0020 through U+007E, including space.
* 32 French letters: `à â æ ç è é ê ë î ï ô œ ù û ü ÿ` and their uppercase forms.
* 12 additional letters: `ä å ñ ö ā ō` and `Ä Å Ñ Ö Ā Ō`.

This is the specifically agreed repertoire, not every character that can appear
in French typography or English loanwords. Curly quotes, nonbreaking spaces,
guillemets, currency symbols and decomposed combining sequences are outside it.
Counts refer to Unicode characters, not necessarily distinct bitmap shapes.

## Method and interpretation

Regenerate from the repository root:

```sh
uv run --no-sync python -m cntrl.maker.morningstar.mc.font_coverage
```

Outline coverage uses each local TTF/OTF Unicode cmap and does not claim visual
quality at small sizes. Packaged CFN2 coverage uses the runtime character map at
every stored size, with separate rows if coverage differs. Prepared whole-string
runs do not establish general runtime character support. Embedded portable arrays
are decoded using the same CFN2 reader. Legacy generated bitmap coverage uses the
character ranges in its font declarations. This does not launch the application.
Files are read from the working tree so regeneration checks newly generated assets.

Source coverage and packaged coverage are separate: a complete outline does not
mean its selected glyphs have been included in a generated asset. In particular,
ASCII-only bitmap assets omit all 44 non-ASCII base extensions.
Tamzen's selected UI asset includes every supported drawable comparison character;
the other comparison samples retain their original ASCII-only purpose.
"""


if __name__ == '__main__':
    main()
