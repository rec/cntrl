"""Apply the reference catalogue to the existing built-in UI literals.

This migration is deliberately restricted to UI sources. It is also useful for
reporting newly introduced untranslated captions when the catalogue grows.
"""

import ast
import re
from pathlib import Path
from string import Formatter

import tyro
from pydantic import BaseModel, Field

from cntrl.localization.catalogue import load


class Convert(BaseModel, frozen=True):
    write: bool = False
    files: list[Path] = Field(default_factory=lambda: [Path(n) for n in FILES])


def printf_template(text: str) -> str:
    return ''.join(
        literal + ('%' + spec if name is not None else '')
        for literal, name, spec, _ in Formatter().parse(text)
    )


def arguments(source: str, opening: int) -> tuple[list[str], int]:
    depth, begin, cursor = 0, opening + 1, opening + 1
    result: list[str] = []
    while cursor < len(source):
        token = STRING.match(source, cursor)
        if token:
            cursor = token.end()
            continue
        char = source[cursor]
        if char == '(':
            depth += 1
        elif char == ')':
            if not depth:
                result.append(source[begin:cursor].strip())
                return result, cursor + 1
            depth -= 1
        elif char == ',' and not depth:
            result.append(source[begin:cursor].strip())
            begin = cursor + 1
        cursor += 1
    raise ValueError('unterminated call')


def convert(path: Path, write: bool) -> None:
    catalogue = {printf_template(m.en): n for n, m in load().messages.items()}
    source = path.read_text()
    prefix = (
        'i18n'
        if path.parent.name == 'ui'
        else 'ui::i18n'
        if str(path).startswith('firmware/include/cntrl/')
        else 'cntrl::ui::i18n'
    )
    edits: list[tuple[int, int, str]] = []
    for call in re.finditer(r'\b(std::snprintf|prompt)\(', source):
        args, end = arguments(source, call.end() - 1)
        index = 2 if call[1] == 'std::snprintf' else 0
        if len(args) <= index or not STRING.fullmatch(args[index]):
            continue
        text = ast.literal_eval(args[index])
        if text not in catalogue:
            continue
        args[index] = f'{prefix}::Message::{catalogue[text]}'
        name = f'{prefix}::format' if index == 2 else 'prompt'
        edits.append((call.start(), end, name + '(' + ', '.join(args) + ')'))
    for start, end, value in reversed(edits):
        source = source[:start] + value + source[end:]
    lines: list[str] = []
    for line in source.splitlines():
        if (
            not line.lstrip().startswith(('#', '//'))
            and 'static_assert' not in line
            and '// i18n: preserve' not in line
        ):

            def replace(match: re.Match[str]) -> str:
                value = ast.literal_eval(match[0])
                if value not in catalogue:
                    return match[0]
                return f'{prefix}::text({prefix}::Message::{catalogue[value]})'

            line = STRING.sub(replace, line)
            if f'{prefix}::text' in line:
                line = line.replace('static const char *', 'const char *')
                line = line.replace('constexpr const char *', 'const char *')
        lines.append(line)
    source = '\n'.join(lines) + '\n'
    if path.name == 'patch_executor.h':
        source = re.sub(
            r'fail\(ui::i18n::text\((ui::i18n::Message::[a-z0-9_]+)\)\)',
            r'fail(\1)',
            source,
        )
    if 'messages.h' not in source:
        include = 'messages.h' if path.parent.name == 'ui' else 'ui/messages.h'
        source = source.replace(
            '#pragma once\n', '#pragma once\n#include "' + include + '"\n', 1
        )
    if write:
        path.write_text(source)
    else:
        for line in source.splitlines():
            if (
                STRING.search(line)
                and re.search('[A-Za-z] ', line)
                and prefix not in line
            ):
                print(path, line.strip())


def main() -> None:
    options = tyro.cli(Convert)
    for path in options.files:
        convert(path, options.write)


STRING = re.compile(r'"(?:[^"\\]|\\.)*"')
FILES = [
    'firmware/include/cntrl/ui/pages.h',
    'firmware/include/cntrl/ui/page_host.h',
    'firmware/include/cntrl/ui/application.h',
    'firmware/include/cntrl/setup_ui.h',
    'firmware/include/cntrl/songs_ui.h',
    'firmware/include/cntrl/controller.h',
    'firmware/include/cntrl/banked_page.h',
    'firmware/include/cntrl/patch_executor.h',
    'firmware/include/cntrl/switches.h',
    'firmware/include/cntrl/midi_runtime.h',
    'firmware/include/cntrl/ui/controls.h',
    'firmware/include/cntrl/ui/firmware_visit.h',
    'firmware/include/cntrl/document.h',
    'firmware/include/cntrl/songs.h',
]


if __name__ == '__main__':
    main()
