"""App and page authoring, including the empty screen-saver setup."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, layout, models, resolved


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
        {'pages': [{'type': 'status', 'windows': -1}]},
        {'pages': [{'type': 'unknown'}]},
    ],
)
def test_invalid_app_and_page_layouts_are_rejected(source: dict[str, object]) -> None:
    with pytest.raises(ValueError):
        models.Setup.model_validate(source)


@pytest.mark.parametrize('count', [1, 2, 5])
def test_layout_follows_declared_displays(count: int) -> None:
    descriptor = target().model_dump()
    descriptor['displays'] = [
        {
            'id': f'window-{i}',
            'label': f'Window {i}',
            'width': 160 + i * 8,
            'height': 128 + i * 8,
        }
        for i in range(count)
    ]
    device = models.Target.model_validate(descriptor)
    show = models.Setup(pages=[models.Page(type='status', windows=count)])
    data = layout.pack(show, device)
    assert data[6:8] == bytes((count, 2))
    assert layout.section_end(data, 0) == len(data)
    exported = layout.unpack(data, 0)
    app = exported['apps']['patch-switcher']
    assert app['give_up_windows'] == [f'window-{i}' for i in reversed(range(count))]
    assert exported['pages'] == [{'type': 'status', 'windows': count}]
    assert device.display_width == 160
    assert device.display_height == 128
    with pytest.raises(ValueError, match='more windows'):
        layout.pack(
            show.model_copy(
                update={'pages': [models.Page(type='status', windows=count + 1)]}
            ),
            device,
        )


def test_window_priority_requires_every_declared_id() -> None:
    show = models.Setup(apps={'example': models.App(give_up_windows=['right'])})
    with pytest.raises(ValueError, match='every target display ID'):
        layout.pack(show, target())


def test_legacy_layout_conversion_preserves_priority_and_following_bytes() -> None:
    old = b'CLAY\x01\x01' + b'example'.ljust(32, b'\0') + bytes((2, 0, 1, 1, 2))
    section, old_end = layout.upgrade_legacy_section(
        old + b'unchanged', 0, ['one', 'two', 'three']
    )
    assert old_end == len(old)
    assert (old + b'unchanged')[old_end:] == b'unchanged'
    assert layout.unpack(section, 0) == {
        'apps': {
            'example': {
                'type': 'patch-switcher',
                'give_up_windows': ['three', 'one', 'two'],
            }
        },
        'pages': [{'type': 'status', 'windows': 2}],
    }


def target() -> models.Target:
    return models.Target.model_validate(compiler.read_document(ROOT / 'target.json'))


ROOT = Path(__file__).parents[1] / 'firmware/test'
