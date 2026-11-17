"""A mirrored session boots its saved selection and language without new defaults."""

from pathlib import Path

import pytest

from cntrl.emulator import backend, cli, mirror
from cntrl.emulator.layout import EmulatorLayout
from cntrl.maker.morningstar.mc.emulator import profiles
from cntrl.patch_switcher import (
    binary_format,
    compiler,
    languages,
    setup_bundle,
    storage_info,
    transport,
)


class StoredPedal(transport.SetupClient):
    def __init__(self, directory: Path, bundle: bytes, status: str) -> None:
        self.directory = directory
        self.bundle = bundle
        self.status = status

    def current(self) -> str:
        return 'second'

    def download_bundle(self) -> bytes:
        return self.bundle

    def download_state(self, path: str) -> bytes:
        return (self.directory / path.lstrip('/')).read_bytes()

    def request(self, command: str) -> str:
        assert command == 'CNTRL STATUS'
        return self.status


def test_mirrored_native_session_keeps_selection_language_and_installed_packs(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    target = cli.load_target(
        profiles.export(None, 'mc6_pro', tmp_path / 'profile', mirror=True)
    )
    # The pedal compiles physical switch and display IDs. Compiling against the
    # virtual target here would hide mismatches in the mirror adapter.
    physical_target = cli.load_target(BASE / 'patch-switcher-target.json')
    sources: dict[str, object] = {
        'first': {
            'patches': {'menu': {'operation': 'menu'}},
            'patch_cycles': {'main': ['menu']},
            'pages': {'live': {}},
            'use_animations': False,
            'fill_in_styles': False,
        },
        'second': {
            'pages': {'live': {}, 'language': {'type': 'language'}},
            'regions': {
                'performance': {'pages': ['live']},
                'tools': {'pages': ['language']},
            },
            'layout': {'focus': 'tools'},
            'use_animations': False,
            'fill_in_styles': False,
        },
    }
    documents = {
        n: compiler.compile_show(s, {}, [], physical_target) for n, s in sources.items()
    }
    bundle = tmp_path / 'setup.bin'
    bundle.write_bytes(
        setup_bundle.build(
            setup_bundle.reindex(documents), sources, physical_target.memory
        ).pack()
    )
    executable = backend.build(target, tmp_path / 'native', BASE.parents[3])
    layout = EmulatorLayout.load(BASE / 'emulator/mc6-pro.toml')
    original = tmp_path / 'pedal'
    packs = original / 'cntrl/languages'
    packs.mkdir(parents=True)
    for tag in ('en', 'fr'):
        (packs / f'{tag}.bin').write_bytes(languages.pack(tag))
    with backend.Session(layout, target, executable, original, bundle) as session:
        session.advance(1000000)

        def press(button: str) -> None:
            session.button(button, True)
            session.advance(session.time_us + 80000)
            session.button(button, False)
            session.advance(session.time_us + 80000)

        # Open Setups from the performance patch, select the second setup,
        # then select French on its Language page using the real switches.
        for button in ('a', 'c', 'b', 'c'):
            press(button)
        session.advance(session.time_us + 1000000)
        english = session.frames['lcd-3']
        press('e')
        press('c')
        session.advance(session.time_us + 1000000)
        french = session.frames['lcd-3']
        assert french != english
        assert not [m for m in session.messages if m.kind == 'error']
    status = (
        'CNTRL READY model=mc6-pro persistent_root=0 '
        f'persistent_format={binary_format.DEVELOPMENT_FORMAT}'
    )
    pedal = StoredPedal(original, bundle.read_bytes(), status)
    report = storage_info.StorageInfo(
        capacity_bytes=10000000,
        allocation_unit_bytes=512,
        usable_bytes=10000000,
        free_bytes=None,
        used_bytes=None,
        entries=[
            storage_info.CardEntry(
                path='/' + p.relative_to(original).as_posix(),
                directory=False,
                size_bytes=p.stat().st_size,
                estimated_allocation_bytes=512,
            )
            for p in (original / 'cntrl').rglob('*')
            if p.is_file()
        ],
    )
    monkeypatch.setattr(storage_info, 'read_storage_report', lambda read: report)
    captured = tmp_path / 'mirror'
    captured.mkdir()

    def download(
        port: Path, storage: Path | None, controller: str | None
    ) -> tuple[str, Path, Path]:
        return 'mc6-pro', mirror.capture(pedal, captured, status), captured

    def compile_forbidden(sources: object, path: Path) -> setup_bundle.SetupBundle:
        pytest.fail('mirrored setups must never be recompiled')

    monkeypatch.setattr(cli, 'download', download)
    mirrored_layout, mirrored_target, setup, storage = cli.prepare(
        cli.Emulate(port=Path('/dev/test-pedal')), compile_forbidden
    )
    assert sorted(p.stem for p in (storage / 'cntrl/languages').glob('*.bin')) == [
        'en',
        'fr',
    ]
    with backend.Session(
        mirrored_layout, mirrored_target, executable, storage, setup
    ) as session:
        session.advance(1000000)
        assert session.frames['lcd-3'] == french
        assert not [m for m in session.messages if m.kind == 'error']


BASE = Path(__file__).parents[1]
