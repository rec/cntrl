"""App and page authoring, including the empty screen-saver setup."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved


@pytest.mark.parametrize('apps', [False, True])
@pytest.mark.parametrize('pages', [False, True])
def test_apps_and_pages_are_independently_optional(apps: bool, pages: bool) -> None:
    source = {
        'apps': {'patch-switcher': {}} if apps else {},
        'pages': [{'type': 'status'}] if pages else [],
        'fill_in_styles': False,
    }
    data = compiler.compile_show(source, {}, [], target())
    exported = models.Setup.model_validate(
        tomlkit.parse(resolved.export(data)).unwrap()
    )
    original = models.Setup.model_validate(source)
    assert exported.apps == original.apps
    assert exported.pages == original.pages
    assert (
        compiler.compile_show(exported.model_dump(exclude_none=True), {}, [], target())
        == data
    )


def test_app_instances_and_page_order_survive_compilation() -> None:
    source = compiler.read_document(ROOT / 'apps.toml')
    data = compiler.compile_show(source, {}, [], target())
    assert data.hex() == (ROOT / 'apps.golden').read_text().strip()
    exported = models.Setup.model_validate(
        tomlkit.parse(resolved.export(data)).unwrap()
    )
    original = models.Setup.model_validate(source)
    assert exported.apps == original.apps
    assert exported.pages == original.pages
    assert exported.pages[0].windows == 0


@pytest.mark.parametrize(
    'source',
    [
        {'apps': {'x': {'give_up_windows': ['right', 'right', 'left']}}},
        {'apps': {'x': {'type': 'unknown'}}},
        {'pages': [{'type': 'None', 'windows': 1}]},
        {'pages': [{'type': 'status', 'windows': 4}]},
        {'pages': [{'type': 'status', 'windows': -1}]},
        {'pages': [{'type': 'unknown'}]},
    ],
)
def test_invalid_app_and_page_layouts_are_rejected(source: dict[str, object]) -> None:
    with pytest.raises(ValueError):
        models.Setup.model_validate(source)


def target() -> models.Target:
    return models.Target.model_validate(compiler.read_document(ROOT / 'target.json'))


ROOT = Path(__file__).parents[1] / 'firmware/test'
