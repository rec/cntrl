import sys
import tempfile
from pathlib import Path
sys.path.insert(0, '/Users/tom/code/cntrl')
from test.test_songs import test_song_library_transfer_against_cpp_receiver, target
from cntrl.patch_switcher.transport import SerialConnection, SetupClient
original = SerialConnection.exchange
request = SetupClient.request
calls = 0
last = ''
def exchange(self: SerialConnection, data: bytes) -> None:
    if self.sequence % 1000 == 0:
        print('frame', self.sequence, 'reply', self.acknowledged, 'last', last, flush=True)
    original(self, data)
def requested(self: SetupClient, command: str) -> str:
    global calls, last
    calls += 1
    last = command[:60]
    if calls % 100 == 0:
        print('request', calls, last, flush=True)
    return request(self, command)
SerialConnection.exchange = exchange
SetupClient.request = requested
with tempfile.TemporaryDirectory() as directory:
    test_song_library_transfer_against_cpp_receiver(Path(directory), True, target.__wrapped__())
print('passed')
