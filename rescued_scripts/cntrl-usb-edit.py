from pathlib import Path
p=Path('cntrl/patch_switcher/transport.py')
s=p.read_text().replace('import select\n','import select\nimport secrets\nimport sys\n')
a=s.index('class SerialConnection:'); b=s.index('\n\nclass SetupEntry',a)
s=s[:a]+'''class SerialConnection:
    """A resumable, acknowledged byte stream over a replaceable serial fd."""

    def __init__(self, port: Path, timeout: float = 30) -> None:
        self.port = port
        self.timeout = timeout
        self.fd = -1
        self.session = secrets.randbits(64) or 1
        self.sequence = 1
        self.acknowledged = 0
        self.received = bytearray()
        self.previous: list[int | list[bytes | int]] = []

    def __enter__(self) -> 'SerialConnection':
        self.exchange(b'')
        return self

    def __exit__(self, *args: object) -> None:
        self.close()

    def close(self) -> None:
        if self.fd < 0:
            return
        try:
            termios.tcsetattr(self.fd, termios.TCSANOW, self.previous)
        except (OSError, termios.error):
            pass  # The USB device may have disappeared during the command.
        finally:
            os.close(self.fd)
            self.fd = -1

    def connect(self) -> None:
        self.fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            self.previous = termios.tcgetattr(self.fd)
            settings = termios.tcgetattr(self.fd)
            settings[0] = settings[1] = settings[3] = 0
            settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            settings[4] = settings[5] = termios.B115200
            settings[6][termios.VMIN] = 0
            settings[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, settings)
            termios.tcflush(self.fd, termios.TCIFLUSH)
        except (OSError, termios.error):
            os.close(self.fd)
            self.fd = -1
            raise

    def exchange(self, data: bytes) -> None:
        frame = (
            f'\\x1eCNTRL USB {self.session:x} {self.sequence} '
            f'{self.acknowledged} {data.hex() or "-"}\\n'
        ).encode('ascii')
        failures = 0
        delay = 1
        while True:
            try:
                if self.fd < 0:
                    self.connect()
                self.write_frame(frame)
                while True:
                    response = self.read_frame().split()
                    if response == ['CNTRL', 'USB', 'LOST']:
                        raise OSError('USB session lost; waiting for the original session')
                    if len(response) != 6 or response[:2] != ['CNTRL', 'USB']:
                        raise OSError('Invalid USB transport response')
                    try:
                        session = int(response[2], 16)
                        sequence, reply = int(response[3]), int(response[4])
                        payload = b'' if response[5] == '-' else bytes.fromhex(response[5])
                    except ValueError as error:
                        raise OSError('Invalid USB transport response') from error
                    if session != self.session or sequence < self.sequence:
                        continue
                    if sequence != self.sequence:
                        raise OSError('Unexpected USB sequence')
                    if payload and reply == self.acknowledged + 1:
                        self.received.extend(payload)
                        self.acknowledged = reply
                    elif payload and reply != self.acknowledged:
                        raise OSError('Unexpected USB reply sequence')
                    self.sequence += 1
                    return
            except (OSError, termios.error):
                self.close()
                failures += 1
                if failures >= 3:
                    print(
                        f'Waiting {delay} seconds for USB (hit control-C to exit)',
                        file=sys.stderr,
                        flush=True,
                    )
                    # Short sleeps remain interruptible even with a very large
                    # accumulated backoff, without overflowing platform time_t.
                    for _ in range(delay):
                        time.sleep(1)
                    delay *= 2

    def write_frame(self, data: bytes) -> None:
        deadline = time.monotonic() + self.timeout
        pending = memoryview(data)
        while pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                raise TimeoutError('USB serial write timed out')
            try:
                count = os.write(self.fd, pending)
            except BlockingIOError:
                continue
            if count == 0:
                raise OSError('USB serial disconnected')
            pending = pending[count:]

    def read_frame(self) -> str:
        deadline = time.monotonic() + self.timeout
        result = bytearray()
        while len(result) < 1300:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.fd], [], [], remaining)[0]:
                raise TimeoutError('USB serial response timed out')
            try:
                value = os.read(self.fd, 1)
            except BlockingIOError:
                continue
            if not value:
                raise OSError('USB serial disconnected')
            if value == b'\\n':
                try:
                    return result.decode('ascii').rstrip('\\r')
                except UnicodeDecodeError as error:
                    raise OSError('Invalid USB response encoding') from error
            result.extend(value)
        raise OSError('USB transport response too long')

    def write(self, data: bytes) -> None:
        for i in range(0, len(data), 128):
            self.exchange(data[i:i + 128])

    def line(self, limit: int = 256) -> str:
        while True:
            if b'\\n' in self.received:
                line, _, remaining = self.received.partition(b'\\n')
                self.received = remaining
                if len(line) >= limit:
                    raise ValueError(f'USB serial response exceeds {limit - 1} bytes')
                response = line.decode('ascii').rstrip('\\r')
                if response != 'CNTRL WAIT':
                    return response
            else:
                self.exchange(b'')
                if not self.received:
                    time.sleep(0.01)
''' + s[b:]
p.write_text(s)
# Add explicit cancellation and keepalive for transfers retained by a session.
p=Path('firmware/include/cntrl/documents.h'); s=p.read_text(); at='    void receive(uint8_t byte, uint32_t now) {'
s=s.replace(at,'''    bool accepts_input() const { return browse_phase_ == BrowsePhase::idle && history_action_ == HistoryAction::none && !writing_ && !erasing_; }
    void keepalive(uint32_t now) { last_byte_ = now; }
    void cancel_transfer() { receiving_ = listing_ = overflow_ = false; download_ = used_ = 0; }
'''+at); p.write_text(s)
p=Path('firmware/include/cntrl/songs_transfer.h');s=p.read_text().replace('    bool busy() const', '''    bool processing() const { return processing_; }
    void keepalive(uint32_t now) { if (receiving_) last_ = now; }
    void cancel_transfer() { if (receiving_) songs_.import_abort(); receiving_ = false; }
    bool busy() const''');p.write_text(s)
p=Path('cntrl/maker/morningstar/mc/firmware/patch_switcher/font_storage.h');s=p.read_text().replace('    bool busy() const', '''    void keepalive(uint32_t now) { last_ = now; }
    void cancel_transfer() { writer_.close(); }
    bool busy() const''');p.write_text(s)
p=Path('cntrl/maker/morningstar/mc/splash/receiver.h');s=p.read_text().replace('    bool busy() const', '''    void keepalive(uint32_t now) { last_ = now; }
    void cancel_transfer() {
        if (state_ == State::receiving) { storage_.finish(); state_ = State::command; }
        used_ = line_used_ = 0;
    }
    bool busy() const''');p.write_text(s)
p=Path('cntrl/maker/morningstar/mc/splash/router.h');s=p.read_text().replace('    bool accepts_input() const { return assets_.accepts_input(); }', '''    bool accepts_input() const { return assets_.accepts_input() && documents_.accepts_input(); }
    void keepalive(uint32_t now) { last_ = now; documents_.keepalive(now); assets_.keepalive(now); }
    void cancel_transfer() { used_ = 0; overflow_ = false; documents_.cancel_transfer(); assets_.cancel_transfer(); }''');p.write_text(s)
p=Path('firmware/include/cntrl/usb_session.h');s=p.read_text().replace('reply_[0] = 0; reset_ = true;', 'reply_[0] = 0; size_ = offset_ = 0; reset_ = true;');p.write_text(s)
