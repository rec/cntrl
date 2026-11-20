"""Device language preferences survive source recovery without setup changes."""

import pytest

from cntrl.patch_chooser import device_globals, global_overlay


def test_old_overlay_defaults_to_english_and_current_overlay_preserves_language() -> (
    None
):
    old = device_globals.load(b'format=1\n[values]\n"0"=160\n')
    assert old == global_overlay.Overlay(values={0: 160})
    selected = global_overlay.Overlay(values={0: 160}, language='fr')
    assert device_globals.load(device_globals.dump(selected)) == selected
    assert device_globals.load(b'format=2\nlanguage="fr"\n') == global_overlay.Overlay(
        language='fr'
    )
    future = global_overlay.Overlay(language='fr-CA')
    assert device_globals.load(device_globals.dump(future)) == future
    assert (
        device_globals.load(device_globals.dump(global_overlay.Overlay())).language
        is None
    )


@pytest.mark.parametrize(
    'source',
    [
        b'format=1\nlanguage="fr"\n[values]\n',
        b'format=2\nlanguage="FR"\n[values]\n',
        b'format=2\nlanguage="../../fr"\n[values]\n',
    ],
)
def test_invalid_language_overlays_are_rejected(source: bytes) -> None:
    with pytest.raises(ValueError):
        device_globals.load(source)
