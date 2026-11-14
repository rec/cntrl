"""Reproduce the mechanical compiler-call migration from the original test suite."""

import ast
import re
import shlex
from pathlib import Path

import tomlkit
import tyro
from pydantic import BaseModel, Field


class Options(BaseModel, frozen=True):
    root: Path = Field(default_factory=lambda: Path(__file__).resolve().parents[1])


def migrate(options: Options) -> None:
    for path in [
        *options.root.glob('test/test_*.py'),
        *options.root.glob('cntrl/**/test/test_*.py'),
    ]:
        source = path.read_text()
        tree = ast.parse(source)
        outputs: set[str] = set()
        for n in ast.walk(tree):
            if (
                isinstance(n, ast.Call)
                and ast.unparse(n.func) == 'subprocess.run'
                and n.args
                and isinstance(n.args[0], ast.List)
            ):
                values = n.args[0].elts
                if (
                    values
                    and isinstance(values[0], ast.Constant)
                    and values[0].value == 'c++'
                ):
                    for i, v in enumerate(values):
                        if isinstance(v, ast.Constant) and v.value == '-o':
                            output = values[i + 1]
                            if isinstance(output, ast.Call) and isinstance(
                                output.args[0], ast.Name
                            ):
                                outputs.add(output.args[0].id)
        replacements: list[tuple[int, int, str]] = []
        lines = source.splitlines(keepends=True)
        offsets = [0]
        for line in lines:
            offsets.append(offsets[-1] + len(line))
        for n in ast.walk(tree):
            if (
                not isinstance(n, ast.Call)
                or ast.unparse(n.func) != 'subprocess.run'
                or not n.args
                or not isinstance(n.args[0], ast.List)
            ):
                continue
            first = n.args[0].elts[0]
            name = ''
            if isinstance(first, ast.Constant) and first.value == 'c++':
                name = 'compile_native'
            elif (
                isinstance(first, ast.Call)
                and first.args
                and isinstance(first.args[0], ast.Name)
                and first.args[0].id in outputs
            ):
                name = 'run_native'
            if name:
                replacements.append(
                    (
                        offsets[n.func.lineno - 1] + n.func.col_offset,
                        offsets[n.func.end_lineno - 1] + n.func.end_col_offset,
                        f'native_tests.{name}',
                    )
                )
        if replacements:
            for start, end, text in sorted(replacements, reverse=True):
                source = source[:start] + text + source[end:]
            source = (
                source.replace(
                    '\nimport pytest\n',
                    '\nimport pytest\n\nfrom scripts import native_tests\n',
                )
                if '\nimport pytest\n' in source
                else source.replace(
                    '\nfrom pathlib import Path\n',
                    '\nfrom pathlib import Path\n\nfrom scripts import native_tests\n',
                )
            )
            path.write_text(source)
    for path in options.root.glob('test/test_*.py'):
        source = path.read_text()
        if 'pty.openpty()' not in source:
            continue
        if path.name == 'test_document_transport.py':
            start = source.index(
                '    master, slave = pty.openpty()',
                source.index('def test_incompatible_usb_replies'),
            )
            end = source.index('\n\ndef test_protocol_error', start)
            body = source[start:end]
            body = body.replace('    master, slave = pty.openpty()\n', '')
            body = body.replace('Path(os.ttyname(slave))', "Path('/unused')")
            body = body.replace(
                '    connection.session = 0xABC',
                "    monkeypatch.setattr(connection, 'connect', lambda: None)\n"
                "    monkeypatch.setattr(connection, 'write_frame', lambda _: None)\n"
                '    connection.session = 0xABC',
            )
            body = body.replace(
                '        os.close(master)\n        os.close(slave)\n', ''
            )
            source = source[:start] + body + source[end:]
        source = source.replace('Path(os.ttyname(slave))', 'Path(port)').replace(
            'os.ttyname(slave)', 'port'
        )
        source = source.replace('connection.fd == -1', 'connection.serial is None')
        # Each old PTY block owns exactly one child and a try/finally cleanup.
        pattern = (
            r'(?P<indent> +)master, slave = pty.openpty\(\)\n'
            r'(?P=indent)process = subprocess.Popen\((?P<args>.*?)\s*,?\s*'
            r'stdin=master, stdout=master\s*\)\n(?P<before>.*?)'
            r'(?P=indent)try:\n(?P<body>.*?)(?P=indent)finally:\n'
            r'(?P<cleanup>.*?)(?P=indent)    os.close\(slave\)\n'
        )

        def replace(match: re.Match[str]) -> str:
            indent = match['indent']
            before = match['before']
            before = ''.join(
                '    ' + a if a.strip() else a for a in before.splitlines(keepends=True)
            )
            return (
                f'{indent}with NativePeer('
                f'{match["args"].strip().rstrip(",")}) as port:\n'
                + before
                + match['body']
            )

        source = re.sub(pattern, replace, source, flags=re.DOTALL)
        source = source.replace('import pty\n', '')
        source = source.replace(
            'from scripts import native_tests\n',
            'from scripts import native_tests\nfrom native_peer import NativePeer\n',
        )
        source = source.replace('        port = Path(port)\n', '').replace(
            '        port = port\n', ''
        )
        path.write_text(source)
    makefile = options.root / 'firmware/Makefile'
    if '$(CXX)' in makefile.read_text():
        tests: dict[str, list[str]] = {}
        recipes = makefile.read_text().replace('\\\n', ' ').splitlines()
        pending: str | None = None
        for line in recipes:
            match = re.search(r'"\$\(ROOT\)/(test/[^\"]+\.cpp)"', line)
            if '$(CXX)' in line:
                pending = match[1] if match else None
            elif pending and line.strip().startswith('"$(ROOT)/build/'):
                tests[pending] = [
                    a.removeprefix('$(ROOT)/') for a in shlex.split(line)[1:]
                ]
                pending = None
        (options.root / 'scripts/native-tests.toml').write_text(
            tomlkit.dumps({'tests': tests})
        )
        makefile.write_text(
            '.DEFAULT_GOAL := test\n'
            'ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))\n'
            'PYTHON ?= python3\n.PHONY: test\ntest:\n'
            '\tcd "$(ROOT)/.." && $(PYTHON) -m pytest -q test\n'
        )


if __name__ == '__main__':
    migrate(tyro.cli(Options))
