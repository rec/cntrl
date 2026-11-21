"""Convert the adapter's native font sources to portable bitmap resources."""

import shutil
from pathlib import Path

import tyro
from pydantic import BaseModel, Field

from ....fonts import status, ui
from ....fonts.assets import decode, pack
from ....fonts.bitmap import BitmapRaster
from ....fonts.catalogue import FONT_IDS, SIZES, UI_FONTS, bundled_sources
from ....fonts.outline import Glyph, Outline
from ....fonts.ui import embedded_header
from ....patch_chooser import models
from .build_font_metrics import native_face
from .native_raster import NativeRaster


class FontExport(BaseModel, frozen=True):
    output: Path
    ui_fonts: list[str] = Field(default_factory=lambda: list(UI_FONTS))
    generated: Path = Path(__file__).parent / 'firmware/patch_chooser'
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
        else:
            outline = Outline(bundled_sources()[name], hinting='auto')
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
                hinting='auto',
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
    result = []
    for name, resource in sources.items():
        path = directory / resource
        face = decode(path.read_bytes(), name, 0)
        raster = BitmapRaster(path)
        # Embedded rendering already applies the asset's cap-height origin.
        # Layout needs only the remaining ascent above that origin.
        sizes = {
            s: m.model_copy(
                update={
                    'top': max(raster.glyph(s, ord(c)).top for c in m.characters)
                    - m.top
                }
            )
            for s, m in face.sizes.items()
        }
        result.append(
            face.model_copy(
                update={
                    'id': FONT_IDS[name],
                    'horizontal_scale': 1.5 if name == 'StillTime' else 1.0,
                    'sizes': sizes,
                }
            )
        )
    return result


def main() -> None:
    export(tyro.cli(FontExport))


if __name__ == '__main__':
    main()
