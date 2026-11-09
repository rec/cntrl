"""The device-local implementation of the portable build executable contract."""

import shutil
from pathlib import Path

import tomlkit
import tyro
from pydantic import BaseModel

from ....firmware.capabilities import header
from ....patch_switcher.compiler import read_document
from ....patch_switcher.models import Target
from . import boot_switch, export_fonts, toolchain


class Build(BaseModel, frozen=True):
    source_root: Path
    output: Path


def run(command: Build) -> None:
    root = Path(__file__).parent
    toolchain.ensure(root / 'firmware')
    command.output.mkdir(parents=True, exist_ok=True)
    data = read_document(root / 'patch-switcher-target.json')
    if not isinstance(data, dict):
        raise ValueError('target description must be a table')
    target = Target.model_validate(data)
    sources = export_fonts.export(
        export_fonts.FontExport(
            output=command.output / 'fonts', ui_fonts=target.ui_fonts
        )
    )
    data['font_sources'] = {n: f'fonts/{p}' for n, p in sources.items()}
    target = Target.model_validate(data)
    capabilities = header(target)
    (root / 'firmware/patch_switcher/target_capabilities.h').write_text(capabilities)
    (command.output / 'target_capabilities.h').write_text(capabilities)
    (command.output / 'target.toml').write_text(
        tomlkit.dumps(target.model_dump(mode='json', exclude_none=True))
    )
    boot_switch.build(boot_switch.Build(), source_root=command.source_root)
    shutil.copyfile(
        root / 'firmware/build/boot-switch/combined.hex',
        command.output / 'firmware.hex',
    )
    shutil.copyfile(
        root / 'firmware/.tools/provenance.toml', command.output / 'toolchain.toml'
    )
    shutil.copyfile(
        root / 'firmware/build/boot-switch/memory.txt', command.output / 'memory.txt'
    )
    (command.output / 'build.toml').write_text(
        tomlkit.dumps({'image': 'firmware.hex', 'target': 'target.toml'})
    )


def main() -> None:
    run(tyro.cli(Build))


if __name__ == '__main__':
    main()
