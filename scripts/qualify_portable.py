"""Qualify core sources and a relocated export without an implementation tree."""

import ast
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

import tomlkit
import tyro
from pydantic import BaseModel, Field

from cntrl.patch_switcher import data_files, models
from cntrl.patch_switcher.compiler import read_document


class Qualification(BaseModel, frozen=True):
    export: Path
    output: Path
    source_root: Path = Field(
        default_factory=lambda: Path(__file__).resolve().parents[1]
    )


def qualify(options: Qualification) -> None:
    check_dependencies(options.source_root)
    options.output.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix='cntrl-portable-') as directory:
        root = Path(directory)
        for name in SOURCE_DIRECTORIES:
            shutil.copytree(
                options.source_root / name,
                root / name,
                ignore=ignored_files,
            )
        shutil.copyfile(options.source_root / 'pyproject.toml', root / 'pyproject.toml')
        shutil.copytree(options.export, root / 'export')
        environment = dict(os.environ, PYTHONPATH=str(root))
        commands = {
            'python': [sys.executable, '-m', 'pytest', '-q', 'test'],
            'native': ['make', '-C', 'firmware', 'test'],
        }
        for name, command in commands.items():
            with (options.output / f'{name}.log').open('w') as log:
                subprocess.run(
                    command,
                    cwd=root,
                    env=environment,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    check=True,
                )
        source = read_document(root / 'firmware/test/show.json')
        if not isinstance(source, dict):
            raise ValueError('portable compile fixture must be a table')
        errors: list[str] = []
        target = models.Target.model_validate(
            data_files.load_target(
                data_files.DataFile(name='target', paths=[root / 'export/target.toml']),
                True,
                errors,
            )
        )
        if errors:
            raise ValueError('\n'.join(errors))
        families = [f.name for f in target.fonts if f.name in target.font_sources]
        if not families:
            raise ValueError('export qualification requires a declared font source')
        source['style'] = {'font_name': families[0], 'horizontal_scale': 1.25}
        fixture = root / 'qualification.toml'
        fixture.write_text(tomlkit.dumps(source))
        definitions = root / 'definitions.toml'
        definitions.write_text(
            tomlkit.dumps(
                {
                    'profiles': [
                        {
                            'instrument': 'synth',
                            'name': 'stock',
                            'banks': {'0': {'msb': 0}},
                        }
                    ]
                }
            )
        )
        executable = Path(sys.executable).with_name('cntrl')
        compiled = root / 'qualified.bin'
        with (options.output / 'export.log').open('w') as log:
            subprocess.run(
                [
                    str(executable),
                    'compile',
                    str(fixture),
                    '--target',
                    str(root / 'export/target.toml'),
                    '--definitions',
                    str(definitions),
                    '--output',
                    str(compiled),
                ],
                cwd=root,
                env=environment,
                stdout=log,
                stderr=subprocess.STDOUT,
                check=True,
            )
        if not compiled.is_file() or not list((root / 'fonts').glob('*.cfont')):
            raise ValueError('export qualification did not prepare native font assets')
        (options.output / 'result.toml').write_text(
            tomlkit.dumps(
                {
                    'portable_python': True,
                    'portable_native': True,
                    'relocated_export': True,
                    'compiled_bytes': compiled.stat().st_size,
                }
            )
        )
    print(
        f'Portable source and relocated export qualification passed: {options.output}'
    )


