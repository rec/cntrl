"""Native device preferences preserve raw setups and reject unsafe changes."""

import subprocess
from pathlib import Path

from cntrl.patch_chooser import compiler, models, setup_bundle
from scripts import native_tests


def test_device_settings_survive_reboot_and_preserve_setup_overrides(
    tmp_path: Path,
) -> None:
    first = tmp_path / 'a.toml'
    first.write_text("""[patches.global_brightness]
operation = 'global-setting'
setting = 'brightness'
value = 170
[patches.global_buffers]
operation = 'global-setting'
setting = 'frame_buffers'
value = 1
[page_settings]
clock_bpms = [121]
""")
    second = tmp_path / 'b.toml'
    second.write_text("""brightness = 90
[instruments.synth]
output = 'usb-return'
channel = 1
[patches.sound]
instrument = 'synth'
program = 10
[patch_cycles]
Live = ['sound']
""")
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    target = target.model_copy(
        update={
            'midi_endpoints': [
                e.model_copy(update={'output': True}) if e.name == 'usb-return' else e
                for e in target.midi_endpoints
            ]
        }
    )
    sources = {'a': compiler.read_document(first), 'b': compiler.read_document(second)}
    compiled = {
        n: compiler.compile_show(d, {}, [], target, setting_overrides=d)
        for n, d in sources.items()
    }
    bundle = tmp_path / 'setups.bin'
    bundle.write_bytes(setup_bundle.build(compiled, sources, target.memory).pack())
    program = tmp_path / 'globals'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{ROOT / "firmware/include"}',
            str(ROOT / 'firmware/test/test_global_settings.cpp'),
            '-o',
            str(program),
        ],
    )
    subprocess.run([str(program), str(bundle), str(tmp_path / 'device')], check=True)


ROOT = Path(__file__).resolve().parents[1]
