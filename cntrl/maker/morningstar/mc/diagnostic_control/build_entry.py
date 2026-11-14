"""Build MIDI transport instrumentation on the working normal/newlib control."""

import hashlib
import shutil
import subprocess
import tarfile
from io import BytesIO
from pathlib import Path

import tomlkit
import tyro
from pydantic import BaseModel

from cntrl import build
from cntrl.maker.morningstar.mc import toolchain


class Build(BaseModel, frozen=True):
    source_root: Path
    output: Path
    diagnostics: bool = False


def run(command: Build) -> None:
    if command.diagnostics:
        raise ValueError('this MIDI control target does not enable the watchdog')
    repository, output = command.source_root.resolve(), command.output.resolve()
    source = output.with_name(output.name + '-source')
    source.mkdir(parents=True)
    archive = subprocess.check_output(
        ['git', '-C', str(repository), 'archive', BASELINE]
    )
    with tarfile.open(fileobj=BytesIO(archive)) as tree:
        tree.extractall(source, filter='data')
    patches = source / 'diagnostic_overlays'
    patches.mkdir()
    revisions: list[str] = []
    for revision, paths in OVERLAYS.items():
        patch = patches / f'{revision}.patch'
        patch.write_bytes(
            subprocess.check_output(
                [
                    'git',
                    '-C',
                    str(repository),
                    'diff',
                    f'{revision}^',
                    revision,
                    '--',
                    *paths,
                ]
            )
        )
        toolchain.apply_patch(source, patch)
        revisions.append(
            subprocess.check_output(
                ['git', '-C', str(repository), 'rev-parse', revision], text=True
            ).strip()
        )
    root = Path(__file__).resolve().parents[1]
    target = source / 'cntrl/maker/morningstar/mc'
    firmware = target / 'firmware'
    shutil.copyfile(
        root / 'firmware/patch_switcher/midi_trace.h',
        firmware / 'patch_switcher/midi_trace.h',
    )
    shutil.copyfile(
        root / 'firmware/serial-rx-diagnostics.patch',
        firmware / 'serial-rx-diagnostics.patch',
    )
    toolchain.apply_patch(source, Path(__file__).with_name('trace.patch'))
    identity = hashlib.sha256()
    identity.update(BASELINE.encode())
    identity.update(''.join(revisions).encode())
    for path in (
        firmware / 'patch_switcher/target.cpp',
        firmware / 'patch_switcher/midi_trace.h',
        firmware / 'serial-rx-diagnostics.patch',
        source / 'firmware/include/cntrl/midi_bridge.h',
    ):
        identity.update(path.relative_to(source).as_posix().encode() + b'\0')
        identity.update(path.read_bytes())
    trace_id = identity.hexdigest()[:16]
    (firmware / 'patch_switcher/midi_trace_build.h').write_text(
        f'#define CNTRL_MIDI_TRACE_BUILD "{trace_id}"\n'
    )
    # Copy the provisioned SDK, never mutate the SDK retained with the rollback.
    toolchain.ensure(root / 'firmware')
    shutil.copytree(
        root / 'firmware/.tools',
        firmware / '.tools',
        ignore=shutil.ignore_patterns('downloads'),
    )
    # Keep core patches isolated, but invoke the provisioned compiler in place:
    # the copied Windows compiler cannot resolve its multilib headers.
    compiler = toolchain.arm_tool(root / 'firmware', 'g++').parents[2]
    (
        firmware / '.tools/data/packages/teensy/hardware/avr/1.62.0/platform.local.txt'
    ).write_text(f'compiler.path={compiler.as_posix()}/\n')
    build.run(build.Build(target_dir=target, source_root=source, output=output))
    manifest = tomlkit.parse((output / 'build.toml').read_text())
    manifest['trace_id'] = trace_id
    manifest['image_sha256'] = hashlib.sha256(
        (output / 'firmware.hex').read_bytes()
    ).hexdigest()
    manifest['baseline'] = BASELINE
    manifest['overlays'] = revisions
    (output / 'build.toml').write_text(tomlkit.dumps(manifest))
    print(f'MIDI trace control: build={trace_id}, allocator=newlib, watchdog=off')


BASELINE = 'd6c6d06'
OVERLAYS = {
    '017ec53': [
        'cntrl/patch_switcher/layout.py',
        'cntrl/patch_switcher/models.py',
        'firmware/include/cntrl/document.h',
        'firmware/include/cntrl/page_layout.h',
    ],
    '8e71150': [
        'cntrl/maker/morningstar/mc/firmware/patch_switcher/target.cpp',
        'cntrl/patch_switcher/layout.py',
    ],
    '39245d3': [
        'firmware/include/cntrl/ui/page_host.h',
        'firmware/include/cntrl/ui/pages.h',
    ],
    'f498944': [
        'cntrl/maker/morningstar/mc/firmware/patch_switcher/target.cpp',
        'cntrl/maker/morningstar/mc/firmware/patch_switcher/usb_forward_device.cpp',
        'cntrl/maker/morningstar/mc/firmware/patch_switcher/usb_forward_device.h',
    ],
}


if __name__ == '__main__':
    run(tyro.cli(Build))
