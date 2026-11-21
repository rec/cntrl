"""Native test binaries survive pytest's disposable compilation directories."""

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from cntrl.file_lock import writer_lock
from cntrl.firmware import native_cache
from scripts import native_tests


class NativeBuild:
    def __init__(self, directory: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        self.source = directory / 'source/main.cpp'
        self.source.parent.mkdir()
        self.source.write_text('int main() { return 0; }\n')
        self.compiler = directory / ('cxx.exe' if sys.platform == 'win32' else 'cxx')
        shutil.copyfile(sys.executable, self.compiler)
        self.compiler.chmod(0o755)
        self.compilations = 0
        self.fail = False
        self.version = b'compiler 1'
        # Keep mocked binaries out of the checkout's real build cache.
        monkeypatch.setattr(
            native_tests, '__file__', str(directory / 'scripts/native_tests.py')
        )
        manifest = directory / 'scripts/windows-native.manifest'
        manifest.parent.mkdir()
        manifest.write_text('manifest')
        monkeypatch.setenv('CXX', str(self.compiler))
        monkeypatch.setattr(native_tests.subprocess, 'run', self.run)

    def run(
        self,
        command: list[str],
        *,
        check: bool,
        capture_output: bool = False,
        timeout: int = 180,
        env: dict[str, str] | None = None,
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
        if self.fail and check:
            raise subprocess.CalledProcessError(1, command)
        return subprocess.CompletedProcess(command, int(self.fail))

    def build(
        self, directory: Path, define: str = '-DVALUE=1', check: bool = True
    ) -> Path:
        directory.mkdir()
        (directory / 'generated.h').write_text('generated header\n')
        output = directory / 'runner'
        native_tests.compile_native(
            [
                'c++',
                '-std=c++17',
                define,
                f'-I{directory}',
                str(self.source),
                '-o',
                str(output),
            ],
            check=check,
        )
        return native_tests.executable(output)


@pytest.fixture
def native_build(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> NativeBuild:
    return NativeBuild(tmp_path, monkeypatch)


def test_new_pytest_directory_reuses_native_binary(
    native_build: NativeBuild, tmp_path: Path
) -> None:
    first = native_build.build(tmp_path / 'first')
    first.write_text('mutated local executable')
    second = native_build.build(tmp_path / 'second')
    assert second.read_text() == 'build 1'
    assert native_build.compilations == 1


@pytest.mark.parametrize(
    'changed', ['source', 'header', 'flags', 'compiler', 'environment']
)
def test_native_build_inputs_invalidate_cache(
    native_build: NativeBuild,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    changed: str,
) -> None:
    header = native_build.source.with_name('example.h')
    header.write_text('header 1')
    native_build.build(tmp_path / 'first')
    if changed == 'source':
        native_build.source.write_text('int main() { return 1; }\n')
    elif changed == 'header':
        header.write_text('header 2')
    elif changed == 'compiler':
        native_build.compiler.write_bytes(b'compiler 2')
        native_build.version = b'compiler 2'
    elif changed == 'environment':
        monkeypatch.setenv('CL', '/O2')
    output = native_build.build(
        tmp_path / 'second', '-DVALUE=2' if changed == 'flags' else '-DVALUE=1'
    )
    assert output.read_text() == 'build 2'


@pytest.mark.parametrize('check', [True, False])
def test_failed_native_compilation_is_not_cached(
    native_build: NativeBuild, tmp_path: Path, check: bool
) -> None:
    native_build.fail = True
    if check:
        with pytest.raises(subprocess.CalledProcessError):
            native_build.build(tmp_path / 'first', check=check)
    else:
        native_build.build(tmp_path / 'first', check=check)
    native_build.fail = False
    output = native_build.build(tmp_path / 'second')
    assert output.read_text() == 'build 2'


def test_native_cache_evicts_the_least_recently_used_build(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(native_cache, 'MAX_CACHE_BYTES', 8)
    cache = tmp_path / 'cache'
    output = tmp_path / 'program'
    output.write_bytes(b'code')
    first, second, third = ('1' * 64, '2' * 64, '3' * 64)
    native_cache.store(cache, first, output)
    native_cache.store(cache, second, output)
    os.utime(cache / first, (1, 1))
    os.utime(cache / second, (2, 2))
    unrelated = cache / 'notes.txt'
    unrelated.write_bytes(b'keep this file')

    assert native_cache.restore(cache, first, tmp_path / 'restored')
    native_cache.store(cache, third, output)

    assert not native_cache.restore(cache, second, tmp_path / 'missing')
    assert native_cache.restore(cache, first, tmp_path / 'first')
    assert native_cache.restore(cache, third, tmp_path / 'third')
    assert unrelated.read_bytes() == b'keep this file'


def test_native_cache_contention_leaves_built_output_available(tmp_path: Path) -> None:
    cache = tmp_path / 'cache'
    output = tmp_path / 'program'
    output.write_bytes(b'code')
    key = '1' * 64
    native_cache.store(cache, key, output)

    with writer_lock(cache, operation='native cache operation'):
        assert not native_cache.restore(cache, key, tmp_path / 'restored')
        native_cache.store(cache, '2' * 64, output)

    assert output.read_bytes() == b'code'
    assert not native_cache.restore(cache, '2' * 64, tmp_path / 'other')
    assert native_cache.restore(cache, key, tmp_path / 'restored')


def test_oversized_build_keeps_smaller_cached_builds(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(native_cache, 'MAX_CACHE_BYTES', 4)
    cache = tmp_path / 'cache'
    output = tmp_path / 'program'
    output.write_bytes(b'code')
    native_cache.store(cache, '1' * 64, output)
    output.write_bytes(b'bigger code')
    native_cache.store(cache, '2' * 64, output)

    assert output.read_bytes() == b'bigger code'
    assert not native_cache.restore(cache, '2' * 64, tmp_path / 'oversized')
    assert native_cache.restore(cache, '1' * 64, tmp_path / 'smaller')
