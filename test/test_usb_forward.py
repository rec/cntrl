"""Hosted-device forwarding survives compilation and editable download."""

import subprocess
from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved


def test_hotplug_descriptors_and_bidirectional_midi(tmp_path: Path) -> None:
    root = Path(__file__).resolve().parents[1]
    executable = tmp_path / 'usb-forward'
    subprocess.run(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-pedantic',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_usb_forward.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)


@pytest.mark.parametrize('enabled', [False, True])
@pytest.mark.parametrize('fast', [False, True])
def test_forwarding_and_fast_switches_are_independent(
    enabled: bool, fast: bool
) -> None:
    root = Path(__file__).resolve().parents[1]
    show = compiler.read_document(root / 'firmware/test/show.json')
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    )
    assert isinstance(show, dict)
    assert models.Setup.model_validate(show).forward_device_to_host is True
    show.update(forward_device_to_host=enabled, fast_switches=fast)
    definitions = {
        'profiles': [
            {'instrument': 'synth', 'name': 'stock', 'banks': {'0': {'msb': 0}}}
        ]
    }
    data = compiler.compile_show(show, definitions, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['forward_device_to_host'] is enabled
    assert exported['fast_switches'] is fast
    assert compiler.compile_show(exported, {'profiles': []}, [], target) == data
