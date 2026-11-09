"""Function patches round-trip and activate through the portable controller."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved, songs
from scripts import native_tests


def test_function_patch_round_trip_and_controller_activation(tmp_path: Path) -> None:
    root = Path(__file__).resolve().parents[1]
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    ).model_copy(update={'functions': ['stock-firmware'], 'buttons': list('ABCDEF')})
    show = {
        'instruments': {},
        'patch_cycles': {
            'Functions': [
                {
                    'exit_buttons': 'F+A',
                    'display_name': 'Original firmware',
                    'style': {'foreground_color': 'red'},
                    'operation': 'stock-firmware',
                }
            ]
        },
    }
    data = compiler.compile_show(show, {'profiles': []}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['patch_cycles']['Functions'][0]['exit_buttons'] == 'A+F'
    assert compiler.compile_show(exported, {'profiles': []}, [], target) == data
    with pytest.raises(compiler.CompilationError, match='unavailable'):
        compiler.compile_show(
            show, {'profiles': []}, [], target.model_copy(update={'functions': []})
        )
    image = tmp_path / 'function.bin'
    image.write_bytes(data)
    peer = tmp_path / 'function-test'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_patch_functions.cpp'),
            '-o',
            str(peer),
        ],
        check=True,
    )
    native_tests.run_native([str(peer), str(image)], check=True)


@pytest.mark.parametrize('pair', ['A+A', 'AF', 'A+B+C', '+Back', 'Less+'])
def test_invalid_exit_combinations_are_rejected(pair: str) -> None:
    with pytest.raises(ValueError, match='exit_buttons'):
        models.StockFirmware(operation='stock-firmware', exit_buttons=pair)


def test_stock_firmware_requires_operation_instead_of_type() -> None:
    with pytest.raises(ValueError):
        compiler.normalize_patch({'type': 'stock-firmware', 'exit_buttons': 'A+F'})


def test_function_patches_cannot_contain_midi_properties() -> None:
    with pytest.raises(ValueError):
        compiler.normalize_patch(
            {'exit_buttons': 'A+F', 'program': 1, 'operation': 'stock-firmware'}
        )


def test_song_with_function_and_repeated_patches_preserves_occurrences() -> None:
    root = Path(__file__).resolve().parents[1]
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    ).model_copy(update={'functions': ['stock-firmware'], 'buttons': list('ABCDEF')})
    library = songs.Library(
        songs=[
            songs.Song(
                name='Song',
                patches=[
                    {
                        'type': 'program-change',
                        'output': 'din',
                        'channel': 1,
                        'program': 1,
                    },
                    {'operation': 'stock-firmware', 'exit_buttons': 'E+B'},
                    {
                        'type': 'program-change',
                        'output': 'din',
                        'channel': 1,
                        'program': 1,
                    },
                ],
            )
        ]
    )
    data = songs.encode(library, target)
    restored = songs.decode(data)
    assert len(restored.songs[0].patches) == 3
    assert restored.songs[0].patches[0] == restored.songs[0].patches[2]
    function = restored.songs[0].patches[1]
    assert (
        function['operation'] == 'stock-firmware' and function['exit_buttons'] == 'B+E'
    )
    assert songs.encode(restored, target) == data
