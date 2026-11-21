"""The device-local implementation of the portable build executable contract."""

import shutil
from pathlib import Path

import tomlkit
import tyro
from pydantic import BaseModel

from ....firmware.capabilities import header
from ....patch_chooser.compiler_inputs import read_document
from ....patch_chooser.models import Target
from . import boot_switch, export_fonts, release, toolchain


class Build(BaseModel, frozen=True):
    source_root: Path
    output: Path
    diagnostics: bool = False
    publish: bool = False


def run(command: Build) -> None:
    if command.output.exists() and any(
        p.name != 'tmp' for p in command.output.iterdir()
    ):
        raise ValueError(
            'choose a new build output directory to preserve previous exports'
        )
    root = Path(__file__).parent
    toolchain.ensure(root / 'firmware')
    command.output.mkdir(parents=True, exist_ok=True)
    data = read_document(root / 'patch-chooser-target.json')
    if not isinstance(data, dict):
        raise ValueError('target description must be a table')
    target = Target.model_validate(data)
    sources = export_fonts.export(
        export_fonts.FontExport(
            output=command.output / 'fonts',
            ui_fonts=target.ui_fonts,
            display_height=target.display_height,
        )
    )
    data['font_sources'] = {n: f'fonts/{p}' for n, p in sources.items()}
    data['fonts'] = [
        f.model_dump(mode='json')
        for f in export_fonts.faces(command.output / 'fonts', sources)
    ]
    target = Target.model_validate(data)
    capabilities = header(target)
    (root / 'firmware/patch_chooser/target_capabilities.h').write_text(capabilities)
    (command.output / 'target_capabilities.h').write_text(capabilities)
    (command.output / 'target.toml').write_text(
        tomlkit.dumps(target.model_dump(mode='json', exclude_none=True))
    )
    components = boot_switch.build(
        boot_switch.Build(diagnostics=command.diagnostics),
        source_root=command.source_root,
    )
    shutil.copyfile(
        root / 'firmware/build/boot-switch/cntrl/patch_chooser.ino.elf',
        command.output / 'firmware.elf',
    )
    shutil.copyfile(
        root / 'firmware/.tools/provenance.toml', command.output / 'toolchain.toml'
    )
    shutil.copyfile(
        root / 'firmware/build/boot-switch/memory.txt', command.output / 'memory.txt'
    )
    shutil.copyfile(
        root / 'firmware/build/boot-switch/compiler-options.toml',
        command.output / 'compiler-options.toml',
    )
    release.export(command.output, command.source_root, components)
    release.prepare(command.output / 'distribution', command.output)
    if command.publish:
        release.publish(command.output, command.source_root)
    (command.output / 'build.toml').write_text(
        tomlkit.dumps(
            {
                'image': 'firmware.hex',
                'elf': 'firmware.elf',
                'target': 'target.toml',
                'diagnostics': command.diagnostics,
            }
        )
    )


def main() -> None:
    run(tyro.cli(Build))


if __name__ == '__main__':
    main()
