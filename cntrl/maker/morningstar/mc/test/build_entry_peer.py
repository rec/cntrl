"""Check package binding in a fresh process with deliberately stale imports."""

import runpy
import sys
from pathlib import Path
from types import ModuleType


directory = Path(sys.argv[1]).resolve()
for suffix in ('', '.build_target', '.boot_switch', '.firmware'):
    name = 'cntrl.maker.morningstar.mc' + suffix
    stale = ModuleType(name)
    stale.__file__ = '/wrong-installation/' + suffix
    sys.modules[name] = stale
namespace = runpy.run_path(str(directory / 'build_entry.py'))
target = namespace['load_target']()
assert Path(target.__file__).resolve() == directory / 'build_target.py'
assert Path(target.boot_switch.__file__).resolve() == directory / 'boot_switch.py'
