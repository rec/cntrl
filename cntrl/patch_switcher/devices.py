"""Find cntrl USB serial endpoints from OS metadata without opening them."""

import sys
from pathlib import Path

from pydantic import BaseModel
from serial.tools.list_ports import comports


class Device(BaseModel, frozen=True):
    port: Path
    name: str
    serial: str = ''

    @property
    def label(self) -> str:
        return f'{self.name} ({self.serial})' if self.serial else self.name


def connected() -> list[Device]:
    result: dict[Path, Device] = {}
    for port in comports():
        name = port.product or port.description or ''
        if is_cntrl(name, port.manufacturer or ''):
            endpoint = Path(port.device)
            result[endpoint] = Device(
                port=endpoint, name=name or 'cntrl', serial=port.serial_number or ''
            )
    return sorted(result.values(), key=lambda d: (d.name, d.serial, str(d.port)))


def select_port(port: Path | None = None) -> Path:
    if port is not None:
        return port
    devices = connected()
    if not devices:
        raise ValueError(
            'No cntrl device found. Connect it by USB and make sure '
            'cntrl firmware is running.'
        )
    if len(devices) == 1:
        return devices[0].port
    print('Connected cntrl devices:', file=sys.stderr)
    for i, device in enumerate(devices, 1):
        print(f'  {i}. {device.label}', file=sys.stderr)
    if not sys.stdin.isatty():
        raise ValueError(
            'Multiple cntrl devices found. Run interactively to choose a device, '
            'or connect only one.'
        )
    print(f'Choose a device (1-{len(devices)}): ', end='', file=sys.stderr, flush=True)
    try:
        answer = input().strip()
    except EOFError:
        raise ValueError('Device selection cancelled') from None
    if not answer.isdecimal() or not 1 <= int(answer) <= len(devices):
        raise ValueError(f'Choose a device number from 1 to {len(devices)}')
    return devices[int(answer) - 1].port


def is_cntrl(product: str, manufacturer: str) -> bool:
    return (
        manufacturer.casefold() == 'cntrl'
        or product.casefold().split(' ', 1)[0] == 'cntrl'
    )
