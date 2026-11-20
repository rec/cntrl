"""Export own-only firmware resources and compose an installed MC6 Pro release."""

import hashlib
import shutil
import subprocess
from pathlib import Path

import tomlkit

from ....firmware import distribution
from ....firmware.intel_hex import AddressRange, FirmwareImage, encode_hex, read_hex
from ....localization import catalogue
from ....patch_chooser import data_files, models, resolved, setup_bundle, songs
from ....patch_chooser.cli import compile_to_file
from . import boot_switch, upload_helper


def input_names(source_root: Path) -> list[str]:
    names = (
        subprocess.run(
            [
                'git',
                '-C',
                str(source_root),
                'ls-files',
                '-z',
                '--',
                'firmware',
                'cntrl/firmware',
                'cntrl/fonts',
                'cntrl/localization',
                'cntrl/patch_chooser',
                'cntrl/maker/morningstar/mc',
            ],
            capture_output=True,
            check=True,
        )
        .stdout.decode()
        .split('\0')
    )
    return sorted(
        n
        for n in names
        if n
        and '/distribution/' not in n
        and '/archive/' not in n
        and '/test/' not in n
        and '/tests/' not in n
        and '/emulator/' not in n
        and not Path(n).name.startswith('test_')
        and Path(n).suffix
        in (
            '.py',
            '.cpp',
            '.h',
            '.c',
            '.S',
            '.ino',
            '.ld',
            '.patch',
            '.cfont',
            '.json',
            '.toml',
        )
    )


def input_digest(source_root: Path) -> str:
    digest = hashlib.sha256()
    for name in input_names(source_root):
        digest.update(name.encode() + b'\0')
        digest.update((source_root / name).read_bytes())
    return digest.hexdigest()


def export(
    directory: Path, source_root: Path, components: boot_switch.Components
) -> None:
    """Build a complete resource set with no stock application bytes."""
    root = Path(__file__).parent
    source_commit = subprocess.run(
        ['git', '-C', str(source_root), 'rev-parse', 'HEAD'],
        capture_output=True,
        check=True,
        text=True,
    ).stdout.strip()
    destination = directory / 'distribution'
    destination.mkdir()
    custom = read_hex(components.application)
    (destination / 'application.bin').write_bytes(custom.data)
    shutil.copyfile(components.hook, destination / 'hook.bin')
    shutil.copyfile(directory / 'target.toml', destination / 'target.toml')
    shutil.copytree(directory / 'fonts', destination / 'fonts')
    shutil.copytree(
        Path(__file__).parents[3] / 'localization' / 'packs', destination / 'languages'
    )
    for pack in (destination / 'languages').glob('*.bin'):
        if catalogue.inspect(pack.read_bytes()).language != pack.stem:
            raise ValueError('language pack identity differs from its filename')
    shutil.copyfile(directory / 'toolchain.toml', destination / 'toolchain.toml')
    shutil.copyfile(
        directory / 'compiler-options.toml', destination / 'compiler-options.toml'
    )
    starter = destination / 'starter'
    starter.mkdir()
    shutil.copyfile(root / 'starter.toml', starter / 'starter.toml')
    compile_to_file(
        data_files.Sources(
            document=[str(starter / 'starter.toml')],
            target=[str(destination / 'target.toml')],
        ),
        starter / 'bundle.bin',
    )
    errors: list[str] = []
    target = models.Target.model_validate(
        data_files.load_target(
            data_files.DataFile(name='target', paths=[destination / 'target.toml']),
            True,
            errors,
        )
    )
    if errors:
        raise ValueError('\n'.join(errors))
    (starter / 'songs.bin').write_bytes(songs.encode(songs.Library(), target))
    manifest = distribution.Manifest(
        controller='mc6-pro',
        source_commit=source_commit,
        inputs_sha256=input_digest(source_root),
        application='application.bin',
        application_origin=custom.origin,
        application_entry=custom.entry or 0,
        hook='hook.bin',
        stock_reader=components.stock_reader,
        switch_window_ms=50,
        target='target.toml',
        starter='starter',
        stock=STOCK,
        files={
            p.relative_to(destination).as_posix(): hashlib.sha256(
                p.read_bytes()
            ).hexdigest()
            for p in sorted(destination.rglob('*'))
            if p.is_file()
        },
    )
    (destination / 'manifest.toml').write_text(tomlkit.dumps(manifest.model_dump()))


