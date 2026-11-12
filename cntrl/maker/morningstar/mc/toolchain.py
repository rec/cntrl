"""Pinned host SDK provisioning and build operations. Never accesses a device."""

import hashlib
import os
import platform
import shutil
import subprocess
import tarfile
from pathlib import Path
from urllib.request import urlopen
from zipfile import ZipFile

import tomlkit
import tyro
from pydantic import BaseModel


class Provision(BaseModel, frozen=True):
    root: Path = Path(__file__).parent / 'firmware'


def environment(root: Path) -> dict[str, str]:
    tools = root / '.tools'
    return dict(
        os.environ,
        ARDUINO_DIRECTORIES_DATA=str(tools / 'data'),
        ARDUINO_DIRECTORIES_DOWNLOADS=str(tools / 'downloads'),
        ARDUINO_DIRECTORIES_USER=str(tools / 'user'),
        ARDUINO_UPDATER_ENABLE_NOTIFICATION='false',
    )


def executable(path: Path) -> Path:
    return path.with_suffix('.exe') if os.name == 'nt' else path


def arm_tool(root: Path, name: str) -> Path:
    return executable(
        root
        / '.tools/data/packages/teensy/tools/teensy-compile/15.2.1/arm/bin'
        / f'arm-none-eabi-{name}'
    )


def ensure(root: Path) -> Path:
    tools = root / '.tools'
    cli = executable(tools / 'bin/arduino-cli')
    if not cli.is_file():
        host = f'{platform.system()}-{platform.machine().lower()}'
        archive_name, digest = DOWNLOADS[host]
        archive = tools / 'downloads' / archive_name
        archive.parent.mkdir(parents=True, exist_ok=True)
        with urlopen(
            f'https://github.com/arduino/arduino-cli/releases/download/v1.5.1/{archive_name}',
            timeout=60,
        ) as response:
            archive.write_bytes(response.read())
        if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
            raise ValueError('Arduino CLI archive checksum mismatch')
        cli.parent.mkdir(parents=True, exist_ok=True)
        if archive.suffix == '.zip':
            with ZipFile(archive) as source:
                cli.write_bytes(source.read('arduino-cli.exe'))
        else:
            with tarfile.open(archive) as source:
                member = source.extractfile('arduino-cli')
                assert member is not None
                cli.write_bytes(member.read())
        cli.chmod(0o755)
    core = tools / 'data/packages/teensy/hardware/avr/1.62.0'
    if not core.is_dir():
        for command in (
            ['core', 'update-index'],
            ['core', 'install', 'teensy:avr@1.62.0'],
        ):
            subprocess.run(
                [
                    str(cli),
                    *command,
                    '--additional-urls',
                    'https://www.pjrc.com/teensy/package_teensy_index.json',
                ],
                env=environment(root),
                check=True,
                timeout=900,
            )
    for name in PATCHES:
        apply_patch(core, root / f'{name}.patch')
    provenance = {
        'host': platform.platform(),
        'cli': '1.5.1',
        'core': '1.62.0',
        'compiler': '15.2.1',
        'cli_sha256': hashlib.sha256(cli.read_bytes()).hexdigest(),
        'compiler_sha256': hashlib.sha256(
            arm_tool(root, 'g++').read_bytes()
        ).hexdigest(),
        'patches': {
            n: hashlib.sha256((root / f'{n}.patch').read_bytes()).hexdigest()
            for n in PATCHES
        },
    }
    (tools / 'provenance.toml').write_text(tomlkit.dumps(provenance))
    return cli


def apply_patch(directory: Path, patch: Path) -> None:
    """Verify or apply the existing SDK patches with Git on every host."""
    command = [
        'git',
        'apply',
        '--unidiff-zero',
        '--unsafe-paths',
        f'--directory={directory.resolve().as_posix()}',
    ]
    reverse = subprocess.run(
        [*command, '--check', '--reverse', str(patch.resolve())],
        capture_output=True,
        check=False,
        timeout=30,
    )
    if reverse.returncode:
        subprocess.run(
            [*command, '--check', str(patch.resolve())], check=True, timeout=30
        )
        subprocess.run([*command, str(patch.resolve())], check=True, timeout=30)


