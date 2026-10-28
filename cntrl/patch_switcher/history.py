"""Named setup snapshots and semantic comparisons through the existing USB link."""

import sys
import time
from enum import StrEnum, auto
from pathlib import Path
from typing import Annotated

import tomlkit
import tyro
from pydantic import BaseModel

from . import data_files, resolved, transport


class Action(StrEnum):
    list = auto()
    save = auto()
    download = auto()
    compare = auto()
    restore = auto()
    delete = auto()
    clear = auto()


class History(BaseModel, frozen=True):
    """List, save, download, compare, restore or delete complete setup revisions."""

    name: Annotated[str, tyro.conf.Positional]
    port: Path
    action: Annotated[Action, tyro.conf.Positional] = Action.list
    revision: int = 0
    label: str = ''
    output: Path | None = None
    yes: bool = False


class Change(BaseModel, frozen=True):
    path: str
    before: object
    after: object


def compare(before: bytes, after: bytes) -> list[Change]:
    first = tomlkit.parse(resolved.export(before)).unwrap()
    second = tomlkit.parse(resolved.export(after)).unwrap()
    result: list[Change] = []

    def visit(path: str, old: object, new: object) -> None:
        if isinstance(old, dict) and isinstance(new, dict):
            for k in sorted(old.keys() | new.keys()):
                if k not in old or k not in new:
                    result.append(
                        Change(
                            path=f'{path}.{k}'.lstrip('.'),
                            before=old.get(k),
                            after=new.get(k),
                        )
                    )
                else:
                    visit(f'{path}.{k}', old[k], new[k])
        elif isinstance(old, list) and isinstance(new, list):
            for i in range(max(len(old), len(new))):
                if i >= len(old) or i >= len(new):
                    result.append(
                        Change(
                            path=f'{path}[{i}]'.lstrip('.'),
                            before=old[i] if i < len(old) else None,
                            after=new[i] if i < len(new) else None,
                        )
                    )
                else:
                    visit(f'{path}[{i}]', old[i], new[i])
        elif type(old) is not type(new) or old != new:
            result.append(Change(path=path.lstrip('.'), before=old, after=new))

    visit('', first, second)
    return result


def run(command: History) -> None:
    name = data_files.validate_name(command.name).encode('ascii').hex()
    action = command.action
    if command.revision < 0 or command.revision >= 2**64 - 1:
        raise ValueError('revision out of range')
    if action in ('download', 'compare', 'restore', 'delete') and not command.revision:
        raise ValueError('choose --revision')
    if action in ('delete', 'clear') and not command.yes:
        if not sys.stdin.isatty() or input(
            f'{action} history for {command.name}? [y/N] '
        ).lower() not in ('y', 'yes'):
            raise ValueError('deletion requires confirmation or --yes')
    with transport.SerialConnection(command.port) as connection:
        client = transport.SetupClient(connection)
        prefix = 'CNTRL HISTORY'
        if action == 'list':
            before = 2**64 - 1
            while True:
                response = client.request(f'{prefix} LIST {name} {before}')
                if response == 'CNTRL END':
                    break
                parts = response.split()
                if len(parts) != 6 or parts[:2] != ['CNTRL', 'REVISION']:
                    raise ValueError(f'invalid revision response: {response}')
                number = int(parts[2])
                if not 0 < number < before:
                    raise ValueError('invalid revision order')
                label = (
                    '' if parts[5] == '-' else bytes.fromhex(parts[5]).decode('ascii')
                )
                print(f'{number}  {label}  timestamp={parts[3]}  bytes={parts[4]}')
                before = number
        elif action == 'save':
            label = (
                data_files.validate_name(command.label).encode('ascii').hex()
                if command.label
                else '-'
            )
            client.ok(f'CNTRL TIME {int(time.time())}')
            client.ok(f'{prefix} SAVE {name} 0 {label}')
        elif action in ('download', 'compare'):
            data = client.download(command.name, command.revision)
            if action == 'compare':
                for c in compare(client.download(command.name), data):
                    print(f'{c.path}: {c.before!r} -> {c.after!r}')
            elif command.output is None:
                raise ValueError('download requires --output (exact compiled bytes)')
            else:
                with command.output.open('xb') as file:
                    file.write(data)
        else:
            operation = 'RESTORE' if action == 'restore' else 'DELETE'
            client.ok(
                f'{prefix} {operation} {name} '
                f'{0 if action == "clear" else command.revision}'
            )
