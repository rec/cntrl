"""Find cntrl USB serial endpoints from OS metadata without opening them."""

import plistlib
import subprocess
import sys
from pathlib import Path

from pydantic import BaseModel, Field, TypeAdapter


class Device(BaseModel, frozen=True):
    port: Path
    name: str
    serial: str = ''

    @property
    def label(self) -> str:
        return f'{self.name} ({self.serial})' if self.serial else self.name


class RegistryNode(BaseModel, frozen=True):
    product: str = Field(default='', alias='USB Product Name')
    manufacturer: str = Field(default='', alias='USB Vendor Name')
    serial: str = Field(default='', alias='USB Serial Number')
    port: Path | None = Field(default=None, alias='IOCalloutDevice')
    children: list['RegistryNode'] = Field(
        default_factory=list, alias='IORegistryEntryChildren'
    )


def registry_devices(data: bytes) -> list[Device]:
    roots = TypeAdapter(list[RegistryNode]).validate_python(plistlib.loads(data))
    result: dict[Path, Device] = {}

    def visit(
        node: RegistryNode, name: str = '', manufacturer: str = '', serial: str = ''
    ) -> None:
        if node.product:
            name, manufacturer, serial = node.product, node.manufacturer, node.serial
        if node.port and is_cntrl(name, manufacturer):
            result[node.port] = Device(
                port=node.port, name=name or 'cntrl', serial=serial
            )
        for child in node.children:
            visit(child, name, manufacturer, serial)

    for node in roots:
        visit(node)
    return sorted(result.values(), key=lambda d: (d.name, d.serial, d.port))


def linux_devices(root: Path = Path('/sys/class/tty')) -> list[Device]:
    result: list[Device] = []
    for entry in sorted(root.iterdir()):
        device = (entry / 'device').resolve()
        for directory in (device, *device.parents):
            if not (directory / 'idVendor').is_file():
                continue
            name = usb_property(directory / 'product')
            manufacturer = usb_property(directory / 'manufacturer')
            if is_cntrl(name, manufacturer):
                result.append(
                    Device(
                        port=Path('/dev') / entry.name,
                        name=name or 'cntrl',
                        serial=usb_property(directory / 'serial'),
                    )
                )
            break
    return result


def connected() -> list[Device]:
    if sys.platform == 'darwin':
        result = subprocess.run(
            ['ioreg', '-a', '-l', '-r', '-c', 'IOUSBHostDevice'],
            check=True,
            capture_output=True,
        )
        return registry_devices(result.stdout)
    if sys.platform.startswith('linux'):
        return linux_devices()
    raise ValueError('Automatic cntrl discovery requires macOS or Linux')


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


def usb_property(path: Path) -> str:
    try:
        return path.read_text().strip()
    except FileNotFoundError:
        return ''
