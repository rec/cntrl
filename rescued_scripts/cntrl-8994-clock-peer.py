import os
import pty
import subprocess
from pathlib import Path
from cntrl.patch_switcher import compiler, models, resolved, transport
import tomlkit

target = models.Target.model_validate(compiler.read_document(Path('firmware/test/target.json')))
master, slave = pty.openpty()
peer = subprocess.Popen(['/private/tmp/cntrl-8994-clock-peer'], stdin=master, stdout=master)
try:
    with transport.SerialConnection(Path(os.ttyname(slave))) as connection:
        client = transport.SetupClient(connection)
        for name, options in [('Default', {}), ('Clock', {'clock_outputs':['din'], 'clock_bpms':[88,130,401]})]:
            data = compiler.compile_show({'page_settings':options}, {}, [], target)
            client.copy(name, data, False)
            actual = client.download(name)
            assert actual == data
            settings = models.Setup.model_validate(tomlkit.parse(resolved.export(actual)).unwrap()).page_settings
            print(name, settings.clock_outputs, settings.clock_bpms)
finally:
    peer.terminate()
    peer.wait(timeout=5)
    os.close(master)
    os.close(slave)
