"""Function patches round-trip and activate through the portable controller."""

import subprocess
from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved, songs


def test_function_patch_round_trip_and_controller_activation(tmp_path: Path) -> None:
    root = Path(__file__).resolve().parents[1]
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    ).model_copy(update={'functions': ['stock-firmware'], 'buttons': list('ABCDEF')})
    show = {
        'instruments': {},
        'groups': {
            'Functions': [
                {
                    'exit_buttons': 'F+A',
                    'display_name': 'Original firmware',
                    'style': {'foreground_color': 'red'},
                    'type': 'stock-firmware',
                }
            ]
        },
    }
    data = compiler.compile_show(show, {'setups': []}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert exported['groups']['Functions'][0]['exit_buttons'] == 'A+F'
    assert compiler.compile_show(exported, {'setups': []}, [], target) == data
    with pytest.raises(compiler.CompilationError, match='unavailable'):
        compiler.compile_show(
            show, {'setups': []}, [], target.model_copy(update={'functions': []})
        )
    image = tmp_path / 'function.bin'
    image.write_bytes(data)
    peer = tmp_path / 'function-test'
    subprocess.run(
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
    subprocess.run([str(peer), str(image)], check=True)


@pytest.mark.parametrize('pair', ['A+A', 'AF', 'A+G', 'A+B+C', 'a+f'])
def test_invalid_exit_combinations_are_rejected(pair: str) -> None:
    with pytest.raises(ValueError, match='exit_buttons'):
        models.StockFirmware(type='stock-firmware', exit_buttons=pair)


def test_function_patches_cannot_contain_midi_properties() -> None:
    with pytest.raises(ValueError):
        compiler.normalize_patch(
            {'exit_buttons': 'A+F', 'program': 1, 'type': 'stock-firmware'}
        )


def test_song_with_function_and_repeated_patches_preserves_occurrences() -> None:
    root = Path(__file__).resolve().parents[1]
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    ).model_copy(update={'functions': ['stock-firmware'], 'buttons': list('ABCDEF')})
    library = songs.Library(
        songs=[
            songs.Song(
                id=1,
                name='Song',
                patches=[
                    {'type': 'midi', 'output': 'din', 'bytes': [0xC0, 0]},
                    {'type': 'stock-firmware', 'exit_buttons': 'E+B'},
                    {'type': 'midi', 'output': 'din', 'bytes': [0xC0, 0]},
                ],
            )
        ]
    )
    data = songs.encode(library, target)
    restored = songs.decode(data)
    assert len({p['id'] for p in restored.songs[0].patches}) == 3
    function = restored.songs[0].patches[1]
    assert function['type'] == 'stock-firmware' and function['exit_buttons'] == 'B+E'
    assert songs.encode(restored, target) == data
