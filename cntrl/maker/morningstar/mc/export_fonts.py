"""Convert the adapter's native font sources to portable bitmap resources."""

import shutil
from pathlib import Path

import tyro
from pydantic import BaseModel, Field

from ....fonts import status, ui
from ....fonts.assets import decode, pack
from ....fonts.catalogue import FONT_IDS, SIZES, UI_FONTS, bundled_sources
from ....fonts.outline import Glyph, Outline
from ....fonts.ui import embedded_header
from ....patch_switcher import models
from .build_font_metrics import Options, native_face
from .native_raster import NativeRaster


class FontExport(BaseModel, frozen=True):
    output: Path
    ui_fonts: list[str] = Field(default_factory=lambda: list(UI_FONTS))
    source: Path = Options().source
    generated: Path = Path(__file__).parent / 'firmware/patch_switcher'
    display_height: int = 128


class EmbeddedRaster(NativeRaster):
    def glyph(self, size: int, identifier: int) -> Glyph:
        glyph = super().glyph(size, identifier)
        return glyph.model_copy(update={'top': glyph.top + self.tables[size][2][-1]})


def embedded_font(path: Path, name: str, face: int) -> bytes:
    """Prepare the same neutral font asset for firmware and desktop execution."""
    raster = EmbeddedRaster(path, name)
    ascii_text = ''.join(chr(c) for c in range(32, 127))
    selected = {s: list(ascii_text) for s in raster.tables}
    metrics = native_face(path.read_text(), name, face).sizes
    embedded_metrics = {
        s: models.FontMetrics(line_height=m.line_height, top=raster.tables[s][2][-1])
        for s, m in metrics.items()
    }
    return pack(
        raster,
        selected,
        embedded_metrics,
        runtime_characters=ascii_text,
        hinted_advances=True,
        prepared_runs=False,
    )


def export(options: FontExport) -> dict[str, str]:
    options.output.mkdir(parents=True, exist_ok=True)
    sources = {}
    embedded: dict[str, bytes] = {}
    for name in FONT_IDS:
        if name in UI_FONTS and name not in options.ui_fonts:
            continue
        if name == 'TamzenBold7x13':
            data = status.prepare()
        elif name == 'ComicSansMS':
            raster = EmbeddedRaster(
                options.source / 'st7735_t3_font_ComicSansMS.c', name
            )
            characters = ''.join(chr(c) for c in range(32, 127))
            heights = {}
            for size in raster.tables:
                glyphs = [raster.glyph(size, ord(c)) for c in characters]
                heights[size] = max(g.top for g in glyphs) + max(
                    g.height - g.top for g in glyphs
                )
            size = max(
                s for s, h in heights.items() if h <= options.display_height // 3 - 4
            )
            data = pack(
                raster,
                {size: list(characters)},
                {
                    size: models.FontMetrics(
                        line_height=heights[size], top=raster.tables[size][2][-1]
                    )
                },
                runtime_characters=characters,
                hinted_advances=True,
                prepared_runs=False,
            )
        else:
            outline = Outline(bundled_sources()[name], hinting='native')
            characters = ui.supported_characters(outline)
            sizes = (
                SIZES
                if name == 'CourierPrime'
                else list(range(8, 33)) + [40, 48, 60, 72, 96]
                if name in UI_FONTS
                else [ui.performance_size(outline, characters, options.display_height)]
            )
            data = ui.prepare(
                name,
                sizes,
                characters=characters,
                hinting='auto' if name == 'AtkinsonNextSemiBold' else 'native',
                horizontal_scale=1.5 if name == 'StillTime' else 1.0,
            )
        resource = f'{name}.cfont'
        (options.output / resource).write_bytes(data)
        sources[name] = resource
        embedded[name] = data
        if name in bundled_sources():
            source = bundled_sources()[name]
            license_file = (
                source.parents[1] / 'CC0-1.0.txt'
                if name in ('StillTime', 'MetalLord')
                else next(
                    p
                    for p in [
                        source.parent / 'OFL.txt',
                        source.parent / 'LICENSE.txt',
                    ]
                    if p.is_file()
                )
            )
            shutil.copyfile(license_file, options.output / f'{name}-LICENSE.txt')
        elif name == 'TamzenBold7x13':
            shutil.copyfile(
                Path(__file__).parents[3] / 'fonts/bitmap_samples/Tamzen-LICENSE.txt',
                options.output / f'{name}-LICENSE.txt',
            )
    prefix = (
        '#if defined(__IMXRT1062__)\n'
        '#define CNTRL_UI_FONT_STORAGE __attribute__((section(".progmem")))\n'
        '#endif\n'
    )
    (options.generated / 'embedded_fonts.h').write_text(
        prefix + embedded_header(embedded)
    )
    return sources


def faces(directory: Path, sources: dict[str, str]) -> list[models.FontFace]:
    return [
        decode((directory / p).read_bytes(), n, 0).model_copy(
            update={
                'id': FONT_IDS[n],
                'horizontal_scale': 1.5 if n == 'StillTime' else 1.0,
            }
        )
        for n, p in sources.items()
    ]


def main() -> None:
    export(tyro.cli(FontExport))


if __name__ == '__main__':
    main()
