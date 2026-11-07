import importlib.util
import runpy
import sys
import cntrl.patch_switcher
spec = importlib.util.spec_from_file_location('cntrl.patch_switcher.transport', '/private/tmp/cntrl-usb-old-transport.py')
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
runpy.run_path('/private/tmp/cntrl-pause-check.py')
