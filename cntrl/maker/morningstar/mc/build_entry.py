"""Bind the selected adapter's helpers without importing another installation."""

import sys
from importlib import import_module
from pathlib import Path
from types import ModuleType

import cntrl


def load_target() -> ModuleType:
    directory = Path(__file__).resolve().parent
    prefix = 'cntrl.maker.morningstar.mc'
    for name in list(sys.modules):
        if name == prefix or name.startswith(prefix + '.'):
            del sys.modules[name]
    parent = cntrl
    for name, path in zip(
        ('maker', 'morningstar', 'mc'),
        (directory.parent.parent, directory.parent, directory),
        strict=True,
    ):
        namespace = f'{parent.__name__}.{name}'
        package = ModuleType(namespace)
        package.__package__ = namespace
        package.__path__ = [str(path)]
        sys.modules[namespace] = package
        setattr(parent, name, package)
        parent = package
    return import_module(prefix + '.build_target')


if __name__ == '__main__':
    load_target().main()
