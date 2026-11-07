from pathlib import Path
import signal
from cntrl.patch_switcher.transport import SerialConnection, SetupClient

def interrupt(signum: int, frame: object) -> None:
    raise KeyboardInterrupt
signal.signal(signal.SIGALRM, interrupt)
signal.alarm(15)
with SerialConnection(Path('/dev/cu.usbmodem144682801')) as conn:
    client = SetupClient(conn)
    print(client.request('CNTRL STATUS'), client.current(), flush=True)
    print(client.request('CNTRL SONGS SIZE'), flush=True)
