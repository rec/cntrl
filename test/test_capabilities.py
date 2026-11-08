"""The exported target model and generated native capability API agree."""

import subprocess
from pathlib import Path

import pytest

from cntrl.firmware.capabilities import header
from cntrl.patch_switcher import compiler, models


@pytest.mark.parametrize(
    'count,buttons', [(1, []), (2, ['First']), (5, ['Less', 'More'])]
)
def test_capabilities_compile_without_an_implementation(
    tmp_path: Path, count: int, buttons: list[str]
) -> None:
    root = Path(__file__).resolve().parents[1]
    data = compiler.read_document(root / 'firmware/test/target.json')
    assert isinstance(data, dict)
    data['memory'] = {'capacity': 803840, 'runtime': 120000}
    data['buttons'] = buttons
    data['usb_block_size'] = 256
    data['displays'] = [
        {'id': f'view-{i}', 'label': f'View {i}', 'width': 177 + i, 'height': 131}
        for i in range(count)
    ]
    # Exercise an implementation with no bidirectional physical cable pair.
    for endpoint in data['midi_endpoints']:
        endpoint['transport'] = 'usb-device'
        endpoint['input'] = False
    target = models.Target.model_validate(data)
    (tmp_path / 'target_capabilities.h').write_text(header(target))
    executable = tmp_path / 'capabilities'
    subprocess.run(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-pedantic',
            f'-I{tmp_path}',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_capabilities.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    subprocess.run(
        [str(executable), '803840', '177', str(count), str(len(buttons))], check=True
    )
