"""Launch or replay a controller against the production native runtime."""

from collections.abc import Callable
from importlib import import_module
from pathlib import Path
from shutil import copyfile
from typing import Annotated, Self

import tomlkit
import tyro
from PIL import Image
from pydantic import BaseModel, Field, model_validator

from ..patch_switcher import data_files
from ..patch_switcher.models import Target
from ..patch_switcher.setup_bundle import SetupBundle
from .backend import Session, bind_target, build
from .layout import EmulatorLayout
from .replay import Scenario


class Emulate(BaseModel, frozen=True):
    """Compile and open a setup on a built-in or explicitly configured controller."""

    setup: Annotated[Path, tyro.conf.Positional] = Field(
        description='Setup source file or compiled setup bundle'
    )
    controller: str | None = Field(
        default=None, description='Built-in controller profile name'
    )
    layout: Path | None = None
    target: Path | None = None
    storage: Path | None = Field(
        default=None,
        description='Session directory; defaults to .cntrl-emulator/<controller>',
    )
    source_root: Path = Field(default_factory=Path.cwd)
    scenario: Path | None = None
    output: Path | None = None
    interactive: bool = Field(
        default=False,
        description='Open the window after replaying the optional startup scenario',
    )

    @model_validator(mode='after')
    def profile_selection(self) -> Self:
        if self.controller is not None:
            if self.layout is not None or self.target is not None:
                raise ValueError('use --controller or --layout and --target, not both')
        elif self.layout is None or self.target is None:
            raise ValueError('choose --controller or supply both --layout and --target')
        return self


def prepare(
    command: Emulate, compile_setup: Callable[[data_files.Sources, Path], SetupBundle]
) -> tuple[EmulatorLayout, Target, Path, Path]:
    """Resolve a profile and compile source inputs without starting the runtime."""
    if command.controller is None:
        assert command.layout is not None
        name = command.layout.stem
    else:
        name = command.controller
    storage = command.storage or Path('.cntrl-emulator') / name
    if command.controller is not None:
        root = Path(__file__).resolve().parents[1]
        matches = [
            p
            for p in (root / 'maker').glob('**/emulator/*.toml')
            if p.stem == command.controller
        ]
        if not matches:
            raise ValueError(f'unknown emulator controller: {command.controller}')
        if len(matches) != 1:
            raise ValueError(f'ambiguous emulator controller: {command.controller}')
        layout_path = matches[0]
        layout = EmulatorLayout.load(layout_path)
        provider = import_module(
            '.'.join((*layout_path.relative_to(root.parent).parts[:-1], 'profiles'))
        )
        target_path = provider.export(
            target=None,
            controller=layout.controller,
            output=storage / '.emulator-build' / 'profile',
        )
    else:
        assert command.layout is not None and command.target is not None
        layout = EmulatorLayout.load(command.layout)
        target_path = command.target
    target = load_target(target_path)
    bind_target(layout, target)
    storage.mkdir(parents=True, exist_ok=True)
    setup = command.setup
    with setup.open('rb') as source:
        compiled = source.read(4) in (b'CSTB', b'CNBF')
    if not compiled:
        setup = storage / '.emulator-build' / 'setup' / 'setup.bin'
        setup.parent.mkdir(parents=True, exist_ok=True)
        compile_setup(
            data_files.Sources(
                document=[str(command.setup)], target=[str(target_path)]
            ),
            setup,
        )
    return layout, target, setup, storage


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


def emulate(
    command: Emulate, compile_setup: Callable[[data_files.Sources, Path], SetupBundle]
) -> None:
    layout, target, setup, storage = prepare(command, compile_setup)
    for kind, suffix in [('fonts', '.cfont'), ('movies', '.cnsa')]:
        for asset in (setup.parent / kind).glob(f'*{suffix}'):
            destination = storage / 'cntrl' / kind / asset.name
            if destination.exists():
                if destination.read_bytes() != asset.read_bytes():
                    raise ValueError(
                        f'emulator asset differs from saved session: {destination}'
                    )
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                copyfile(asset, destination)
    executable = build(target, storage / '.emulator-build', command.source_root)
    with Session(layout, target, executable, storage, setup) as session:
        if command.scenario is not None:
            Scenario.load(command.scenario).run(session)
            save(session, command.output or storage / 'replay-output')
        if command.scenario is None or command.interactive:
            from .gui import run_window

            run_window(session)
