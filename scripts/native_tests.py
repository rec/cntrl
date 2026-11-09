"""Build and run the shared C++ tests with the host's native C++17 compiler."""

import os
import subprocess
import sys
from pathlib import Path

import tomlkit
import tyro
from pydantic import BaseModel, Field


class Run(BaseModel, frozen=True):
    source_root: Path = Field(
        default_factory=lambda: Path(__file__).resolve().parents[1]
    )
    output: Path | None = None


class Suite(BaseModel, frozen=True):
    tests: dict[str, list[str]]


def executable(path: Path) -> Path:
    return path.with_suffix('.exe') if sys.platform == 'win32' else path


def compile_native(
    arguments: list[str], *, check: bool = True
) -> subprocess.CompletedProcess[bytes]:
    output_index = arguments.index('-o') + 1
    output = executable(Path(arguments[output_index])).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.parent / 'tmp'
    temporary.mkdir(exist_ok=True)
    if sys.platform == 'win32':
        command = [
            os.environ.get('CXX', 'cl'),
            '/nologo',
            '/std:c++17',
            '/EHsc',
            '/W4',
            '/WX',
            '/D_CRT_SECURE_NO_WARNINGS',
            '/DNOMINMAX',
            '/utf-8',
            '/Zc:__cplusplus',
        ]
        for a in arguments[1 : output_index - 1]:
            if a.startswith(('-I', '-D')):
                command.append('/' + a[1:])
            elif not a.startswith('-'):
                command.append(a)
        objects = output.parent / (output.stem + '-objects')
        objects.mkdir(exist_ok=True)
        command.extend([f'/Fe{output}', f'/Fo{objects}{os.sep}'])
        command.extend(
            [
                '/link',
                '/MANIFEST:EMBED',
                f'/MANIFESTINPUT:{Path(__file__).with_name("windows-native.manifest")}',
            ]
        )
    else:
        command = [os.environ.get('CXX', arguments[0]), *arguments[1:]]
        command[output_index] = str(output)
    return subprocess.run(
        command,
        check=check,
        timeout=180,
        env=dict(
            os.environ, TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary)
        ),
    )


def run_native(
    arguments: list[str],
    *,
    check: bool = True,
    capture_output: bool = False,
    text: bool = False,
) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(executable(Path(arguments[0])).resolve()), *arguments[1:]],
        check=check,
        capture_output=capture_output,
        text=text,
        timeout=180,
    )


def run(options: Run) -> None:
    root = options.source_root.resolve() / 'firmware'
    output = options.output or root / 'build'
    suite = Suite.model_validate(
        tomlkit.parse(
            Path(__file__).with_name('native-tests.toml').read_text()
        ).unwrap()
    )
    failures: list[str] = []
    for source, arguments in suite.tests.items():
        binary = output / Path(source).with_suffix('')
        result = compile_native(
            [
                'c++',
                '-std=c++17',
                '-Wall',
                '-Wextra',
                '-Werror',
                '-pedantic',
                '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer',
                f'-I{root / "include"}',
                str(root / source),
                '-o',
                str(binary),
            ],
            check=False,
        )
        if result.returncode:
            failures.append(source)
        elif run_native(
            [str(binary), *(str(root / a) for a in arguments)], check=False
        ).returncode:
            failures.append(source)
    if failures:
        sys.exit('Native tests failed: ' + ', '.join(failures))


if __name__ == '__main__':
    run(tyro.cli(Run))
