import signal
from pathlib import Path
from cntrl.patch_switcher.transport import SerialConnection
from cntrl.maker.morningstar.mc.storage_info import query_storage_info

def stop(signum: int, frame: object) -> None:
    raise KeyboardInterrupt

signal.signal(signal.SIGALRM, stop)
signal.alarm(60)
read = SerialConnection.line
count = 0

def line(self: SerialConnection, limit: int = 256) -> str:
    global count
    result = read(self, limit)
    count += 1
    if count <= 10 or count % 50 == 0:
        print('Reply', count, ' '.join(result.split()[:5]), flush=True)
    return result

SerialConnection.line = line
result = query_storage_info(Path('/dev/cu.usbmodem144682801'))
print('Validated storage report')
signal.alarm(0)
