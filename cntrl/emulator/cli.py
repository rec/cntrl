"""Launch or replay a controller against the production native runtime."""

from collections.abc import Callable
from contextlib import nullcontext
from functools import cached_property
from importlib import import_module
from pathlib import Path
from shutil import copyfile
from typing import Annotated, Self

import pint
import tomlkit
import tyro
from PIL import Image
from pydantic import BaseModel, Field, ValidationError, field_validator, model_validator
from tomlkit.exceptions import ParseError

from ..file_lock import writer_lock
from ..localization import catalogue
from ..maker.morningstar.mc.emulator.profiles import built_doom_host
from ..patch_chooser import data_files, device_globals, languages
from ..patch_chooser.models import Target
from ..patch_chooser.setup_bundle import SetupBundle
from .backend import Session, bind_target, build
from .errors import EmulatorError, emulator_files
from .layout import EmulatorLayout
from .mirror import download
from .replay import Scenario


class Emulate(BaseModel, frozen=True):
    """Compile and open a setup on a built-in or explicitly configured controller."""

    setup: Annotated[Path | None, tyro.conf.Positional] = Field(
        default=None, description='Setup source file or compiled setup bundle'
    )
    port: Path | None = Field(
        default=None,
        description='Mirror compiled files and preferences from a connected pedal',
    )
    controller: str | None = Field(
        default=None, description='Built-in controller profile (setup default: mc6-pro)'
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
    doom_host: Path | None = Field(
        default=None,
        description='Built MC6 Pro doom-host executable next to e1m1.img',
    )
    compare_status_fonts: bool = Field(
        default=False,
        description='Preview native-size Status fonts; F cycles samples',
    )
    interactive: bool = Field(
        default=False,
        description='Open the window after replaying the optional startup scenario',
    )
    caret_blink_period: str = Field(
        default='1 second',
        description='Full correction caret blink cycle with units, e.g. "750 ms"',
    )

    @field_validator('caret_blink_period')
    @classmethod
    def valid_caret_blink_period(cls, value: str) -> str:
        caret_blink_interval_ms(value)
        return value

    @cached_property
    def playable_doom_host(self) -> Path | None:
        return self.doom_host or built_doom_host(self.source_root)

    @cached_property
    def caret_blink_interval(self) -> int:
        return caret_blink_interval_ms(self.caret_blink_period)

    @model_validator(mode='after')
    def profile_selection(self) -> Self:
        if (self.setup is None) == (self.port is None):
            raise ValueError('supply a setup file or --port, not both')
        if self.port is not None and (
            self.layout is not None or self.target is not None
        ):
            raise ValueError('--port uses the connected controller profile')
        if self.controller is not None:
            if self.layout is not None or self.target is not None:
                raise ValueError('use --controller or --layout and --target, not both')
        elif self.port is None:
            if (self.layout is None) != (self.target is None):
                raise ValueError('supply both --layout and --target')
            if self.layout is None:
                object.__setattr__(self, 'controller', 'mc6-pro')
        return self


def caret_blink_interval_ms(period: str) -> int:
    try:
        duration = pint.get_application_registry().Quantity(period)
        milliseconds = float(duration.to('millisecond').magnitude)
    except (pint.PintError, TypeError, ValueError) as error:
        raise ValueError('caret blink period must be a duration with units') from error
    if not 2 <= milliseconds <= 2 * (2**31 - 1):
        raise ValueError('caret blink period is outside the timer range')
    return round(milliseconds / 2)


def prepare(
    command: Emulate, compile_setup: Callable[[data_files.Sources, Path], SetupBundle]
) -> tuple[EmulatorLayout, Target, Path, Path]:
    """Resolve a profile and compile source inputs without starting the runtime."""
    if command.port is not None:
        controller, setup, storage = download(
            command.port, command.storage, command.controller
        )
        command = command.model_copy(
            update=dict(controller=controller, setup=setup, storage=storage)
        )
    assert command.setup is not None
    storage = preview_storage(command)
    if command.controller is not None:
        root = Path(__file__).resolve().parents[1]
        matches = [
            p
            for p in (root / 'maker').glob('**/emulator/*.toml')
            if p.stem == command.controller
        ]
        if not matches:
            raise EmulatorError(f'unknown emulator controller: {command.controller}')
        if len(matches) != 1:
            raise EmulatorError(f'ambiguous emulator controller: {command.controller}')
        layout_path = matches[0]
        layout = EmulatorLayout.load(layout_path)
        provider = import_module(
            '.'.join((*layout_path.relative_to(root.parent).parts[:-1], 'profiles'))
        )
        target_path = provider.export(
            target=None,
            controller=layout.controller,
            output=storage / '.emulator-build' / 'profile',
            mirror=command.port is not None,
            doom_host=command.playable_doom_host,
        )
    else:
        assert command.layout is not None and command.target is not None
        layout = EmulatorLayout.load(command.layout)
        target_path = command.target
    target = load_target(target_path)
    bind_target(layout, target)
    with emulator_files():
        storage.mkdir(parents=True, exist_ok=True)
        setup_parent = command.setup.resolve().parent
    if command.port is None:
        language_folder = storage / 'cntrl' / 'languages'
        with emulator_files():
            language_folder.mkdir(parents=True, exist_ok=True)
        recovered_languages = setup_parent / 'languages'
        for tag in catalogue.locales():
            destination = language_folder / f'{tag}.bin'
            recovered = recovered_languages / f'{tag}.bin'
            with emulator_files():
                if destination.exists():
                    continue
                data = recovered.read_bytes() if recovered.exists() else None
            if data is None:
                data = languages.pack(tag)
            with emulator_files():
                destination.write_bytes(data)
    overlay = setup_parent / 'device-globals.toml'
    global_folder = storage / 'cntrl' / 'global'
    with emulator_files():
        needs_globals = overlay.is_file() and not any(
            (global_folder / f'{i}.bin').exists() for i in range(2)
        )
    if needs_globals:
        with emulator_files():
            data = overlay.read_bytes()
        try:
            preference = device_globals.load(data)
        except (UnicodeDecodeError, ParseError, ValidationError) as error:
            raise EmulatorError(f'{overlay}: {error}') from error
        journal = device_globals.journal(preference)
        with emulator_files():
            global_folder.mkdir(parents=True, exist_ok=True)
            (global_folder / '0.bin').write_bytes(journal)
    setup = command.setup
    with emulator_files(), setup.open('rb') as source:
        compiled = source.read(4) in (b'CSTB', b'CNBF')
    if not compiled:
        setup = storage / '.emulator-build' / 'setup' / 'setup.bin'
        with emulator_files():
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
        raise EmulatorError('\n'.join(errors))
    try:
        return Target.model_validate(data)
    except ValidationError as error:
        raise EmulatorError(f'{path}: {error}') from error


def save(session: Session, directory: Path) -> None:
    with emulator_files():
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
        image = Image.frombytes(
            'RGB',
            (display.width, display.height),
            bytes(pixels),
        )
        with emulator_files():
            image.save(directory / f'{display.id}.png')
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
    trace = tomlkit.dumps({'events': records})
    with emulator_files():
        (directory / 'trace.toml').write_text(trace)


def preview_storage(command: Emulate) -> Path:
    if command.storage is not None:
        return command.storage
    if command.controller is not None:
        return Path('.cntrl-emulator') / command.controller
    assert command.layout is not None
    return Path('.cntrl-emulator') / command.layout.stem


def emulate(
    command: Emulate, compile_setup: Callable[[data_files.Sources, Path], SetupBundle]
) -> None:
    # Live mirrors allocate their own fresh snapshot directory.
    lock = (
        writer_lock(preview_storage(command), operation='emulator session')
        if command.port is None
        else nullcontext()
    )
    with lock:
        emulate_locked(command, compile_setup)


def emulate_locked(
    command: Emulate, compile_setup: Callable[[data_files.Sources, Path], SetupBundle]
) -> None:
    layout, target, setup, storage = prepare(command, compile_setup)
    print(
        f'Pedal mirror: {command.port}\nSnapshot: {storage}'
        if command.port is not None
        else f'Setup preview: {command.setup}',
        flush=True,
    )
    for kind, suffix in [('fonts', '.cfont'), ('movies', '.cnsa')]:
        for asset in (setup.parent / kind).glob(f'*{suffix}'):
            destination = storage / 'cntrl' / kind / asset.name
            with emulator_files():
                if destination.exists():
                    if destination.read_bytes() != asset.read_bytes():
                        raise EmulatorError(
                            f'emulator asset differs from saved session: {destination}'
                        )
                else:
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    copyfile(asset, destination)
    executable = build(target, storage / '.emulator-build', command.source_root)
    scenario = command.scenario
    while True:
        with Session(layout, target, executable, storage, setup) as session:
            if command.compare_status_fonts:
                session.compare_status_fonts()
            if scenario is not None:
                Scenario.load(scenario).run(session)
                save(session, command.output or storage / 'replay-output')
                if not command.interactive:
                    return
                scenario = None  # A pedal restart does not replay the launch gesture.
            from .gui import run_window

            if not run_window(
                session,
                caret_blink_interval=command.caret_blink_interval,
                doom_host=command.playable_doom_host,
            ):
                return
