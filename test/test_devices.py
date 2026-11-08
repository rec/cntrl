"""Discover USB metadata and select a device without opening serial ports."""

import plistlib
import sys
from pathlib import Path

import pytest

from cntrl.patch_switcher import devices


def test_macos_discovery_keeps_usb_identity_and_ignores_unrelated_ports() -> None:
    data = plistlib.dumps(
        [
            {
                'USB Product Name': 'USB Hub',
                'IORegistryEntryChildren': [
                    {
                        'USB Product Name': 'cntrl experimental',
                        'USB Vendor Name': 'cntrl',
                        'USB Serial Number': 'pedal-2',
                        'IORegistryEntryChildren': [
                            {
                                'IORegistryEntryChildren': [
                                    {'IOCalloutDevice': '/dev/cu.usbmodem2'}
                                ]
                            }
                        ],
                    },
                    {
                        'USB Product Name': 'Other serial device',
                        'IORegistryEntryChildren': [
                            {'IOCalloutDevice': '/dev/cu.other'}
                        ],
                    },
                ],
            }
        ]
    )
    found = devices.registry_devices(data)
    assert len(found) == 1
    assert found[0].port == Path('/dev/cu.usbmodem2')
    assert found[0].label == 'cntrl experimental (pedal-2)'


def test_linux_discovery_follows_tty_ancestry_to_usb_identity(tmp_path: Path) -> None:
    tty = tmp_path / 'tty'
    tty.mkdir()
    for name, vendor in [('ttyACM0', 'cntrl'), ('ttyUSB0', 'Other')]:
        usb = tmp_path / name
        interface = usb / 'interface'
        interface.mkdir(parents=True)
        (usb / 'idVendor').write_text('1234')
        (usb / 'manufacturer').write_text(vendor)
        (usb / 'product').write_text('Foot Controller')
        (usb / 'serial').write_text(name)
        entry = tty / name
        entry.mkdir()
        (entry / 'device').symlink_to(interface, target_is_directory=True)
    found = devices.linux_devices(tty)
    assert len(found) == 1
    assert found[0].port == Path('/dev/ttyACM0')
    assert found[0].label == 'Foot Controller (ttyACM0)'


def test_sole_device_is_selected_without_prompting(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    device = devices.Device(port=Path('/dev/one'), name='cntrl')
    monkeypatch.setattr(devices, 'connected', lambda: [device])
    assert devices.select_port() == device.port


def test_no_device_has_an_actionable_error(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(devices, 'connected', lambda: [])
    with pytest.raises(ValueError, match='No cntrl device found. Connect it by USB'):
        devices.select_port()


def test_multiple_devices_offer_names_and_serial_numbers(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    found = [
        devices.Device(port=Path('/dev/one'), name='cntrl stage', serial='100'),
        devices.Device(port=Path('/dev/two'), name='cntrl spare', serial='200'),
    ]
    monkeypatch.setattr(devices, 'connected', lambda: found)
    monkeypatch.setattr(sys.stdin, 'isatty', lambda: True)
    monkeypatch.setattr('builtins.input', lambda: '2')
    assert devices.select_port() == found[1].port
    output = capsys.readouterr().err
    assert 'cntrl stage (100)' in output and 'cntrl spare (200)' in output
    assert '/dev/' not in output


@pytest.mark.parametrize('answer', ['0', '3', 'invalid'])
def test_invalid_device_choice_has_a_clear_error(
    monkeypatch: pytest.MonkeyPatch, answer: str
) -> None:
    found = [devices.Device(port=Path(f'/dev/{n}'), name='cntrl') for n in range(2)]
    monkeypatch.setattr(devices, 'connected', lambda: found)
    monkeypatch.setattr(sys.stdin, 'isatty', lambda: True)
    monkeypatch.setattr('builtins.input', lambda: answer)
    with pytest.raises(ValueError, match='Choose a device number from 1 to 2'):
        devices.select_port()


def test_noninteractive_multiple_matches_are_not_selected_arbitrarily(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    found = [devices.Device(port=Path(f'/dev/{n}'), name='cntrl') for n in range(2)]
    monkeypatch.setattr(devices, 'connected', lambda: found)
    monkeypatch.setattr(sys.stdin, 'isatty', lambda: False)
    with pytest.raises(ValueError, match='Run interactively to choose a device'):
        devices.select_port()


def test_explicit_port_skips_discovery(monkeypatch: pytest.MonkeyPatch) -> None:
    def unexpected() -> list[devices.Device]:
        pytest.fail('Explicit endpoint must not enumerate devices')

    monkeypatch.setattr(devices, 'connected', unexpected)
    assert devices.select_port(Path('/dev/explicit')) == Path('/dev/explicit')