def check_dependencies(source_root: Path) -> None:
    """Reject implementation dependencies before isolation can hide them."""
    sources = [
        p
        for n in SOURCE_DIRECTORIES
        for p in (source_root / n).rglob('*')
        if p.is_file()
        and not any(n in p.relative_to(source_root).parts for n in IGNORED_DIRECTORIES)
        and not any(n == 'build' for n in p.relative_to(source_root).parts[:-1])
        and (
            p.suffix
            in (
                '.py',
                '.h',
                '.hpp',
                '.c',
                '.cc',
                '.cpp',
                '.ino',
                '.ld',
                '.lds',
                '.s',
                '.S',
            )
            or p.name in ('Makefile', 'makefile', 'build')
        )
    ]
    python = {
        p: ast.parse(p.read_text(), filename=str(p))
        for p in sources
        if p.suffix == '.py'
    }
    # Native compile fixtures deliberately include headers generated into a
    # temporary directory. Their names are declared by portable Python sources.
    generated_headers: set[str] = set()
    for tree in python.values():
        paths = {
            t.id: n.value
            for n in ast.walk(tree)
            if isinstance(n, ast.Assign)
            for t in n.targets
            if isinstance(t, ast.Name)
        }
        for node in ast.walk(tree):
            if not (
                isinstance(node, ast.Call)
                and isinstance(node.func, ast.Attribute)
                and node.func.attr in ('write_text', 'write_bytes')
            ):
                continue
            destination = node.func.value
            if isinstance(destination, ast.Name):
                destination = paths.get(destination.id, destination)
            generated_headers.update(
                Path(n.value).name
                for n in ast.walk(destination)
                if isinstance(n, ast.Constant)
                and isinstance(n.value, str)
                and Path(n.value).suffix in ('.h', '.hpp')
            )
    errors: list[str] = []
    for path in sources:
        relative = path.relative_to(source_root)
        resolved = path.resolve()
        if (
            not resolved.is_relative_to(source_root.resolve())
            or 'maker' in resolved.relative_to(source_root.resolve()).parts
        ):
            errors.append(f'{relative}: source alias escapes the portable tree')
            continue
        text = path.read_text()
        host_fixture = relative.parts[:2] == ('firmware', 'test')
        production = relative.parts[0] not in ('test',) and not host_fixture
        if path.suffix in ('.ld', '.lds', '.s', '.S'):
            errors.append(
                f'{relative}: linker/assembly source belongs to an implementation'
            )
        if path in python:
            for node in ast.walk(python[path]):
                modules = (
                    [n.name for n in node.names]
                    if isinstance(node, ast.Import)
                    else [
                        node.module or '',
                        *[f'{node.module or ""}.{n.name}' for n in node.names],
                    ]
                    if isinstance(node, ast.ImportFrom)
                    else []
                )
                if isinstance(node, (ast.Import, ast.ImportFrom)) and any(
                    'maker' in n.split('.') for n in modules
                ):
                    errors.append(
                        f'{relative}:{node.lineno}: implementation package import'
                    )
                if isinstance(node, ast.Constant) and isinstance(node.value, str):
                    if IMPLEMENTATION_PATH.search(node.value):
                        errors.append(
                            f'{relative}:{node.lineno}: implementation path reference'
                        )
                    if production and CPU_RECIPE.search(node.value):
                        errors.append(
                            f'{relative}:{node.lineno}: processor/toolchain command'
                        )
                    if production and CPP_FRAGMENT.search(node.value):
                        errors.extend(
                            check_cpp(
                                node.value, path, source_root, generated_headers, False
                            )
                        )
                if (
                    production
                    and isinstance(node, (ast.Assign, ast.AnnAssign))
                    and isinstance(node.value, ast.Constant)
                    and isinstance(node.value.value, int)
                    and node.value.value > 0
                ):
                    targets = (
                        node.targets if isinstance(node, ast.Assign) else [node.target]
                    )
                    if any(
                        isinstance(t, ast.Name) and GEOMETRY_NAME.fullmatch(t.id)
                        for t in targets
                    ):
                        errors.append(
                            f'{relative}:{node.lineno}: fixed physical display geometry'
                        )
        elif path.suffix in ('.h', '.hpp', '.c', '.cc', '.cpp', '.ino'):
            errors.extend(
                check_cpp(text, path, source_root, generated_headers, host_fixture)
            )
        else:
            if (
                IMPLEMENTATION_PATH.search(text)
                or CPU_RECIPE.search(text)
                or FIXED_GEOMETRY.search(text)
            ):
                errors.append(f'{relative}: implementation/toolchain build recipe')
    if errors:
        raise ValueError(
            'Portable dependency boundary failed:\n' + '\n'.join(dict.fromkeys(errors))
        )


def check_cpp(
    text: str,
    path: Path,
    source_root: Path,
    generated_headers: set[str],
    host_fixture: bool,
) -> list[str]:
    relative = path.relative_to(source_root)
    errors = []
    if any(
        re.match(r'^\s*#\s*include\b', line) and not INCLUDES.match(line)
        for line in text.splitlines()
    ):
        errors.append(f'{relative}: computed implementation include')
    for match in INCLUDES.finditer(text):
        opening, header = match.groups()
        if IMPLEMENTATION_PATH.search(header):
            errors.append(f'{relative}: implementation include {header!r}')
            continue
        if opening == '<':
            standard = header in CPP_HEADERS or header in C_HEADERS
            if not (
                standard
                or host_fixture
                and header in HOST_HEADERS
                or header.startswith('cntrl/')
                and (source_root / 'firmware/include' / header).is_file()
            ):
                errors.append(f'{relative}: nonportable external include {header!r}')
        elif (
            not any(
                p.is_file()
                and p.resolve().is_relative_to(source_root.resolve())
                and 'maker' not in p.resolve().relative_to(source_root.resolve()).parts
                for p in (
                    path.parent / header,
                    source_root / 'firmware/include' / header,
                )
            )
            and Path(header).name not in generated_headers
        ):
            errors.append(f'{relative}: unresolved implementation include {header!r}')
    code = CPP_COMMENTS_AND_STRINGS.sub('', text)
    if PLATFORM_CODE.search(code):
        errors.append(f'{relative}: processor/register/linker/storage-placement code')
    if not host_fixture and relative.parts[0] != 'test' and FIXED_GEOMETRY.search(code):
        errors.append(f'{relative}: fixed physical display geometry')
    return errors


