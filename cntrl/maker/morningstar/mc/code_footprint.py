"""Compare production -Os with -Oz and LTO without uploading firmware."""

import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

import tomlkit
import tyro
from pydantic import BaseModel

from . import boot_switch, toolchain


class Experiment(BaseModel, frozen=True):
    output: Path
    source_root: Path = Path(__file__).resolve().parents[4]


def size_sections(report: str) -> dict[str, int]:
    return {
        p[0]: int(p[1])
        for r in report.splitlines()
        if len(p := r.split()) == 3 and p[0].startswith('.') and p[1].isdigit()
    }


def variant_command(
    command: list[str], optimization: str, lto: bool, *, diagnostic: bool = False
) -> list[str]:
    result = command.copy()
    index = result.index('--fqbn') + 1
    result[index] = result[index].replace(
        'opt=osstd', 'opt=oslto' if lto else 'opt=osstd'
    )
    flags = f'{optimization} --specs=nano.specs'
    if lto:
        flags += ' -flto=auto -fno-fat-lto-objects'
    result[-1:-1] = ['--build-property', f'build.flags.optimize={flags}']
    if diagnostic:
        directory = Path(result[result.index('--build-path') + 1]).parent
        for i, value in enumerate(result):
            if value.startswith('build.flags.ld='):
                result[i] += (
                    f' -Wl,--noinhibit-exec "-Wl,-Map={directory / "link.map"}"'
                )
    return result


def measure(
    root: Path, directory: Path, elf: Path, returncode: int
) -> dict[str, object]:
    result: dict[str, object] = {'compile_exit': returncode}
    if not elf.exists():
        return result
    report = subprocess.check_output(
        [str(toolchain.arm_tool(root, 'size')), '-A', str(elf)], text=True, timeout=60
    )
    (directory / 'memory.txt').write_text(report)
    symbols = subprocess.check_output(
        [
            str(toolchain.arm_tool(root, 'nm')),
            '--print-size',
            '--size-sort',
            '--demangle',
            str(elf),
        ],
        text=True,
        timeout=60,
    )
    (directory / 'symbols.txt').write_text(symbols)
    sections = size_sections(report)
    result.update(
        code_bytes=sum(sections[n] for n in ('.text.code', '.text.itcm'))
        + sections.get('.fini', 0),
        flash_code_bytes=sections.get('.text.code', 0),
        instruction_ram_code_bytes=sections.get('.text.itcm', 0),
        constant_bytes=sections.get('.text.progmem', 0),
        ram1_data_bytes=sections.get('.data', 0) + sections.get('.bss', 0),
        ram2_static_bytes=sections.get('.bss.dma', 0),
        elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
        sections=sections,
    )
    return result


def run(options: Experiment) -> None:
    root = Path(__file__).resolve().parent / 'firmware'
    output, source_root = options.output.resolve(), options.source_root.resolve()
    output.mkdir(parents=True)
    revision = subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=source_root, text=True, timeout=30
    ).strip()
    results: dict[str, object] = {'source_revision': revision, 'variants': {}}
    variants: dict[str, object] = {}
    results['variants'] = variants
    # Generate target capabilities and fonts using the normal build contract first.
    baseline = output / 'Os'
    baseline.mkdir(exist_ok=True)
    print('Building Os baseline', flush=True)
    with (baseline / 'build.log').open('w') as log:
        subprocess.run(
            [
                sys.executable,
                str(root.parent / 'build_entry.py'),
                '--source-root',
                str(source_root),
                '--output',
                str(baseline),
            ],
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
            timeout=1200,
            env=dict(toolchain.environment(root), PYTHONPATH=str(source_root)),
        )
    elf = root / 'build/boot-switch/cntrl/patch_switcher.ino.elf'
    shutil.copyfile(elf, baseline / 'firmware.elf')
    shutil.copyfile(root / '.tools/provenance.toml', output / 'toolchain.toml')
    variants['Os'] = measure(root, baseline, baseline / 'firmware.elf', 0)
    (output / 'results.toml').write_text(tomlkit.dumps(results))
    for name, optimization, lto, diagnostic in VARIANTS:
        directory = output / name
        directory.mkdir(exist_ok=True)
        linker = directory / 'cntrl.ld'
        core = root / '.tools/data/packages/teensy/hardware/avr/1.62.0'
        linker.write_text(
            boot_switch.linker_script(
                (core / 'cores/teensy4/imxrt1062_t41.ld').read_text()
            )
        )
        command = variant_command(
            toolchain.compile_command(root, source_root, directory, linker),
            optimization,
            lto,
            diagnostic=diagnostic,
        )
        (directory / 'command.toml').write_text(tomlkit.dumps({'arguments': command}))
        print(f'Building {name}', flush=True)
        with (directory / 'build.log').open('w') as log:
            completed = subprocess.run(
                command,
                env=toolchain.environment(root),
                stdout=log,
                stderr=subprocess.STDOUT,
                check=False,
                timeout=1200,
            )
        elf = directory / 'cntrl/patch_switcher.ino.elf'
        measurement = measure(root, directory, elf, completed.returncode)
        measurement['diagnostic_only'] = diagnostic
        measurement['usable_build'] = not diagnostic and completed.returncode == 0
        variants[name] = measurement
        (output / 'results.toml').write_text(tomlkit.dumps(results))
        print(
            f'{name}: exit={completed.returncode}, '
            f'code={measurement.get("code_bytes", "unavailable")}, '
            f'diagnostic={diagnostic}',
            flush=True,
        )


def main() -> None:
    run(tyro.cli(Experiment))


VARIANTS = [
    ('Oz', '-Oz', False, False),
    ('Os-lto', '-Os', True, False),
    ('Oz-lto', '-Oz', True, False),
    ('Os-lto-diagnostic', '-Os', True, True),
    ('Oz-lto-diagnostic', '-Oz', True, True),
]


if __name__ == '__main__':
    main()
