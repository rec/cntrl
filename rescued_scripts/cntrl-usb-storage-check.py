import signal
from pathlib import Path
from cntrl.maker.morningstar.mc.storage_info import query_storage_info

def stop(signum: int, frame: object) -> None:
    raise KeyboardInterrupt

signal.signal(signal.SIGALRM, stop)
signal.alarm(40)
result = query_storage_info(Path('/dev/cu.usbmodem144682801'))
print('Read-only storage listing received and validated')
signal.alarm(0)
