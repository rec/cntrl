"""Convert the adapter's native font sources to portable bitmap resources."""

import shutil
from hashlib import sha256
from importlib.metadata import version
from pathlib import Path
from tempfile import TemporaryDirectory

import freetype
import tomlkit
import tyro
from pydantic import BaseModel, Field, ValidationError
from tomlkit.exceptions import ParseError

from ....fonts import status, ui
from ....fonts.assets import decode, pack
from ....fonts.bitmap import BitmapRaster
from ....fonts.catalogue import FONT_IDS, SIZES, UI_FONTS, bundled_sources
from ....fonts.outline import Glyph, Outline
from ....fonts.ui import embedded_header
from ....patch_chooser import models
from ....source_files import write_if_changed
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


class FontCache(BaseModel, frozen=True):
    key: str
    sources: dict[str, str]
    metrics: list[models.FontFace]
    files: dict[str, str]


def export_cached(
    options: FontExport, directory: Path
) -> tuple[dict[str, str], list[models.FontFace]]:
    """Keep rasterized assets and metrics across release output directories."""
    directory.mkdir(parents=True, exist_ok=True)
    record_path = directory / 'export.toml'
    key = export_key(options)
    record: FontCache | None = None
    contents: dict[str, bytes] = {}
    try:
        candidate = FontCache.model_validate(
            tomlkit.parse(record_path.read_text()).unwrap()
        )
        if candidate.key == key:
            contents = {n: (directory / n).read_bytes() for n in candidate.files}
            if {
                n: sha256(b).hexdigest() for n, b in contents.items()
            } == candidate.files:
                record = candidate
    except (OSError, UnicodeError, ParseError, ValidationError):
        pass
    if record is None:
        with TemporaryDirectory(dir=directory) as name:
            staging = Path(name)
            sources = export(
                options.model_copy(update={'output': staging, 'generated': staging})
            )
            metrics = faces(staging, sources)
            contents = {p.name: p.read_bytes() for p in staging.iterdir()}
        record = FontCache(
            key=key,
            sources=sources,
            metrics=metrics,
            files={n: sha256(b).hexdigest() for n, b in contents.items()},
        )
        for name, data in contents.items():
            write_if_changed(directory / name, data)
        write_if_changed(
            record_path,
            tomlkit.dumps(record.model_dump(mode='json', exclude_none=True)).encode(),
        )
    options.output.mkdir(parents=True, exist_ok=True)
    for name, data in contents.items():
        destination = (
            options.generated if name == 'embedded_fonts.h' else options.output
        )
        write_if_changed(destination / name, data)
    return record.sources, record.metrics


def export_key(options: FontExport) -> str:
    sources = bundled_sources()
    names = [n for n in FONT_IDS if n not in UI_FONTS or n in options.ui_fonts]
    directory = Path(status.__file__).parent
    inputs = [
        Path(__file__),
        *sorted(directory.glob('*.py')),
        *(sources[n] for n in names if n in sources),
        *(font_license(n, sources) for n in names),
    ]
    if 'TamzenBold7x13' in names:
        inputs.extend(sorted((directory / 'bitmap_samples').glob('Tamzen*.bdf')))
    key = sha256(
        repr(
            (
                names,
                options.display_height,
                freetype.version(),
                [
                    version(n)
                    for n in (
                        'freetype-py',
                        'uharfbuzz',
                        'fonttools',
                        'pydantic',
                        'python-bidi',
                    )
                ],
            )
        ).encode()
    )
    for path in inputs:
        key.update(sha256(path.read_bytes()).digest())
    return key.hexdigest()


def font_license(name: str, sources: dict[str, Path]) -> Path:
    if name == 'TamzenBold7x13':
        return Path(status.__file__).with_name('bitmap_samples') / 'Tamzen-LICENSE.txt'
    source = sources[name]
    if name in ('StillTime', 'MetalLord'):
        return source.parents[1] / 'CC0-1.0.txt'
    return next(
        p
        for p in (source.parent / 'OFL.txt', source.parent / 'LICENSE.txt')
        if p.is_file()
    )


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
        shutil.copyfile(
            font_license(name, bundled_sources()),
            options.output / f'{name}-LICENSE.txt',
        )
    prefix = (
        '#if defined(__IMXRT1062__)\n'
        '#define CNTRL_UI_FONT_STORAGE __attribute__((section(".progmem")))\n'
        '#endif\n'
    )
    write_if_changed(
        options.generated / 'embedded_fonts.h',
        (prefix + embedded_header(embedded)).encode(),
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
