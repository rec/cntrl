"""Convert the adapter's native font sources to portable bitmap resources."""

from pathlib import Path

import tyro
from pydantic import BaseModel, Field

from ....fonts.assets import pack
from ....fonts.catalogue import UI_FONTS
from ....fonts.outline import Glyph
from ....fonts.ui import embedded_header
from ....patch_switcher import models
from .build_bitmap_fonts import FAMILIES, PAGE_FAMILY
from .build_font_metrics import Options, native_face
from .native_raster import NativeRaster


class FontExport(BaseModel, frozen=True):
    output: Path
    ui_fonts: list[str] = Field(default_factory=lambda: list(UI_FONTS))
    source: Path = Options().source
    generated: Path = Path(__file__).parent / 'firmware/patch_switcher'


class EmbeddedRaster(NativeRaster):
    def glyph(self, size: int, identifier: int) -> Glyph:
        glyph = super().glyph(size, identifier)
        return glyph.model_copy(update={'top': glyph.top + self.tables[size][2][-1]})


def export(options: FontExport) -> dict[str, str]:
    options.output.mkdir(parents=True, exist_ok=True)
    sources = {}
    embedded: dict[str, bytes] = {}
    families = [
        (0, 'classic', 'Arial'),
        (1, 'Arial', 'Arial'),
        (2, 'ComicSansMS', 'ComicSansMS'),
    ]
    families.extend(
        (i, n, n) for i, (n, _, _) in enumerate([*FAMILIES, PAGE_FAMILY], 3)
    )
    ascii_text = ''.join(chr(c) for c in range(32, 127))
    for face, name, native in families:
        path = (
            options.source / f'st7735_t3_font_{native}.c'
            if face < 3
            else options.generated / f'font_{native}.cpp'
        )
        raster = NativeRaster(path, native)
        selected = {s: list(ascii_text) for s in raster.tables}
        metrics = native_face(path.read_text(), native, face).sizes
        if face < 3:
            data = pack(
                raster,
                selected,
                metrics,
                runtime_characters=ascii_text,
                hinted_advances=True,
                prepared_runs=False,
            )
            resource = f'{name}.cfont'
            (options.output / resource).write_bytes(data)
            sources[name] = resource
        if name not in options.ui_fonts:
            continue
        embedded_metrics = {
            s: models.FontMetrics(
                line_height=m.line_height, top=raster.tables[s][2][-1]
            )
            for s, m in metrics.items()
        }
        data = pack(
            EmbeddedRaster(path, native),
            selected,
            embedded_metrics,
            runtime_characters=ascii_text,
            hinted_advances=True,
            prepared_runs=False,
        )
        embedded[name] = data
    prefix = (
        '#if defined(__IMXRT1062__)\n'
        '#define CNTRL_UI_FONT_STORAGE __attribute__((section(".progmem")))\n'
        '#endif\n'
    )
    (options.generated / 'embedded_fonts.h').write_text(
        prefix + embedded_header(embedded)
    )
    return sources


def main() -> None:
    export(tyro.cli(FontExport))


if __name__ == '__main__':
    main()
