"""Run the adapter's native display and boot-selection checks on each host."""

import sys
from pathlib import Path

import tyro
from pydantic import BaseModel, Field

from scripts.native_tests import compile_native, run_native


class Tests(BaseModel, frozen=True):
    source_root: Path = Field(
        default_factory=lambda: Path(__file__).resolve().parents[4]
    )
    output: Path = Path(__file__).parent / 'firmware/build/native'


def run(options: Tests) -> None:
    root = Path(__file__).resolve().parent / 'firmware'
    failures: list[str] = []
    for name, sources, defines in TESTS:
        binary = options.output / name
        result = compile_native(
            [
                'c++',
                '-std=c++17',
                '-Wall',
                '-Wextra',
                '-Werror',
                '-fsanitize=address,undefined',
                f'-I{options.source_root.resolve() / "firmware/include"}',
                f'-I{root / "test_support"}',
                f'-I{root / "usb_probe"}',
                *defines,
                *(str(root / s) for s in sources),
                '-o',
                str(binary),
            ],
            check=False,
        )
        if result.returncode:
            failures.append(name)
        elif run_native([str(binary)], check=False).returncode:
            failures.append(name)
    if failures:
        sys.exit('Adapter native tests failed: ' + ', '.join(failures))


TESTS = [
    ('midi-trace', ['test_midi_trace.cpp'], []),
    (
        'usb-midi-flush',
        ['test_usb_midi_flush.cpp', 'patch_switcher/usb_forward_device.cpp'],
        [],
    ),
    ('bounded-heap', ['test_bounded_heap.cpp', 'patch_switcher/bounded_heap.cpp'], []),
    ('switch-gesture', ['test_switch_gesture.cpp'], []),
    ('boot-selection', ['test_boot_selection.cpp'], []),
    ('pair-timing', ['test_pair_timing.cpp'], []),
    ('renderer', ['test_renderer.cpp'], []),
    ('displays', ['test_displays.cpp', 'usb_probe/displays.cpp'], []),
    (
        'panorama',
        ['test_displays.cpp', 'patch_switcher/displays.cpp'],
        ['-DCNTRL_PANORAMA'],
    ),
    (
        'scroll-displays',
        ['test_displays.cpp', 'scroll_probe/displays.cpp'],
        ['-DCNTRL_SCROLL_PROBE'],
    ),
]


if __name__ == '__main__':
    run(tyro.cli(Tests))
