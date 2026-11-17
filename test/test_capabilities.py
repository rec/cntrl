"""The exported target model and generated native capability API agree."""

from pathlib import Path

import pytest

from cntrl.firmware.capabilities import header
from cntrl.patch_switcher import compiler, models
from scripts import native_tests


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
    labels = {b: b.upper() for b in buttons} if count == 5 else {}
    data['button_labels'] = labels
    data['controls'] = {'previous_page': buttons[-1] if buttons else None}
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
    native_tests.compile_native(
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
    native_tests.run_native(
        [
            str(executable),
            '803840',
            '177',
            str(count),
            str(len(buttons)),
            str(len(buttons) - 1 if buttons else 4294967295),
            *[labels.get(b, b) for b in buttons],
        ],
        check=True,
    )


def test_button_labels_require_declared_switches() -> None:
    data = compiler.read_document(
        Path(__file__).parents[1] / 'firmware/test/target.json'
    )
    assert isinstance(data, dict)
    data['button_labels'] = {'unknown': 'Label'}
    with pytest.raises(ValueError, match='button labels'):
        models.Target.model_validate(data)


@pytest.mark.parametrize('mapping', [[0], [0, 99]])
def test_animation_mapping_requires_a_valid_stream_for_every_display(
    mapping: list[int],
) -> None:
    data = compiler.read_document(
        Path(__file__).parents[1] / 'firmware/test/target.json'
    )
    assert isinstance(data, dict)
    data['animation_display_streams'] = mapping
    with pytest.raises(ValueError, match='animation'):
        models.Target.model_validate(data)