def compile_command(
    root: Path,
    source_root: Path,
    work: Path,
    linker: Path,
    *,
    diagnostics: bool = False,
) -> list[str]:
    """Use the production compiler invocation for builds and footprint experiments."""
    root, source_root, work, linker = (
        root.resolve(),
        source_root.resolve(),
        work.resolve(),
        linker.resolve(),
    )
    cli = ensure(root)
    include = (
        f'"-I{(source_root / "firmware/include").as_posix()}" '
        f'"-I{(root / "usb_probe").as_posix()}"'
    )
    common = '-DCNTRL_EXTERNAL_FLASH_CONFIG -DCNTRL_USB_INTERFACE'
    cpp = (
        '-std=gnu++17 -fno-exceptions -fpermissive -fno-rtti '
        '-fno-threadsafe-statics -felide-constructors -Wno-error=narrowing '
        '-Wno-psabi -Wno-maybe-uninitialized -DCNTRL_USB_INTERFACE '
        '-DCNTRL_CABLE_TEST=1 -DCNTRL_ORIGINAL_FIRMWARE=1 '
        '-DCNTRL_PANIC=1 -DCNTRL_SWITCHES=1'
    )
    if diagnostics:
        cpp += ' -DCNTRL_MEMORY_QUALIFICATION'
    properties = {
        'build.flags.ld': (
            '-Wl,--gc-sections,--relax,--no-warn-rwx-segments '
            f'"-T{linker.as_posix()}" "-T{(root / "memory.ld").as_posix()}"'
        ),
        'build.flags.c': f'{common} {include}',
        'build.flags.cpp': f'{cpp} {include}',
        'recipe.hooks.postbuild.1.pattern': '',
        'recipe.hooks.objcopy.postobjcopy.1.pattern': '',
    }
    if (root / 'patch_switcher/heap_trace.cpp').is_file():
        properties['build.flags.ld'] += (
            ' -Wl,--wrap=_malloc_r,--wrap=_calloc_r,--wrap=_realloc_r,'
            '--wrap=_free_r,--wrap=_memalign_r,--wrap=_malloc_usable_size_r'
        )
    command = [
        str(cli),
        'compile',
        '--verbose',
        '--fqbn',
        'teensy:avr:teensy41:usb=serialmidi,speed=600,opt=osstd,keys=en-us',
        '--warnings',
        'all',
        '--build-path',
        str(work / 'cntrl'),
    ]
    for name, value in properties.items():
        command.extend(['--build-property', f'{name}={value}'])
    return [*command, str(root / 'patch_switcher')]


def build(
    root: Path,
    source_root: Path,
    work: Path,
    linker: Path,
    *,
    diagnostics: bool = False,
) -> None:
    root, source_root, work, linker = (
        root.resolve(),
        source_root.resolve(),
        work.resolve(),
        linker.resolve(),
    )
    subprocess.run(
        compile_command(root, source_root, work, linker, diagnostics=diagnostics),
        env=environment(root),
        check=True,
        timeout=900,
    )
    shutil.copyfile(work / 'cntrl/patch_switcher.ino.hex', work / 'cntrl.hex')
    binary, decoded = work / 'image.bin', work / 'from-hex.bin'
    subprocess.run(
        [
            str(arm_tool(root, 'objcopy')),
            '-O',
            'binary',
            str(work / 'cntrl/patch_switcher.ino.elf'),
            str(binary),
        ],
        check=True,
        timeout=60,
    )
    subprocess.run(
        [
            str(arm_tool(root, 'objcopy')),
            '-I',
            'ihex',
            '-O',
            'binary',
            str(work / 'cntrl.hex'),
            str(decoded),
        ],
        check=True,
        timeout=60,
    )
    if binary.read_bytes() != decoded.read_bytes():
        raise ValueError('ELF and HEX image bytes differ')
    report = subprocess.check_output(
        [str(arm_tool(root, 'size')), '-A', str(work / 'cntrl/patch_switcher.ino.elf')],
        text=True,
        timeout=60,
    )
    (work / 'memory.txt').write_text(report)


PATCHES = (
    'no-external-ram',
    'external-flash-config',
    'usb-keyboard',
    'flex-serial-release',
    'serial-rx-diagnostics',
    'usb-midi-interface',
    'usb-host-forward',
    'fonts-in-flash',
)
DOWNLOADS = {
    'Darwin-arm64': (
        'arduino-cli_1.5.1_macOS_ARM64.tar.gz',
        'cb952e8c1621c95ef5f1d17831c945e3d0ec5973f89c557a7ec8feb9c4f7d4c9',
    ),
    'Darwin-x86_64': (
        'arduino-cli_1.5.1_macOS_64bit.tar.gz',
        'c982e940027996bea9901050e95fae99c59c1dcfee54beedecaf28141e7bf2e7',
    ),
    'Linux-x86_64': (
        'arduino-cli_1.5.1_Linux_64bit.tar.gz',
        '28a8e119c498a25607821c36cb2dc49e8463941b261a0d99091baa7bc692dd2b',
    ),
    'Windows-amd64': (
        'arduino-cli_1.5.1_Windows_64bit.zip',
        'fabe42e0eb04d00e776a66178299ff95a46c623dbc260f997e58fd514853dd40',
    ),
}


if __name__ == '__main__':
    ensure(tyro.cli(Provision).root.resolve())
