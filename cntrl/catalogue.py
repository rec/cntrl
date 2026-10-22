"""Load controller descriptions with shared definitions and hardware shorthand."""

from importlib.resources.abc import Traversable
from typing import cast

import tomlkit

from . import enums, models


def load_catalogue(path: Traversable) -> dict[str, models.Controller]:
    """Load a catalogue file, excluding its shared ``@`` definitions."""
    data = tomlkit.loads(path.read_text()).unwrap()
    definitions = {k: v for k, v in data.items() if k.startswith('@')}
    return {
        k: models.Controller(
            name=v['name'],
            controls=[models.Button(id=c.lower(), name=c) for c in v['buttons']],
            displays=[_display(i, d) for i, d in enumerate(v['displays'], 1)],
            ports=[
                _port(f'{k}-{i}' if isinstance(v, list) else k, p, definitions)
                for k, v in v['ports'].items()
                for i, p in enumerate(v if isinstance(v, list) else [v], 1)
            ],
        )
        for k, v in data.items()
        if not k.startswith('@')
    }


def _display(index: int, value: str | dict[str, object]) -> models.Display:
    specification = {'size': value} if isinstance(value, str) else value
    dimensions = (
        [int(s) for s in cast(str, specification['size']).split('x')]
        if 'size' in specification
        else []
    )
    if len(dimensions) > 2:
        raise ValueError('display size must have one or two dimensions')
    kind = specification.get(
        'kind', 'graphical' if len(dimensions) == 2 else 'alphanumeric'
    )
    return models.Display.model_validate(
        {
            'id': f'lcd-{index}',
            'name': f'LCD {index}',
            'kind': kind,
            'width': dimensions[0] if dimensions else None,
            'height': dimensions[1] if len(dimensions) == 2 else None,
            'colors': specification.get('color', 1 if kind == 'alphanumeric' else None),
        }
    )


def _port(
    name: str,
    value: str | dict[str, object],
    definitions: dict[str, dict[str, object]],
) -> models.Port | enums.UsbSocket | models.UsbPort:
    if isinstance(value, str) and value.startswith('@'):
        if value in definitions:
            specification = definitions[value]
        else:
            key, _, direction = value.rpartition('-')
            if direction not in ('in', 'out'):
                raise KeyError(value)
            specification = definitions[key] | {'direction': direction}
    else:
        specification = {'kind': value} if isinstance(value, str) else value

    kind = specification.get('kind')
    if kind in enums.UsbSocket:
        if specification.get('host'):
            return models.UsbPort.model_validate(specification)
        return enums.UsbSocket(cast(str, kind))

    kinds = cast(
        list[str], specification['kinds'] if 'kinds' in specification else [kind]
    )
    physical = dict(
        cast(dict[str, object], specification.get('physical', {'connector': 'din'}))
    )
    if 'size' in physical:
        physical['diameter_mm'] = physical.pop('size')
    if physical['connector'] == 'trs':
        physical['contacts'] = 3
    modes: list[models.PortMode] = []
    for kind in kinds:
        base, _, suffix = kind.rpartition('-')
        if suffix in ('in', 'out'):
            kind, direction = base, suffix
        else:
            direction = cast(
                str,
                specification.get(
                    'direction',
                    {'expression': 'in', 'switch': 'in', 'relay': 'out'}.get(kind),
                ),
            )
        wiring = specification.get('wiring') if kind == 'midi' else None
        for w in wiring if isinstance(wiring, list) else [wiring]:
            protocol = {'kind': kind}
            if w is not None:
                protocol['trs_wiring'] = w
            modes.append(
                models.PortMode.model_validate(
                    {
                        'direction': {'in': 'input', 'out': 'output'}[direction],
                        'protocol': protocol,
                    }
                )
            )
    return models.Port.model_validate(
        {'id': name, 'name': name, 'physical': physical, 'modes': modes}
    )
