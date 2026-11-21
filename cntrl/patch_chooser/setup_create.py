"""Walk a new user through a small, commented setup project."""

import re
import shlex
import subprocess
import sys
from importlib.resources import files
from pathlib import Path
from typing import Annotated

import tomlkit
import tyro
from pydantic import BaseModel

from . import compiler_errors, data_files, global_config, models


class Create(BaseModel, frozen=True):
    """Create a setup and commented instrument files using interactive prompts."""

    output: Annotated[Path | None, tyro.conf.arg(aliases=['-o'])] = None
    """Target directory; prompt with the current directory as the default if omitted."""


def run(command: Create) -> None:
    print('Create a cntrl setup. Press Enter to accept a default; Ctrl-C cancels.')
    output = (
        command.output
        if command.output is not None
        else Path(ask('Target directory', '.'))
    ).expanduser()
    name = ask_name('Setup name', 'setup')
    root = output / f'{name}.toml'
    errors: list[str] = []
    inherited = global_config.resolve_setup(
        {}, data_files.DataFile(name=name, paths=[root]), errors
    )
    if errors:
        raise compiler_errors.CompilationError(errors)
    settings = models.Setup.model_validate(inherited)
    defaults = global_config.user_path()
    print(
        f'Using system defaults: {defaults}'
        if defaults.exists()
        else 'No user system defaults found; using built-in defaults.'
    )
    print('System defaults will not be changed.')
    instruments = settings.instruments
    if instruments:
        print('Inherited instruments: ' + ', '.join(instruments))
    generated = {
        Path('instruments') / f'{i}-{re.sub(r"[^\w-]", "-", n)}.toml': instrument_text(
            n, {}
        ).encode()
        for i, n in enumerate(instruments, 1)
    }
    names = {n.casefold() for n in instruments}
    used_channels = {
        v.get('channel') for v in instruments.values() if isinstance(v, dict)
    }
    print('Add instruments, or leave the name blank to finish when offered.')
    while True:
        alias = ask_name(
            'Instrument name'
            if not names
            else 'Next instrument name (blank to finish)',
            'synth' if not names else '',
            allow_empty=True,
        )
        if not alias:
            break
        if len(alias) > 31:
            print('Instrument names must fit in 31 characters. Choose a shorter name.')
            continue
        path = Path('instruments') / f'{alias}.toml'
        if alias.casefold() in names or any(
            str(p).casefold() == str(path).casefold() for p in generated
        ):
            print(
                'That instrument name or filename is already included. Choose another.'
            )
            continue
        route = ask('MIDI output name (must match your controller target)', 'din')
        channel_default = next((n for n in range(1, 17) if n not in used_channels), 1)
        while True:
            channel_text = ask('MIDI channel (1-16)', str(channel_default))
            if channel_text.isascii() and channel_text.isdecimal():
                channel = int(channel_text)
                if 1 <= channel <= 16:
                    break
            print('Enter a whole number from 1 to 16.')
        names.add(alias.casefold())
        used_channels.add(channel)
        generated[path] = instrument_text(
            alias, {'output': route, 'channel': channel}
        ).encode()
    arguments = ['cntrl', 'compile'] + global_config.expression_arguments(
        data_files.DataFile(name=name, paths=[Path(root.name)])
    )
    arguments.extend(['--target', 'TARGET.toml', '--output', f'{name}.bin'])
    invocation = (
        subprocess.list2cmdline(arguments)
        if sys.platform == 'win32'
        else shlex.join(arguments)
    )
    generated[Path(root.name)] = (
        '# Your setup: preferences omitted here inherit system defaults.\n'
        '# Run from this directory, replacing TARGET.toml with your target file:\n'
        f'# {invocation}\n'
        '# Instrument files below load automatically for compile, copy and emulate.\n'
        '# Read doc/instrument-definitions.md before adding instrument programs.\n'
        '\n'
        + tomlkit.dumps({'instrument_files': [p.as_posix() for p in generated]})
        + '\n# Add patches and cycles when your instrument definitions are ready.\n'
        '# [patches.first]\n'
        '# instrument = "your instrument name"\n'
        '# program = 1\n'
        '#\n'
        '# [patch_cycles]\n'
        '# Live = ["first"]\n'
    ).encode()
    guide = Path(__file__).resolve().parents[2] / 'doc/instrument-definitions.md'
    generated[Path('doc/instrument-definitions.md')] = (
        guide.read_bytes()
        if guide.is_file()
        else files('cntrl.patch_chooser')
        .joinpath('instrument-definitions.md')
        .read_bytes()
    )
    print('\nFiles to create:')
    for path in generated:
        print(f'  {output / path}')
    from .cli import publish_download

    # Use the existing all-files preflight and no-overwrite writer.
    if ask('Create these files? (yes/no)', 'yes').lower() not in ('y', 'yes'):
        print('Cancelled; no files written.')
        return
    publish_download(generated, output)
    print(f'\nCreated {root}. From {output}, compile with:\n{invocation}')
    print('This starter has no new patches or program names. Fill those in next.')


def ask(prompt: str, default: str = '') -> str:
    suffix = f' [{default}]' if default else ''
    return input(f'{prompt}{suffix}: ').strip() or default


def instrument_text(alias: str, routing: dict[str, object]) -> str:
    definition = tomlkit.dumps({'instruments': {alias: routing}})
    programs = tomlkit.dumps({'instruments': {alias: {'programs': {}}}})
    profile = tomlkit.dumps(
        {'definitions': {'profiles': [{'instrument': alias, 'name': 'default'}]}}
    )
    return (
        '# Instrument definition. Fill this in from the instrument MIDI manual.\n'
        '# See ../doc/instrument-definitions.md for instructions for your agent.\n'
        '# Source manual / model / firmware: TODO\n'
        '# output is a controller port name; channel is numbered 1-16.\n'
        + ('# Routing and programs inherit system defaults.\n' if not routing else '')
        + definition
        + '\n# Program names are optional. No bank selection needed? Add programs\n'
        '# to the routing table above: programs = ["First sound", {}, "Third sound"]\n'
        '# Array positions are program numbers, starting at 1; {} skips a slot.\n'
        '# Otherwise uncomment the bank table and supply verified mappings:\n'
        + ''.join('# ' + n + '\n' for n in programs.splitlines())
        + '# 0 = ["First sound", {}, "Third sound"]\n'
        '#\n'
        '# Local profiles replace inherited profile lists; read the guide first.\n'
        + ''.join('# ' + n + '\n' for n in profile.splitlines())
        + '# [definitions.profiles.banks.0]\n'
        '# msb = 0 # Example only. Use verified CC 0 and/or CC 32 values.\n'
        '# lsb = 0 # Omit any component the instrument does not require.\n'
        '#\n'
        '# Useful MIDI controls (CC/NRPN/RPN), ranges, units and SysEx: TODO\n'
        '# Describe only documented behavior; add performance patches in the setup.\n'
    )


def ask_name(prompt: str, default: str = '', *, allow_empty: bool = False) -> str:
    while True:
        name = ask(prompt, default)
        if allow_empty and not name:
            return name
        try:
            return data_files.validate_name(name)
        except ValueError as error:
            print(error)