def ignored_files(directory: str, names: list[str]) -> list[str]:
    """Keep executable fixtures named build; exclude only build directories."""
    return [
        n
        for n in names
        if n in IGNORED_DIRECTORIES or n == 'build' and (Path(directory) / n).is_dir()
    ]


SOURCE_DIRECTORIES = (
    'cntrl',
    'firmware',
    'test',
    'doc',
    'scripts',
    'instruments',
    'examples',
)
IGNORED_DIRECTORIES = ('maker', '__pycache__', '.pytest_cache')
IMPLEMENTATION_PATH = re.compile(r'(?:[/\\.]maker(?:[/\\.]|$)|^maker[/\\.])')
INCLUDES = re.compile(r'^\s*#\s*include\s*([<"])([^>"\n]+)[>"]', re.MULTILINE)
CPP_FRAGMENT = re.compile(r'#\s*(?:include|pragma|define)|\bnamespace\s+\w+|;\s*$')
CPP_COMMENTS_AND_STRINGS = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.DOTALL
)
PLATFORM_CODE = re.compile(
    r'\b(?:__attribute__|__declspec|__asm__|asm|volatile)\b'
    r'|\[\[(?:gnu|clang)::'
    r'|\bextern\b[^;\n]*\b_+[A-Za-z]\w*'
    r'|^\s*[A-Z][A-Z0-9_]+\s*[|&^]?=(?!=)'
    r'|^\s*[A-Z][A-Z0-9_]{4,}\s+(?:const\s+)?(?:[a-z]\w*|std::)\b',
    re.MULTILINE,
)
FIXED_GEOMETRY = re.compile(
    r'\b\w*(?:display|screen|panel|viewport|framebuffer)_?(?:width|height|count)\b[^;\n]{0,80}(?:=|\breturn)\s*[1-9]\d*\b'
    r'|\b(?:framebuffer|frame_buffer|screen_pixels|display_pixels|pixels_|cache_|previous_)\s*(?:\[[\s\d+*()-]+\]){2,}',
    re.IGNORECASE,
)
CPU_RECIPE = re.compile(
    r'-(?:none|unknown)-(?:eabi|elf)|(?:^|\s)-m(?:cpu|arch|thumb)|(?:^|\s)-T\S'
)
C_HEADERS = set(
    (
        'assert.h complex.h ctype.h errno.h fenv.h float.h inttypes.h iso646.h '
        'limits.h locale.h math.h setjmp.h signal.h stdalign.h stdarg.h '
        'stdatomic.h stdbool.h stddef.h stdint.h stdio.h stdlib.h stdnoreturn.h '
        'string.h tgmath.h threads.h time.h uchar.h wchar.h wctype.h'
    ).split()
)
HOST_HEADERS = {'poll.h', 'unistd.h'}
CPP_HEADERS = set(
    (
        'algorithm any array atomic barrier bit bitset cassert cctype cerrno '
        'cfenv cfloat charconv chrono cinttypes climits clocale cmath codecvt '
        'compare complex concepts condition_variable coroutine csetjmp csignal '
        'cstdarg cstddef cstdint cstdio cstdlib cstring ctgmath ctime cuchar '
        'cwchar cwctype deque exception execution expected filesystem format '
        'forward_list fstream functional future initializer_list iomanip ios '
        'iosfwd iostream istream iterator latch limits list locale map memory '
        'memory_resource mutex new numbers numeric optional ostream print queue '
        'random ranges ratio regex scoped_allocator semaphore set shared_mutex '
        'source_location span sstream stack stdexcept stop_token streambuf '
        'string string_view strstream syncstream system_error thread tuple '
        'type_traits typeindex typeinfo unordered_map unordered_set utility '
        'valarray variant vector version'
    ).split()
)
GEOMETRY_NAME = re.compile(
    r'\w*(?:display|screen|panel|viewport|framebuffer)_?(?:width|height|count)',
    re.IGNORECASE,
)


if __name__ == '__main__':
    qualify(tyro.cli(Qualification))
