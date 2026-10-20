"""Expand hardware counts and labels into controller descriptions."""

from importlib.resources import files
from typing import cast

import tomlkit

from . import enums, models


def load_catalogue(name: str) -> dict[str, models.Controller]:
    """Read a bundled hardware table and return independent controllers."""
    data = tomlkit.loads(
        files('cntrl').joinpath('data', f'{name}.toml').read_text()
    ).unwrap()
    return {
        k: models.Controller(
            name=v['name'],
            controls=[models.Button(id=c.lower(), name=c) for c in v['buttons']],
            displays=[
                d
                for k, c in v['displays'].items()
                for d in _expand(k, c, models.Display)
            ],
            ports=[
                p
                for k, c in v['ports'].items()
                for p in (
                    [enums.UsbSocket(c)]
                    if isinstance(c, str)
                    else _expand(k, c, models.Port)
                )
            ],
        )
        for k, v in data.items()
    }


def _expand[T: models.Display | models.Port](
    key: str,
    value: dict[str, object],
    kind: type[T],
) -> list[T]:
    definition = value.copy()
    quantity = cast(int | list[str], definition.pop('instances'))
    if isinstance(quantity, int):
        if quantity < 1:
            raise ValueError(f'{key}: count must be positive')
        labels = [key] if quantity == 1 else [f'{key}-{i + 1}' for i in range(quantity)]
    else:
        labels = quantity
    return [
        kind.model_validate(definition | {'id': s.lower().replace(' ', '-'), 'name': s})
        for s in labels
    ]