def prepare(resources: Path, output: Path) -> distribution.Manifest:
    """Compose a package's exact pairing without any build tools or device access."""
    manifest = distribution.read_manifest(resources)
    if manifest.controller != 'mc6-pro' or manifest.stock != STOCK:
        raise ValueError('unsupported MC6 Pro firmware pairing')
    for name in ('compiler-options.toml', 'toolchain.toml'):
        if name not in manifest.files:
            raise ValueError(f'{name}: missing build provenance hash')
    options = tomlkit.parse(
        distribution.resource(resources, 'compiler-options.toml').read_text()
    )
    if not any(
        f'CNTRL_GIT_COMMIT=0x{manifest.source_commit[:7]}' in str(a)
        for a in options['command']
    ):
        raise ValueError('compiler source identity differs from the resource manifest')
    errors: list[str] = []
    target = models.Target.model_validate(
        data_files.load_target(
            data_files.DataFile(name='target', paths=[resources / manifest.target]),
            True,
            errors,
        )
    )
    if errors:
        raise ValueError('\n'.join(errors))
    for path in target.font_sources.values():
        name = Path(path).relative_to(resources.resolve()).as_posix()
        if name not in manifest.files:
            raise ValueError(f'{name}: missing target font hash')
    starter = distribution.resource(resources, manifest.starter)
    fonts = {
        int(p.stem, 16): p.read_bytes() for p in (starter / 'fonts').glob('*.cfont')
    }
    for data in setup_bundle.unpack((starter / 'bundle.bin').read_bytes()).values():
        resolved.prepare_restore(data, fonts, target=target)
    songs.upgrade((starter / 'songs.bin').read_bytes(), fonts, target=target)
    application = distribution.resource(resources, manifest.application).read_bytes()
    custom = FirmwareImage(
        origin=manifest.application_origin,
        entry=manifest.application_entry,
        data=application,
        regions=[
            AddressRange(
                start=manifest.application_origin,
                end=manifest.application_origin + len(application),
            )
        ],
        record_counts={},
        sha256=manifest.files[manifest.application],
    )
    if custom.entry != custom.origin + 0x1000:
        raise ValueError('unexpected relocated application entry')
    stock_path = distribution.cached_stock(
        manifest.stock,
        distribution.cache_directory() / 'cntrl-firmware' / manifest.controller,
    )
    combined = boot_switch.combine(
        read_hex(stock_path),
        custom,
        distribution.resource(resources, manifest.hook).read_bytes(),
        manifest.stock_reader,
    )
    output.mkdir(mode=0o700, parents=True, exist_ok=True)
    for name in manifest.files:
        destination = distribution.resource(output, name)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(distribution.resource(resources, name), destination)
    shutil.copyfile(resources / 'manifest.toml', output / 'manifest.toml')
    image_path = output / 'firmware.hex'
    image_path.write_text(encode_hex(combined, 0x60000000, 0x60001000))
    upload_helper.validate_firmware(read_hex(image_path))
    (output / 'build.toml').write_text(
        tomlkit.dumps({'image': 'firmware.hex', 'target': manifest.target})
    )
    return manifest


def publish(directory: Path, source_root: Path) -> None:
    source = directory / 'distribution'
    manifest = distribution.read_manifest(source)
    if manifest.inputs_sha256 != input_digest(source_root):
        raise ValueError(
            'firmware inputs changed after the build; rebuild before publishing'
        )
    changes = subprocess.run(
        [
            'git',
            '-C',
            str(source_root),
            'diff',
            manifest.source_commit,
            '--',
            *input_names(source_root),
        ],
        capture_output=True,
        check=True,
    ).stdout
    if changes:
        raise ValueError('build from committed firmware inputs before publishing')
    destination = Path(__file__).with_name('distribution')
    destination.mkdir(exist_ok=True)
    # Remove only obsolete resources from the previous release's own manifest.
    if (destination / 'manifest.toml').exists():
        previous = distribution.read_manifest(destination)
        for name in previous.files.keys() - manifest.files.keys():
            distribution.resource(destination, name).unlink()
    for name in [*manifest.files, 'manifest.toml']:
        path = distribution.resource(destination, name)
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(distribution.resource(source, name), path)
    print(f'Published {manifest.controller} firmware source {manifest.source_commit}')


STOCK = distribution.Stock(
    version='3.14.0',
    filename='2026-10-16_MC6_PRO_Firmware_v_3_14_0_261016501_0.hex',
    url='https://github.com/Morningstar-Engineering/MC6-PRO-MIDI-Controller/releases/download/v3.14.0/'
    '2026-10-16_MC6_PRO_Firmware_v_3_14_0_261016501_0.hex',
    sha256=boot_switch.STOCK_SHA256,
)
