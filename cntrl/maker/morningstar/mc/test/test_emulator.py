"""Catalogue profiles bind to executable targets without changing hardware facts."""

from pathlib import Path

import pytest

from cntrl.emulator import backend, cli
from cntrl.emulator.backend import bind_target
from cntrl.emulator.layout import EmulatorLayout
from cntrl.maker.morningstar.mc.emulator.profiles import adapt, export
from cntrl.patch_switcher import compiler, data_files, models, setup_bundle


@pytest.fixture
def base() -> models.Target:
    target = Path(__file__).parents[1] / 'patch-switcher-target.json'
    errors: list[str] = []
    data = data_files.load_target(
        data_files.DataFile(name='target', paths=[target]), True, errors
    )
    assert not errors
    return models.Target.model_validate(data)


@pytest.mark.parametrize('controller,count', [('mc6_pro', 6), ('mc8_pro', 8)])
def test_profiles_place_every_declared_component(
    base: models.Target, controller: str, count: int
) -> None:
    layout = EmulatorLayout.load(
        Path(__file__).parents[1]
        / 'emulator'
        / (controller.replace('_', '-') + '.toml')
    )
    target = adapt(base, controller)
    hardware = bind_target(layout, target)
    assert len(hardware.controls) == len(target.buttons) == count
    assert len(hardware.displays) == len(target.displays) == count // 2
    assert target.max_switches == base.max_switches == 5
    assert target.controls.app_actions['a'] == base.controls.app_actions['A']
    assert not target.functions
    assert target.animation is not None and base.animation is not None
    assert len(target.animation.streams) == count // 2
    assert target.animation.width == base.animation.width
    assert target.animation.height == base.animation.height
    assert all(not s.mirror_x and not s.mirror_y for s in target.animation.streams)


def test_extra_switches_have_independent_page_directions(base: models.Target) -> None:
    target = adapt(base, 'mc8_pro')
    assert target.controls.previous_page == 'g'
    assert target.controls.next_page == 'h'
    assert target.controls.reverse_page is None
    assert target.controls.back == 'f'
    assert target.controls.app_actions['f'] == base.controls.app_actions['F']


def test_virtual_geometry_does_not_rewrite_catalogue_facts(base: models.Target) -> None:
    root = Path(__file__).parents[1] / 'emulator'
    current = EmulatorLayout.load(root / 'mc6-pro.toml')
    larger = EmulatorLayout.load(root / 'mc8-pro.toml')
    assert not current.provisional
    assert larger.provisional
    assert current.hardware().displays[0].width == 200
    assert larger.hardware().displays[0].width is None
    assert adapt(base, 'mc6_pro').displays[0].width == base.displays[0].width
    assert all(
        d.width == 160 and d.height == 128 for d in adapt(base, 'mc8_pro').displays
    )


def test_exported_profile_keeps_font_paths_relocatable(tmp_path: Path) -> None:
    source = Path(__file__).parents[1] / 'patch-switcher-target.json'
    path = export(source, 'mc8_pro', tmp_path / 'profile')
    path.parent.rename(tmp_path / 'moved-profile')
    path = tmp_path / 'moved-profile' / path.name
    errors: list[str] = []
    data = data_files.load_target(
        data_files.DataFile(name='target', paths=[path]), True, errors
    )
    assert not errors
    target = models.Target.model_validate(data)
    assert target.controls.previous_page == 'g'
    assert set(target.font_sources) == set(target.ui_fonts)
    assert all(
        Path(p).parent == path.parent / 'fonts' for p in target.font_sources.values()
    )
    assert all(Path(p).is_file() for p in target.font_sources.values())


def test_unknown_profile_is_rejected(base: models.Target) -> None:
    with pytest.raises(ValueError, match='no Morningstar emulator profile'):
        adapt(base, 'other')


@pytest.mark.parametrize('controller', ['mc6_pro', 'mc8_pro'])
def test_exported_profiles_build_without_the_physical_sdk(
    tmp_path: Path, controller: str
) -> None:
    root = Path(__file__).parents[5]
    source = Path(__file__).parents[1] / 'patch-switcher-target.json'
    path = export(source, controller, tmp_path / 'profile')
    target = cli.load_target(path)
    executable = backend.build(target, tmp_path / 'native', root)
    show = {
        'pages': {'performance': {'type': 'patch-switcher'}},
        'patches': {
            'control': {
                'type': 'control-change',
                'output': 'din',
                'channel': 1,
                'controller': 7,
                'value': 100,
            },
        },
        'patch_cycles': {'main': ['control']},
    }
    raw = compiler.compile_show(show, {}, [], target)
    setup = tmp_path / 'setup.bin'
    setup.write_bytes(
        setup_bundle.build({'emulator': raw}, {'emulator': show}, target.memory).pack()
    )
    layout = EmulatorLayout.load(
        Path(__file__).parents[1]
        / 'emulator'
        / (controller.replace('_', '-') + '.toml')
    )
    with backend.Session(
        layout, target, executable, tmp_path / 'session', setup
    ) as session:
        session.advance(20000)
        session.button('a', True)
        session.advance(40000)
        session.button('a', False)
        session.advance(60000)
        cli.save(session, tmp_path / 'actual-render')
        assert len(session.frames) == len(target.displays)
        assert any(
            e.kind == 'midi' and e.endpoint == 'din' and e.data == b'\xb0\x07\x64'
            for e in session.messages
        ), session.messages
