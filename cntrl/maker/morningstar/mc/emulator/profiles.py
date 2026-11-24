"""Export catalogue-bound virtual targets from an MC6 Pro target export."""

import os
from hashlib import sha256
from pathlib import Path
from typing import Literal

import tomlkit
import tyro
from pydantic import BaseModel, ValidationError

from .....emulator.backend import bind_target
from .....emulator.errors import EmulatorError, emulator_files
from .....emulator.layout import EmulatorLayout
from .....fonts.catalogue import UI_FONTS
from .....patch_chooser import data_files, models, source_archive
from ..export_fonts import embedded_font


class Export(BaseModel, frozen=True):
    target: Path
    controller: Literal['mc6_pro', 'mc8_pro']
    output: Path


def built_doom_host(source_root: Path) -> Path | None:
    build = source_root / 'cntrl/maker/morningstar/mc/doom/build'
    host = build / ('doom-host.exe' if os.name == 'nt' else 'doom-host')
    return host if host.is_file() and (build / 'e1m1.img').is_file() else None


def adapt(base: models.Target, controller: str, mirror: bool = False) -> models.Target:
    """Keep physical target behavior while binding to catalogue identities.

    The MC6 Pro uses the physical adapter's usable viewports, not the unverified
    catalogue dimensions. MC8 Pro geometry is explicitly provisional.
    """
    if controller not in ('mc6_pro', 'mc8_pro'):
        raise EmulatorError(f'no Morningstar emulator profile for {controller}')
    if mirror and controller != 'mc6_pro':
        raise EmulatorError(
            'live mirroring requires a verified physical target profile'
        )
    if base.buttons != list('ABCDEF') or len(base.displays) != 3:
        raise EmulatorError(
            'profile source must be the six-switch, three-display MC6 Pro target'
        )
    controls: dict[str, object] = {
        k: v.lower() if isinstance(v, str) else v
        for k, v in base.controls.model_dump(exclude={'app_actions'}).items()
    }
    controls['app_actions'] = {
        n.lower(): a for n, a in base.controls.app_actions.items()
    }
    if controller == 'mc8_pro':
        controls.update(
            previous_page='g', next_page='h', reverse_page=None, settings_shortcut=False
        )
    displays = [
        d.model_copy(update={'id': f'lcd-{i}', 'label': f'LCD {i}'})
        for i, d in enumerate(base.displays, 1)
    ]
    if controller == 'mc8_pro':
        displays = [
            base.displays[0].model_copy(
                update={
                    'id': f'lcd-{i}',
                    'label': f'Virtual LCD {i}',
                    'width': 160,
                    'height': 128,
                }
            )
            for i in range(1, 5)
        ]
    data = base.model_dump()
    data.update(
        buttons=list('abcdefgh' if controller == 'mc8_pro' else 'abcdef'),
        button_labels={
            n.lower(): base.button_labels.get(n, n)
            for n in ('ABCDEFGH' if controller == 'mc8_pro' else 'ABCDEF')
        },
        compiled_button_ids={n.lower(): n for n in 'ABCDEF'} if mirror else {},
        controls=controls,
        displays=[d.model_dump() for d in displays],
        compiled_display_ids={f'lcd-{i}': d.id for i, d in enumerate(base.displays, 1)}
        if mirror
        else {},
        animation_display_streams=base.animation_display_streams if mirror else [],
        animation=(
            base.animation.model_dump()
            if mirror and base.animation is not None
            else {
                'width': base.animation.width,
                'height': base.animation.height,
                'streams': [{} for _ in displays],
            }
            if base.animation is not None
            else None
        ),
    )
    return models.Target.model_validate(data)


def export(
    target: Path | None,
    controller: str,
    output: Path,
    mirror: bool = False,
    doom_host: Path | None = None,
) -> Path:
    if target is None:
        target = Path(__file__).parents[1] / 'patch-chooser-target.json'
    errors: list[str] = []
    data = data_files.load_target(
        data_files.DataFile(name=target.stem, paths=[target]), True, errors
    )
    if errors:
        raise EmulatorError('\n'.join(errors))
    try:
        base = models.Target.model_validate(data)
    except ValidationError as error:
        raise EmulatorError(f'{target}: {error}') from error
    profile = adapt(base, controller, mirror)
    if (
        controller == 'mc6_pro'
        and not mirror
        and doom_host
        and 'doom' not in profile.functions
    ):
        profile = profile.model_copy(update={'functions': [*profile.functions, 'doom']})
    layout = EmulatorLayout.load(
        Path(__file__).with_name(controller.replace('_', '-') + '.toml')
    )
    bind_target(layout, profile)
    fonts = output / 'fonts'
    with emulator_files():
        fonts.mkdir(parents=True, exist_ok=True)
    sources: dict[str, str] = {}
    exporter = source_archive.compiler_identity()
    for n in dict.fromkeys([*profile.ui_fonts, *profile.font_sources]):
        destination = fonts / f'{n}.cfont'
        if n in profile.font_sources:
            source = Path(profile.font_sources[n])
            with emulator_files():
                if source.resolve() != destination.resolve():
                    content = source.read_bytes()
                    if not destination.is_file() or destination.read_bytes() != content:
                        destination.write_bytes(content)
        else:
            source = (
                Path(__file__).parents[1]
                / 'firmware'
                / 'patch_chooser'
                / f'font_{n}.cpp'
            )
            with emulator_files():
                key = sha256(
                    repr((exporter, n, UI_FONTS[n])).encode() + source.read_bytes()
                ).hexdigest()
                record = destination.with_suffix('.sha256')
                if (
                    not record.is_file()
                    or not destination.is_file()
                    or record.read_bytes()
                    != (key + sha256(destination.read_bytes()).hexdigest()).encode()
                ):
                    content = embedded_font(source, n, UI_FONTS[n])
                    destination.write_bytes(content)
                    record.write_bytes((key + sha256(content).hexdigest()).encode())
        sources[n] = f'fonts/{destination.name}'
    data = profile.model_dump(mode='json', exclude_none=True)
    data['font_sources'] = sources
    path = output / 'target.toml'
    text = tomlkit.dumps(data)
    with emulator_files():
        if not path.is_file() or path.read_text() != text:
            path.write_text(text)
    return path


def main() -> None:
    options = tyro.cli(Export)
    print(export(options.target, options.controller, options.output))


if __name__ == '__main__':
    main()
