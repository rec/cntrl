import pytest
import tyro

from cntrl.maker.morningstar.mc.qualify_usb_midi import Options


@pytest.mark.parametrize('value', ['0.5', '500 ms'])
def test_stress_duration_accepts_units_without_running_hardware(value: str) -> None:
    options = tyro.cli(Options, args=['--stress-seconds', value])
    assert options.stress_seconds == 0.5
