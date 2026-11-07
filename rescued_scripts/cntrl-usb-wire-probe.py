import importlib.util
from pathlib import Path
import cntrl.patch_switcher
spec = importlib.util.spec_from_file_location('cntrl.patch_switcher.old_transport', '/private/tmp/cntrl-usb-old-transport.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with module.SerialConnection(Path('/dev/cu.usbmodem144682801'), timeout=1) as connection:
    for frame in (b'\x1eCNTRL USB abc 1 0 -\n', b'CNTRL STATUS\n'):
        print('Sending:', repr(frame), flush=True)
        connection.write(frame)
        try:
            print('Received:', repr(connection.line(limit=1300)), flush=True)
        except TimeoutError as error:
            print(error, flush=True)
