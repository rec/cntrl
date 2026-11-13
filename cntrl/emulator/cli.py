"""Launch or replay a controller against the production native runtime."""

from pathlib import Path
from shutil import copyfile

import tomlkit
from PIL import Image
from pydantic import BaseModel, Field

from ..patch_switcher import data_files
from ..patch_switcher.models import Target
from .backend import Session, bind_target, build
from .layout import EmulatorLayout
from .replay import Scenario


class Emulate(BaseModel, frozen=True):
    layout: Path
    target: Path
    setup: Path = Field(description='Compiled CSTB setup bundle from cntrl compile')
    storage: Path
    source_root: Path = Field(
        default_factory=lambda: Path(__file__).resolve().parents[2]
    )
    scenario: Path | None = None
    output: Path | None = None


def load_target(path: Path) -> Target:
    errors: list[str] = []
    data = data_files.load_target(
        data_files.DataFile(name='target', paths=[path]), True, errors
    )
    if errors:
        raise ValueError('\n'.join(errors))
    return Target.model_validate(data)


def save(session: Session, directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    for display in session.target.displays:
        frame = session.frames[display.id]
        pixels = bytearray(display.width * display.height * 3)
        for index in range(display.width * display.height):
            value = int.from_bytes(frame[index * 2 : index * 2 + 2], 'big')
            pixels[index * 3 : index * 3 + 3] = bytes(
                [
                    (value >> 11) * 255 // 31,
                    ((value >> 5) & 63) * 255 // 63,
                    (value & 31) * 255 // 31,
                ]
            )
        Image.frombytes(
            'RGB',
            (display.width, display.height),
            bytes(pixels),
        ).save(directory / f'{display.id}.png')
    records = [
        dict(
            kind=e.kind,
            endpoint=e.endpoint,
            time_us=e.time_us,
            data=list(e.data),
            message=e.message,
            value=e.value,
        )
        for e in session.messages
    ]
    (directory / 'trace.toml').write_text(tomlkit.dumps({'events': records}))


def emulate(command: Emulate) -> None:
    layout = EmulatorLayout.load(command.layout)
    target = load_target(command.target)
    bind_target(layout, target)
    command.storage.mkdir(parents=True, exist_ok=True)
    for kind, suffix in [('fonts', '.cfont'), ('movies', '.cnsa')]:
        for asset in (command.setup.parent / kind).glob(f'*{suffix}'):
            destination = command.storage / 'cntrl' / kind / asset.name
            if destination.exists():
                if destination.read_bytes() != asset.read_bytes():
                    raise ValueError(
                        f'emulator asset differs from saved session: {destination}'
                    )
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                copyfile(asset, destination)
    executable = build(target, command.storage / '.emulator-build', command.source_root)
    with Session(layout, target, executable, command.storage, command.setup) as session:
        if command.scenario is not None:
            Scenario.load(command.scenario).run(session)
            save(session, command.output or command.storage / 'replay-output')
        else:
            from .gui import run_window

            run_window(session)
