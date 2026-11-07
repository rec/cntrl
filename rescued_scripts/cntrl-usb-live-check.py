import runpy
import signal
from pathlib import Path
from cntrl.patch_switcher.transport import SerialConnection, SetupClient

def stop(signum: int, frame: object) -> None:
    raise KeyboardInterrupt

signal.signal(signal.SIGALRM, stop)
signal.alarm(50)
with SerialConnection(Path('/dev/cu.usbmodem144682801'), timeout=2) as connection:
    reader = connection.read_frame
    losses = 0

    def lose_reply() -> str:
        global losses
        result = reader()
        if losses < 3:
            losses += 1
            raise OSError('Injected acknowledgement loss during read-only status')
        return result

    connection.read_frame = lose_reply
    result = SetupClient(connection).request('CNTRL STATUS')
    assert result == 'CNTRL READY 26 22202 66ba575f', result
    assert losses == 3
    print('Live status recovered after three injected lost acknowledgements:', result)
runpy.run_path('/private/tmp/cntrl-pause-check.py')
signal.alarm(0)
