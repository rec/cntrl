"""Native build reuse follows content, toolchain and effective build options."""

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from cntrl.emulator import backend
from cntrl.fonts import catalogue, ui
from cntrl.patch_switcher import compiler, models


class BuildFiles:
    def __init__(self, directory: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        self.root = directory / 'source'
        self.output = directory / 'output'
        self.source = self.root / 'firmware/emulator/main.cpp'
        self.protocol = self.source.with_name('protocol.h')
        self.header = self.root / 'firmware/include/cntrl/example.h'
        self.manifest = self.root / 'scripts/windows-native.manifest'
        for p in (self.source, self.protocol, self.header, self.manifest):
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text('original input\n')
        self.compiler = directory / (
            'compiler.exe' if sys.platform == 'win32' else 'compiler'
        )
        shutil.copyfile(sys.executable, self.compiler)
        self.compiler.chmod(0o755)
        monkeypatch.setenv('CXX', str(self.compiler))
        data = compiler.read_document(
            Path(__file__).parents[1] / 'firmware/test/target.json'
        )
        self.target = models.Target.model_validate(data)
        self.font = directory / 'font.cfont'
        self.font.write_bytes(
            ui.prepare('classic', [8], catalogue.bundled_sources()['CourierPrime'])
        )
        self.target = self.target.model_copy(
            update={'font_sources': {n: str(self.font) for n in self.target.ui_fonts}}
        )
        self.version = b'test compiler 1'
        self.compilations = 0
        self.fail = False
        monkeypatch.setattr(backend.subprocess, 'run', self.run)

    def run(
        self, command: list[str], *, check: bool, capture_output: bool = False
    ) -> subprocess.CompletedProcess[bytes]:
        if '--version' in command:
            return subprocess.CompletedProcess(command, 0, self.version, b'')
        self.compilations += 1
        output = (
            command[command.index('-o') + 1]
            if '-o' in command
            else next(c[3:] for c in command if c.startswith('/Fe'))
        )
        Path(output).write_text(f'build {self.compilations}')
        if self.fail:
            raise subprocess.CalledProcessError(1, command)
        return subprocess.CompletedProcess(command, 0, b'', b'')


@pytest.fixture
def files(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> BuildFiles:
    return BuildFiles(tmp_path, monkeypatch)


def test_identical_reexported_inputs_reuse_the_executable(files: BuildFiles) -> None:
    executable = backend.build(files.target, files.output, files.root)
    timestamp = executable.stat().st_mtime_ns
    for p in (files.source, files.protocol, files.header, files.font):
        p.write_bytes(p.read_bytes())
        os.utime(p, ns=(p.stat().st_atime_ns, p.stat().st_mtime_ns + 1000000000))
    assert backend.build(files.target, files.output, files.root) == executable
    assert executable.read_text() == 'build 1'
    assert executable.stat().st_mtime_ns == timestamp


@pytest.mark.parametrize(
    'changed',
    [
        'source',
        'protocol',
        'header',
        'added_header',
        'removed_header',
        'target',
        'font',
        'compiler',
        'options',
        'generated_header',
        'manifest',
    ],
)
def test_changed_build_inputs_recompile(
    files: BuildFiles, monkeypatch: pytest.MonkeyPatch, changed: str
) -> None:
    if changed == 'manifest' and sys.platform != 'win32':
        pytest.skip('the embedded manifest is a Windows build input')
    if changed == 'options':
        monkeypatch.setenv('CL', '/O2')
    executable = backend.build(files.target, files.output, files.root)
    if changed in ('source', 'protocol', 'header', 'manifest'):
        path = getattr(files, changed)
        path.write_text('changed input\n')
    elif changed == 'added_header':
        files.header.with_name('added.h').write_text('new header\n')
    elif changed == 'removed_header':
        files.header.unlink()
    elif changed == 'target':
        files.target = files.target.model_copy(
            update={'memory': files.target.memory.model_copy(update={'runtime': 1})}
        )
    elif changed == 'font':
        files.font.write_bytes(
            ui.prepare('classic', [8, 12], catalogue.bundled_sources()['CourierPrime'])
        )
    elif changed == 'compiler':
        files.compiler.write_bytes(b'changed compiler')
        files.version = b'test compiler 2'
    elif changed == 'generated_header':
        original_header = backend.header

        def updated_header(target: models.Target) -> str:
            return original_header(target) + '// changed generator\n'

        monkeypatch.setattr(backend, 'header', updated_header)
    else:
        monkeypatch.setenv('CL', '/Od')
    backend.build(files.target, files.output, files.root)
    assert executable.read_text() == 'build 2'


def test_changed_compiler_location_recompiles(
    files: BuildFiles, monkeypatch: pytest.MonkeyPatch
) -> None:
    executable = backend.build(files.target, files.output, files.root)
    other = files.compiler.with_name('other' + files.compiler.suffix)
    shutil.copyfile(files.compiler, other)
    other.chmod(0o755)
    monkeypatch.setenv('CXX', str(other))
    backend.build(files.target, files.output, files.root)
    assert executable.read_text() == 'build 2'


def test_missing_executable_is_rebuilt(files: BuildFiles) -> None:
    executable = backend.build(files.target, files.output, files.root)
    executable.unlink()
    backend.build(files.target, files.output, files.root)
    assert executable.read_text() == 'build 2'


def test_failed_build_cannot_reuse_a_partial_executable(files: BuildFiles) -> None:
    executable = backend.build(files.target, files.output, files.root)
    original = files.source.read_bytes()
    files.source.write_text('changed source\n')
    files.fail = True
    with pytest.raises(subprocess.CalledProcessError):
        backend.build(files.target, files.output, files.root)
    files.source.write_bytes(original)
    files.fail = False
    backend.build(files.target, files.output, files.root)
    assert executable.read_text() == 'build 3'
