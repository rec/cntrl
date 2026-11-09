"""Discover USB metadata and select a device without opening serial ports."""

import sys
from pathlib import Path

import pytest
from serial.tools.list_ports_common import ListPortInfo

from cntrl.patch_switcher import devices


@pytest.mark.parametrize('endpoint', ['COM10', '/dev/ttyACM0', '/dev/cu.usbmodem2'])
def test_discovery_retains_identity_without_resolving_endpoint(
    endpoint: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    pedal = ListPortInfo(endpoint)
    pedal.product = 'cntrl experimental'
    pedal.serial_number = 'pedal-2'
    unrelated = ListPortInfo('unrelated')
    monkeypatch.setattr(devices, 'comports', lambda: [unrelated, pedal])
    found = devices.connected()
    assert found == [
        devices.Device(port=Path(endpoint), name='cntrl experimental', serial='pedal-2')
    ]
    assert found[0].label == 'cntrl experimental (pedal-2)'


def test_discovery_accepts_manufacturer_when_product_is_unavailable(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    pedal = ListPortInfo('COM11')
    pedal.manufacturer = 'cntrl'
    pedal.description = ''
    monkeypatch.setattr(devices, 'comports', lambda: [pedal])
    assert devices.connected() == [devices.Device(port=Path('COM11'), name='cntrl')]


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
