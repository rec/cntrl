"""Regenerate the adapter's shared fonts and matching layout measurements.

Run from the repository root with:
PYTHONPATH=. uv run --no-sync cntrl/maker/morningstar/mc/prepare_performance_fonts.py
"""

import json
from pathlib import Path

import tyro
from pydantic import BaseModel

from cntrl.maker.morningstar.mc.export_fonts import FontExport, export, faces
from cntrl.patch_switcher.models import Target


class PreparePerformanceFonts(BaseModel, frozen=True):
    """Regenerate the shared performance fonts and their target measurements."""


def main() -> None:
    tyro.cli(PreparePerformanceFonts)
    root = Path(__file__).resolve().parent
    target = root / 'patch-switcher-target.json'
    data = json.loads(target.read_text())
    output = root / 'font_sources'
    sources = export(
        FontExport(
            output=output, display_height=Target.model_validate(data).display_height
        )
    )
    data['fonts'] = [f.model_dump(mode='json') for f in faces(output, sources)]
    target.write_text(json.dumps(data, indent=2) + '\n')


if __name__ == '__main__':
    main()
