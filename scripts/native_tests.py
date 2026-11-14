"""Shared native compilation and execution for the ordinary pytest suite."""

import os
import subprocess
import sys
from pathlib import Path

import tomlkit
from pydantic import BaseModel


class Suite(BaseModel, frozen=True):
    tests: dict[str, list[str]]


class NativePrograms:
    """Compile each shared source once with the suite's strict native flags."""

    def __init__(self, source_root: Path, output: Path) -> None:
        self.source_root = source_root
        self.output = output
        self.programs: dict[str, Path] = {}

    def build(self, source: str) -> Path:
        if source not in self.programs:
            root = self.source_root / 'firmware'
            binary = self.output / Path(source).with_suffix('')
            compile_native(
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
                ]
            )
            self.programs[source] = executable(binary)
        return self.programs[source]


def cases() -> dict[str, list[str]]:
    return Suite.model_validate(
        tomlkit.parse(
            Path(__file__).with_name('native-tests.toml').read_text()
        ).unwrap()
    ).tests


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
