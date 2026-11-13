"""Exercise the Python sender against the real C++ receiver through a PTY."""

import struct
import sys
from collections.abc import Iterator
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import TYPE_CHECKING
from zlib import crc32

import pytest
import tomlkit
from native_peer import NativePeer

from cntrl.patch_switcher import (
    cli,
    compiler,
    data_files,
    midi_assets,
    models,
    resolved,
    setup_sync,
    transport,
)
from scripts import native_tests

if TYPE_CHECKING:
    from pytest_regressions.file_regression import FileRegressionFixture


@pytest.fixture(scope='module', params=[1024, 4096])
def usb_block_size(request: pytest.FixtureRequest) -> int:
    return int(request.param)


@pytest.fixture(scope='module')
def peer(tmp_path_factory: pytest.TempPathFactory, usb_block_size: int) -> Path:
    executable = tmp_path_factory.mktemp('document-peer') / 'peer'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            f'-DCNTRL_USB_BLOCK_SIZE={usb_block_size}',
            '-Wall',
            '-Wextra',
            '-Werror',
            f'-I{ROOT / "firmware/include"}',
            str(ROOT / 'firmware/test/document_server.cpp'),
            '-o',
            str(executable),
        ],
        check=True,
    )
    return executable


@pytest.fixture(scope='module')
def bundle_peer() -> Iterator[Path]:
    with TemporaryDirectory(prefix='cntrl-bundle-peer-') as directory:
        executable = Path(directory) / 'peer'
        native_tests.compile_native(
            [
                'c++',
                '-std=c++17',
                '-Wall',
                '-Wextra',
                '-Werror',
                f'-I{ROOT / "firmware/include"}',
                str(ROOT / 'firmware/test/bundle_server.cpp'),
                '-o',
                str(executable),
            ],
            check=True,
        )
        yield executable


def test_named_setup_lifecycle_against_cpp_receiver(
    peer: Path, usb_block_size: int
) -> None:
    data = bytes.fromhex((ROOT / 'firmware/test/show.golden').read_text())
    with NativePeer([str(peer)]) as port:
        assert (
            transport.device_status(port)
            == f'CNTRL READY {compiler.DOCUMENT_VERSION} 0 00000000'
        )
        with transport.SerialConnection(port) as connection:
            assert connection.usb_block_size == usb_block_size
            client = transport.SetupClient(connection)
            assert client.catalogue() == []
            assert client.current() == ''
            with pytest.raises(ValueError, match='rotation-failed'):
                client.ok('CNTRL SETUP FORWARD')
            client.ok('CNTRL TIME 1792843200')
            client.copy('Jazz + A=B', data, False)
            client.copy('Evening', data, False)
            assert client.current() == ''
            client.ok('CNTRL SETUP FINISH')
            assert client.current() == 'Evening'
            assert [e.name for e in client.catalogue()] == ['Evening', 'Jazz + A=B']
            assert all(e.modified == 1792843200 for e in client.catalogue())
            with pytest.raises(ValueError, match='setup-exists'):
                client.copy('Evening', data, False)
            with pytest.raises(ValueError, match='setup-exists'):
                client.copy('evening', data, True)
            client.copy('Evening', data, True)
            client.ok('CNTRL SETUP FORWARD')
            assert client.current() == 'Jazz + A=B'
            client.ok('CNTRL SETUP FORWARD')
            assert client.current() == 'Evening'
            client.ok('CNTRL SETUP BACKWARD')
            assert client.current() == 'Jazz + A=B'
            assert client.download('Jazz + A=B') == data
            client.delete(['Jazz + A=B', 'Evening'])
            assert client.current() == '' and client.catalogue() == []
            client.copy('Again', data, False)
            client.ok('CNTRL SETUP FINISH')
            assert client.current() == 'Again'
        assert (
            transport.device_status(port)
            == f'CNTRL READY {compiler.DOCUMENT_VERSION} {len(data)} {crc32(data):08x}'
        )


def test_cpp_receiver_rejects_corrupt_document(peer: Path) -> None:
    data = bytearray.fromhex((ROOT / 'firmware/test/show.golden').read_text())
    data[-1] ^= 1
    with NativePeer([str(peer)]) as port:
        with pytest.raises(ValueError, match='invalid-document'):
            with transport.SerialConnection(port) as connection:
                transport.SetupClient(connection).copy('Bad', bytes(data), False)
        assert (
            transport.device_status(port)
            == f'CNTRL READY {compiler.DOCUMENT_VERSION} 0 00000000'
        )


def test_large_setup_uses_the_shared_document_budget(peer: Path) -> None:
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )

    def setup(size: int, value: int = 1) -> bytes:
        return compiler.compile_show(
            {
                'patches': {
                    str(i): {'data': [value] * size, 'output': 'din'} for i in range(2)
                }
            },
            {},
            [],
            target,
        )

    data = setup(40000)
    too_large_alongside_active = setup(60000)
    assert len(data) > 65536
    with NativePeer([str(peer)]) as port:
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            client.copy('Large', data, False)
            client.ok('CNTRL SETUP FINISH')
            assert client.download('Large') == data
            with pytest.raises(ValueError, match='document-memory'):
                client.copy('Larger', too_large_alongside_active, False)
            with pytest.raises(ValueError, match='history-failed'):
                client.copy('Large', setup(40000, 2), True)
            assert client.current() == 'Large'
            assert client.download('Large') == data


ROOT = Path(__file__).resolve().parents[1]


def test_resolved_export_recompiles_without_original_sources() -> None:
    import tomlkit

    data = bytes.fromhex((ROOT / 'firmware/test/show.golden').read_text())
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert compiler.compile_show(exported, {'profiles': []}, [], target) == data


def test_setup_cli_copy_download_and_delete(
    bundle_peer: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    with NativePeer([str(bundle_peer)]) as port:
        with transport.SerialConnection(Path(port)) as connection:
            assert transport.SetupClient(connection).memory() == models.TargetMemory(
                capacity=400000, runtime=240000
            )

        def run(arguments: list[str]) -> None:
            monkeypatch.setattr(
                sys, 'argv', ['cntrl', 'setup', *arguments, '--port', port]
            )
            cli.main()

        fixture = ROOT / 'firmware/test'
        source = tmp_path / 'show.toml'
        source.write_text(tomlkit.dumps(compiler.read_document(fixture / 'show.json')))
        run(
            [
                'copy',
                f'Evening={source}',
                f'Jazz={source}',
                '--target',
                str(fixture / 'target.json'),
                '--definitions',
                str(fixture / 'definitions.json'),
            ]
        )
        assert 'Evening:' in capsys.readouterr().out
        run(['set'])
        assert capsys.readouterr().out.strip() == 'Evening'
        run(['rotate', '-b'])
        run(['set'])
        assert capsys.readouterr().out.strip() == 'Jazz'
        run(['download', 'Evening', '--output', str(tmp_path)])
        downloaded = tmp_path / 'Evening.toml'
        assert downloaded.exists()
        with pytest.raises(SystemExit, match='--force'):
            run(['download', 'Evening', '--output', str(tmp_path)])
        run(['download', 'Evening', '-o', str(tmp_path), '-f'])
        run(
            [
                'copy',
                f'Resolved={downloaded}',
                '--target',
                str(fixture / 'target.json'),
            ]
        )
        run(['set', 'Resolved'])
        run(['del', 'Resolved', '-y'])
        capsys.readouterr()
        run(['list'])
        remaining = capsys.readouterr().out
        assert 'Evening' in remaining and 'Jazz' in remaining


def test_revision_commands_against_cpp_receiver(peer: Path) -> None:
    data = bytes.fromhex((ROOT / 'firmware/test/show.golden').read_text())
    with NativePeer([str(peer)]) as port:
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            client.copy('History', data, False)
            name = b'History'.hex()
            client.ok(f'CNTRL HISTORY SAVE {name} 0 {b"Rehearsal".hex()}')
            response = client.request(f'CNTRL HISTORY LIST {name} {2**64 - 1}')
            assert response.split()[2] == '1'
            assert client.download('History', 1) == data
            client.ok(f'CNTRL HISTORY RESTORE {name} 1')
            client.delete(['History'])
            assert client.download('History', 1) == data
            client.ok(f'CNTRL HISTORY RESTORE {name} 1')
            assert client.download('History') == data
            client.ok(f'CNTRL HISTORY DELETE {name} 0')
            assert (
                client.request(f'CNTRL HISTORY LIST {name} {2**64 - 1}') == 'CNTRL END'
            )


def test_source_sync_round_trip_preserves_independent_pedal_setups(
    bundle_peer: Path, tmp_path: Path
) -> None:
    source = tmp_path / 'show.toml'
    source.write_text(
        '# My setup\nbrightness=100 # dim\nscreensaver_timeout=30\n'
        '[instruments.piano]\noutput="din"\nchannel=1\n'
        '[patches.A]\ninstrument="piano"\nprogram=7\nvolume=80\n'
        '[patches.loud]\nparent="A"\nvolume=100\n'
        '[patch_cycles]\nmain=["A","loud"]\n'
    )
    other = tmp_path / 'other.toml'
    other.write_text('brightness=50\n')
    target = [str(ROOT / 'firmware/test/target.json')]
    inputs = data_files.Sources(document=[f'Show={source}'], target=target)
    with NativePeer([str(bundle_peer)]) as port:
        cli.run_setup(
            cli.Copy(
                document=[f'Show={source}', f'Other={other}'],
                target=target,
                port=Path(port),
            )
        )
        root = setup_sync.store(inputs)
        assert set(setup_sync.baseline(root)) == {'Show', 'Other'}
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            original = client.download('Show')
            edited = bytearray(original)
            # Pedal settings mutate the authoritative binary, leaving embedded
            # authoring metadata unchanged.
            flags = struct.unpack_from('<H', edited, 18)[0]
            struct.pack_into('<H', edited, 18, (flags & ~255) | 75)
            edited[76 + 64] = 4
            graph = resolved.graph_offset(original)
            node = (
                graph
                + struct.unpack_from('<I', edited, graph + compiler.GRAPH.size + 4)[0]
            )
            edited[node + 4 + 5] = 110
            edited[76 + 68 + 68 + 82 + 7] = 110
            struct.pack_into('<I', edited, 20, crc32(edited[28:]))
            client.copy('Show', bytes(edited), True)
            client.ok('CNTRL SETUP FINISH')
            independent = bytearray(client.download('Other'))
            struct.pack_into('<H', independent, 18, 44)
            struct.pack_into('<I', independent, 20, crc32(independent[28:]))
            client.copy('Other', bytes(independent), True)
            client.ok('CNTRL SETUP FINISH')
            client.select('Other')
        with pytest.raises(ValueError, match='unreconciled pedal edits'):
            cli.run_setup(
                cli.Copy(
                    document=inputs.document,
                    target=target,
                    port=Path(port),
                    overwrite=True,
                )
            )
        # A host-only edit must survive importing the pedal changes.
        source.write_text(source.read_text().replace('timeout=30', 'timeout=60'))
        with transport.SerialConnection(Path(port)) as connection:
            review_path = setup_sync.preview(inputs, transport.SetupClient(connection))
        review = setup_sync.Review.model_validate(
            tomlkit.parse(review_path.read_text()).unwrap()
        )
        assert all(c.choice == 'pedal' for c in review.changes)
        setup_sync.apply_review(review_path)
        text = source.read_text()
        assert 'brightness=75 # dim' in text and '# My setup' in text
        authored = tomlkit.parse(text).unwrap()
        assert authored['screensaver_timeout'] == 60
        assert authored['instruments']['piano']['channel'] == 5
        assert authored['patches']['loud'] == {'parent': 'A', 'volume': 110}
        assert setup_sync.baseline(root)['Show'] == bytes(edited)
        # Re-importing after an intentional host revert must keep the revert.
        source.write_text(text.replace('brightness=75', 'brightness=100'))
        with transport.SerialConnection(Path(port)) as connection:
            repeated = setup_sync.preview(inputs, transport.SetupClient(connection))
        assert tomlkit.parse(repeated.read_text())['changes'] == []
        setup_sync.apply_review(repeated)
        cli.run_setup(
            cli.Copy(
                document=inputs.document, target=target, port=Path(port), overwrite=True
            )
        )
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            assert setup_sync.canonical(
                client.download('Other')
            ) == setup_sync.canonical(bytes(independent))
            assert client.current() == 'Other'
            result = setup_sync.canonical(client.download('Show'))
            assert result['brightness'] == 100
            assert result['screensaver_timeout'] == 60


@pytest.mark.parametrize(
    'field', ['program', 'bank', 'volume', 'pan', 'timeout', 'midi', 'trs']
)
def test_source_sync_retains_pedal_editable_fields(
    bundle_peer: Path, tmp_path: Path, field: str
) -> None:
    source = tmp_path / 'show.toml'
    source.write_text(
        '[instruments.piano]\noutput="din"\nchannel=1\n'
        '[patches.A]\ninstrument="piano"\nprogram=7\nvolume=80\npan=0\n'
        '[patches.derived]\nparent="A"\nvolume=100\n'
        '[patch_cycles]\nmain=["derived"]\n'
        '[[definitions.profiles]]\ninstrument="piano"\nname="default"\n'
        '[definitions.profiles.banks.1]\nmsb=2\n'
    )
    roles = tmp_path / 'roles.toml'
    roles.write_text('[trs_ports]\nconfigurable-1=["off","midi-a"]\n')
    inputs = data_files.Sources(
        document=[f'Show={source}'],
        target=[str(ROOT / 'firmware/test/target.json'), '+', str(roles)],
    )
    with NativePeer([str(bundle_peer)]) as port:
        cli.run_setup(cli.Copy(**inputs.model_dump(), port=Path(port)))
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            original = client.download('Show')
            edited = bytearray(original)
            graph = resolved.graph_offset(original)
            node = (
                graph
                + struct.unpack_from('<I', edited, graph + compiler.GRAPH.size + 4)[0]
                + 4
            )
            cache = 76 + 68 + 68
            meta = cache + 82
            if field == 'program':
                edited[node + 2] = edited[cache + 4] = 8
            elif field == 'bank':
                edited[node + 3] = edited[cache + 5] = 2
                struct.pack_into('<I', edited, node + 7, 1)
                struct.pack_into(
                    '<I', edited, meta + compiler.METADATA.size + 64 + 8, 1
                )
            elif field in ('volume', 'pan'):
                position = 5 if field == 'volume' else 6
                edited[node + position] = edited[cache + position + 2] = 255
            elif field == 'timeout':
                struct.pack_into('<I', edited, 24, 45000)
            elif field == 'midi':
                edited[19] ^= 2
            else:
                edited[meta + compiler.METADATA.size - 18] = 1
            struct.pack_into('<I', edited, 20, crc32(edited[28:]))
            client.copy('Show', bytes(edited), True)
            client.ok('CNTRL SETUP FINISH')
            report = setup_sync.preview(inputs, client)
        review = tomlkit.parse(report.read_text())
        assert review['changes']
        for change in review['changes']:
            assert 'destination' in change
            change['choice'] = 'pedal'
        report.write_text(tomlkit.dumps(review))
        setup_sync.apply_review(report)
        assert setup_sync.canonical(
            cli.compile_inputs(inputs).setups['Show']
        ) == setup_sync.canonical(bytes(edited))
        if field in ('program', 'bank', 'volume'):
            # Clearing an inherited value requires replacing the derived parent.
            assert (
                'parent' not in tomlkit.parse(source.read_text())['patches']['derived']
            )


def test_shared_sync_requires_review_and_recovers_interrupted_apply(
    bundle_peer: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    first, second = tmp_path / 'first.toml', tmp_path / 'second.toml'
    first.write_text('brightness=100\n')
    second.write_text('screensaver_timeout=30\n')
    inputs = data_files.Sources(
        document=[f'One={first}', '+', str(second), f'Two={first}', '+', str(second)],
        target=[str(ROOT / 'firmware/test/target.json')],
    )
    with NativePeer([str(bundle_peer)]) as port:
        cli.run_setup(cli.Copy(**inputs.model_dump(), port=Path(port)))
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            original = client.download('One')
            edited = bytearray(original)
            edited[18] = 75
            struct.pack_into('<I', edited, 24, 45000)
            struct.pack_into('<I', edited, 20, crc32(edited[28:]))
            client.copy('One', bytes(edited), True)
            client.ok('CNTRL SETUP FINISH')
            report = setup_sync.preview(inputs, client)
        review = tomlkit.parse(report.read_text())
        assert all(
            c['choice'] == 'unresolved' and c['affects'] == ['One', 'Two']
            for c in review['changes']
        )
        with pytest.raises(ValueError, match='resolve every choice'):
            setup_sync.apply_review(report)
        for change in review['changes']:
            change['choice'] = 'pedal'
        report.write_text(tomlkit.dumps(review))
        captured = report.parent / 'One.bin'
        snapshot = captured.read_bytes()
        captured.write_bytes(b'corrupted snapshot')
        with pytest.raises(ValueError, match='pedal snapshot changed since review'):
            setup_sync.apply_review(report)
        captured.write_bytes(snapshot)
        first.write_text('brightness=101\n')
        with pytest.raises(ValueError, match='source changed since review'):
            setup_sync.apply_review(report)
        first.write_text('brightness=100\n')
        publish = setup_sync.replace

        def fail_second(path: Path, data: bytes) -> None:
            if path == second:
                raise OSError('injected interruption')
            publish(path, data)

        before_base = setup_sync.base_record(setup_sync.store(inputs))
        with monkeypatch.context() as patch:
            patch.setattr(setup_sync, 'replace', fail_second)
            with pytest.raises(OSError, match='injected interruption'):
                setup_sync.apply_review(report)
        assert first.read_text() == 'brightness=75\n'
        assert setup_sync.base_record(setup_sync.store(inputs)) == before_base
        setup_sync.recover(setup_sync.store(inputs) / 'pending.toml')
        assert first.read_text() == 'brightness=100\n'
        setup_sync.apply_review(report)
        compiled = cli.compile_inputs(inputs)
        assert all(
            setup_sync.canonical(v)['brightness'] == 75
            for v in compiled.setups.values()
        )
        assert setup_sync.baseline(setup_sync.store(inputs))['One'] == bytes(edited)
        assert setup_sync.baseline(setup_sync.store(inputs))['Two'] != bytes(edited)


def test_shared_sync_refuses_incompatible_accepted_values(
    bundle_peer: Path, tmp_path: Path
) -> None:
    source = tmp_path / 'shared.toml'
    source.write_text('brightness=100\n')
    inputs = data_files.Sources(
        document=[f'One={source}', f'Two={source}'],
        target=[str(ROOT / 'firmware/test/target.json')],
    )
    with NativePeer([str(bundle_peer)]) as port:
        cli.run_setup(cli.Copy(**inputs.model_dump(), port=Path(port)))
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            for name, brightness in [('One', 75), ('Two', 25)]:
                edited = bytearray(client.download(name))
                edited[18] = brightness
                struct.pack_into('<I', edited, 20, crc32(edited[28:]))
                client.copy(name, bytes(edited), True)
                client.ok('CNTRL SETUP FINISH')
            report = setup_sync.preview(inputs, client)
        review = tomlkit.parse(report.read_text())
        for change in review['changes']:
            change['choice'] = 'pedal'
        report.write_text(tomlkit.dumps(review))
        with pytest.raises(ValueError, match='candidate compilation differs'):
            setup_sync.apply_review(report)
        assert source.read_text() == 'brightness=100\n'
        assert not (setup_sync.store(inputs) / 'pending.toml').exists()


def test_sync_materializes_an_integer_patch_without_rewriting_other_fields(
    bundle_peer: Path, tmp_path: Path
) -> None:
    source = tmp_path / 'show.toml'
    source.write_text(
        '# shorthand\n[instruments.piano]\noutput="din"\nchannel=1\n'
        '[patch_cycles]\nmain=[7]\n'
    )
    inputs = data_files.Sources(
        document=[str(source)], target=[str(ROOT / 'firmware/test/target.json')]
    )
    with NativePeer([str(bundle_peer)]) as port:
        cli.run_setup(cli.Copy(**inputs.model_dump(), port=Path(port)))
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            edited = bytearray(client.download('show'))
            graph = resolved.graph_offset(bytes(edited))
            node = (
                graph + struct.unpack_from('<I', edited, graph + compiler.GRAPH.size)[0]
            )
            edited[node + 4 + 5] = edited[76 + 68 + 68 + 7] = 100
            struct.pack_into('<I', edited, 20, crc32(edited[28:]))
            client.copy('show', bytes(edited), True)
            client.ok('CNTRL SETUP FINISH')
            report = setup_sync.preview(inputs, client)
        review = tomlkit.parse(report.read_text())
        assert len(review['changes']) == 1
        assert 'Materialize' in review['changes'][0]['note']
        review['changes'][0]['choice'] = 'pedal'
        report.write_text(tomlkit.dumps(review))
        setup_sync.apply_review(report)
        assert source.read_text().startswith('# shorthand\n')
        assert setup_sync.canonical(
            cli.compile_inputs(inputs).setups['show']
        ) == setup_sync.canonical(bytes(edited))


def test_sync_without_baseline_requires_explicit_two_way_choices(
    bundle_peer: Path, tmp_path: Path
) -> None:
    source = tmp_path / 'setup.toml'
    source.write_text('brightness=100\n')
    inputs = data_files.Sources(
        document=[str(source)], target=[str(ROOT / 'firmware/test/target.json')]
    )
    bundle = cli.compile_inputs(inputs)
    original = bundle.setups['setup']
    edited = bytearray(original)
    edited[18] = 75
    struct.pack_into('<I', edited, 20, crc32(edited[28:]))
    with NativePeer([str(bundle_peer)]) as port:
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            client.copy_bundle(
                bundle.model_copy(update={'setups': {'setup': bytes(edited)}}).pack(),
                False,
            )
            client.ok('CNTRL SETUP FINISH')
            report = setup_sync.preview(inputs, client)
        assert not setup_sync.baseline(setup_sync.store(inputs))
        review = tomlkit.parse(report.read_text())
        assert review['changes'][0]['choice'] == 'unresolved'
        assert review['changes'][0]['base']['present'] is False
        with pytest.raises(ValueError, match='resolve every choice'):
            setup_sync.apply_review(report)
        review['changes'][0]['choice'] = 'host'
        report.write_text(tomlkit.dumps(review))
        setup_sync.apply_review(report)
        assert source.read_text() == 'brightness=100\n'
        assert setup_sync.baseline(setup_sync.store(inputs))['setup'] == bytes(edited)
        cli.run_setup(cli.Copy(**inputs.model_dump(), port=Path(port), overwrite=True))


def test_setup_copy_transfers_playback_and_looper_dependencies(
    bundle_peer: Path, tmp_path: Path
) -> None:
    device = tmp_path / 'device'
    device.mkdir()
    host = tmp_path / 'host'
    host.mkdir()
    source = host / 'show.toml'
    source.write_text(
        '[pages.loop]\ntype="midi-looper"\n'
        '[patches.play]\noperation="play-midi-file"\nfilename="intro.mid"\n'
        '[patches.load]\noperation="midi-looper"\naction="load"\nfilename="loop.mid"\n'
        '[patch_cycles]\nmain=["play","load"]\n'
    )
    # An SMF with a long SysEx crosses USB blocks and exercises exact byte transfer.
    track = b'\0\xf0\xc0\0' + b'\x01' * 8191 + b'\xf7\0\xff\x2f\0'
    intro = (
        b'MThd\0\0\0\x06\0\0\0\x01\x03\xe8MTrk' + len(track).to_bytes(4, 'big') + track
    )
    loop = bytes.fromhex(
        '4d546864000000060000000103e84d54726b0000000d00903c7f8768803c0000ff2f00'
    )
    (host / 'intro.mid').write_bytes(intro)
    (host / 'loop.mid').write_bytes(loop)
    (host / 'unused.mid').write_bytes(b'not referenced')
    target = [str(ROOT / 'firmware/test/target.json')]
    with NativePeer([str(bundle_peer), str(device)]) as port:
        command = cli.Copy(document=[str(source)], target=target, port=Path(port))
        cli.run_setup(command)
        assert (device / 'cntrl/midi/intro.mid').read_bytes() == intro
        assert (device / 'cntrl/midi/loop.mid').read_bytes() == loop
        assert not (device / 'cntrl/midi/unused.mid').exists()
        root = host / 'setup-sync'
        uploaded = next(root.glob('upload-*/uploaded'))
        assert (uploaded / 'midi/intro.mid').read_bytes() == intro
        assert (uploaded / 'midi/loop.mid').read_bytes() == loop
        replacement = intro[:30] + b'\x02' + intro[31:]
        (host / 'intro.mid').write_bytes(replacement)
        # A newly added setup cannot replace an independently different asset
        # unless the same overwrite flag has been explicitly provided.
        another = command.model_copy(update={'document': [f'Another={source}']})
        with pytest.raises(
            compiler.CompilationError, match='MIDI file exists with different contents'
        ):
            cli.run_setup(another)
        assert (device / 'cntrl/midi/intro.mid').read_bytes() == intro
        cli.run_setup(another.model_copy(update={'overwrite': True}))
        assert (device / 'cntrl/midi/intro.mid').read_bytes() == replacement
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            assert sorted(e.name for e in client.catalogue()) == ['Another', 'show']
            assert client.download_midi('loop.mid') == loop
        # Repeating the upload with identical assets succeeds without replacing them.
        cli.run_setup(command.model_copy(update={'overwrite': True}))


def test_download_collects_setups_companions_and_midi_before_writing(
    bundle_peer: Path, tmp_path: Path
) -> None:
    host = tmp_path / 'host'
    host.mkdir()
    device = tmp_path / 'device'
    device.mkdir()
    (host / 'Other.toml').write_text(
        '[patches.play]\noperation="play-midi-file"\nfilename="song.mid"\n'
    )
    source = host / 'Show.toml'
    source.write_text(
        'inline_patches=false\n[patch_cycles]\nmain=[{document="Other",patch="play"}]\n'
    )
    midi = bytes.fromhex('4d546864000000060000000103e84d54726b0000000400ff2f00')
    (host / 'song.mid').write_bytes(midi)
    target = [str(ROOT / 'firmware/test/target.json')]
    with NativePeer([str(bundle_peer), str(device)]) as port:
        cli.run_setup(cli.Copy(document=[str(source)], target=target, port=Path(port)))
        output = tmp_path / 'download'
        cli.run_setup(cli.Download(output=output, port=Path(port)))
        assert sorted(p.name for p in output.iterdir()) == [
            'Other.toml',
            'Show.toml',
            'song.mid',
        ]
        assert (output / 'song.mid').read_bytes() == midi
        rebuilt = cli.compile_inputs(
            data_files.Sources(document=[str(output / 'Show.toml')], target=target)
        )
        assert rebuilt.names == ['Other', 'Show']
        assert midi_assets.collect(
            rebuilt.setups, {p: p.read_bytes() for p in output.glob('*.toml')}
        ) == {'song.mid': midi}
        # Selecting only Show still downloads its live companion and assets.
        selected = tmp_path / 'selected'
        cli.run_setup(cli.Download(names=['Show'], output=selected, port=Path(port)))
        assert sorted(p.name for p in selected.iterdir()) == [
            'Other.toml',
            'Show.toml',
            'song.mid',
        ]
        blocked = tmp_path / 'blocked'
        blocked.mkdir()
        (blocked / 'song.mid').write_bytes(b'existing MIDI')
        command = cli.Download(names=['Show'], output=blocked, port=Path(port))
        with pytest.raises(compiler.CompilationError, match='--force/-f'):
            cli.run_setup(command)
        assert [p.name for p in blocked.iterdir()] == ['song.mid']
        assert (blocked / 'song.mid').read_bytes() == b'existing MIDI'
        cli.run_setup(command.model_copy(update={'force': True}))
        assert (blocked / 'song.mid').read_bytes() == midi
        # A missing asset must also leave a nonexistent output directory untouched.
        (device / 'cntrl/midi/song.mid').unlink()
        missing = tmp_path / 'missing'
        with pytest.raises(FileNotFoundError):
            cli.run_setup(cli.Download(output=missing, port=Path(port)))
        assert not missing.exists()


def test_progress_reply_is_not_returned_as_the_command_result() -> None:
    connection = transport.SerialConnection(Path('/unused'))
    connection.received.extend(
        transport.Frame(kind=transport.Kind.text, session=1, sequence=1, payload=x)
        for x in (b'CNTRL WAIT\n', b'CNTRL WAIT\n', b'CNTRL OK\n')
    )
    assert connection.line() == 'CNTRL OK'


def test_reconnect_does_not_repeat_rotation_or_transfer_bytes(
    peer: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    data = bytes.fromhex((ROOT / 'firmware/test/show.golden').read_text())
    with NativePeer([str(peer)]) as port:
        with transport.SerialConnection(Path(port), timeout=1) as connection:
            client = transport.SetupClient(connection)
            write_frame = connection.write_frame
            read_frame = connection.read_frame
            writes = 0
            losses = 0
            data_frame = False

            def interrupted_write(frame: bytes) -> None:
                nonlocal writes, data_frame
                writes += 1
                data_frame = (
                    transport.Frame.decode(frame).payload
                    == data[: connection.usb_block_size]
                )
                if writes == 3:
                    write_frame(frame[: len(frame) // 2])
                    raise OSError('injected partial USB write')
                write_frame(frame)

            def lost_reply() -> transport.Frame:
                nonlocal losses
                result = read_frame()
                if data_frame and losses == 0:
                    losses += 1
                    raise OSError('injected lost data acknowledgement')
                return result

            monkeypatch.setattr(connection, 'write_frame', interrupted_write)
            monkeypatch.setattr(connection, 'read_frame', lost_reply)
            client.copy('A', data, False)
            assert losses == 1
            client.copy('B', data, False)
            client.select('A')
            losses = 0

            def lost_completion() -> transport.Frame:
                nonlocal losses
                result = read_frame()
                if result.payload == b'CNTRL OK\n' and losses < 3:
                    losses += 1
                    raise OSError('injected lost command completion')
                return result

            monkeypatch.setattr(connection, 'read_frame', lost_completion)
            monkeypatch.setattr(transport.time, 'sleep', lambda _: None)
            client.ok('CNTRL SETUP FORWARD')
            assert losses == 3
            assert client.current() == 'B'
            losses = 0
            client.ok(f'CNTRL HISTORY SAVE {b"A".hex()} 0 -')
            assert losses == 3
            revision = client.request(f'CNTRL HISTORY LIST {b"A".hex()} {2**64 - 1}')
            assert revision.split()[2] == '1'
            assert client.request(f'CNTRL HISTORY LIST {b"A".hex()} 1') == 'CNTRL END'
            assert client.download('A') == data


def test_usb_wait_backoff_keeps_retrying_until_control_c(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    connection = transport.SerialConnection(Path('/unplugged'))
    attempts = 0
    seconds = 0

    def unplugged() -> None:
        nonlocal attempts
        attempts += 1
        raise FileNotFoundError('USB unplugged')

    def wait(duration: float) -> None:
        nonlocal seconds
        seconds += int(duration)
        if seconds == 15:
            raise KeyboardInterrupt

    monkeypatch.setattr(connection, 'connect', unplugged)
    monkeypatch.setattr(transport.time, 'sleep', wait)
    with pytest.raises(KeyboardInterrupt):
        connection.__enter__()
    assert attempts == 6
    assert capsys.readouterr().err.splitlines() == [
        f'Waiting {n} seconds for USB: USB unplugged (hit control-C to exit)'
        for n in (1, 2, 4, 8)
    ]


@pytest.mark.parametrize(
    'response,message',
    [
        (
            transport.Frame(
                kind=transport.Kind.error,
                status=transport.Status.version,
                session=0xABC,
                sequence=1,
            ),
            'Binary USB status',
        ),
        (
            transport.Frame(kind=transport.Kind.text, session=0xABC, sequence=2),
            'Unexpected USB sequence',
        ),
    ],
)
def test_incompatible_usb_replies_fail_without_reconnecting(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    response: transport.Frame,
    message: str,
) -> None:
    connection = transport.SerialConnection(Path('unused'))
    monkeypatch.setattr(connection, 'connect', lambda: None)
    monkeypatch.setattr(connection, 'write_frame', lambda _: None)
    connection.session = 0xABC
    monkeypatch.setattr(connection, 'read_frame', lambda: response)

    def unexpected_wait(_: float) -> None:
        pytest.fail('A protocol error must not retry as a USB disconnection')

    monkeypatch.setattr(transport.time, 'sleep', unexpected_wait)
    try:
        with pytest.raises(transport.UsbProtocolError, match=message) as caught:
            with connection:
                pytest.fail('An incompatible handshake must not open a session')
        report = str(caught.value)
        assert report.startswith('ERROR: ')
        assert 'Port: unused' in report
        assert transport.__file__ in report
        assert 'session=abc, sequence=1, acknowledged_reply=0' in report
        assert 'usb_block_size=4096' in report
        assert repr(response.encode()) in report
        assert connection.serial is None
        assert 'Waiting' not in capsys.readouterr().err
    finally:
        connection.close()


def test_invalid_usb_fields_report_the_full_diagnostic(
    monkeypatch: pytest.MonkeyPatch, file_regression: 'FileRegressionFixture'
) -> None:
    connection = transport.SerialConnection(Path('unused'))
    connection.session = 0x1234
    connection.sequence = 9
    connection.acknowledged = 7
    monkeypatch.setattr(connection, 'connect', lambda: None)
    monkeypatch.setattr(connection, 'write_frame', lambda _: None)
    monkeypatch.setattr(
        connection,
        'read_frame',
        lambda: transport.Frame(
            kind=transport.Kind.error,
            status=transport.Status.invalid,
            session=0x1234,
            sequence=9,
            acknowledged=8,
        ),
    )
    with pytest.raises(transport.UsbProtocolError) as caught:
        connection.exchange(b'')
    file_regression.check(
        str(caught.value).replace(transport.__file__, '<transport.py>')
    )


def test_protocol_error_during_a_command_does_not_retry_session_cleanup(
    peer: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    with NativePeer([str(peer)]) as port:
        connection = transport.SerialConnection(Path(port))
        reads = 0

        def invalid_response() -> transport.Frame:
            nonlocal reads
            reads += 1
            raise connection.protocol_error('Invalid USB frame', b'invalid')

        with pytest.raises(transport.UsbProtocolError, match='Invalid USB'):
            with connection:
                monkeypatch.setattr(connection, 'read_frame', invalid_response)
                transport.SetupClient(connection).request('CNTRL STATUS')
        assert reads == 1
        assert connection.serial is None


def test_midi_file_operations_survive_cpp_storage(peer: Path) -> None:
    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    data = compiler.compile_show(
        {
            'patch_cycles': {
                'Files': [
                    {
                        'operation': 'play-midi-file',
                        'filename': '255-3.mid',
                        'outputs': ['din', 'trs'],
                        'speed': 1.5,
                        'reverse': True,
                    },
                    {'operation': 'record-midi-file', 'channels': [1, 16]},
                ]
            }
        },
        {},
        [],
        target,
    )
    with NativePeer([str(peer)]) as port:
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            client.copy('Files', data, False)
            assert client.download('Files') == data


def test_history_import_preserves_metadata_and_refuses_conflicting_retries(
    bundle_peer: Path,
) -> None:
    from cntrl.patch_switcher import history

    target = models.Target.model_validate(
        compiler.read_document(ROOT / 'firmware/test/target.json')
    )
    data = compiler.compile_show({}, {}, [], target)
    revision = history.Revision(
        number=42, modified=1234567890, length=len(data), label='Before soundcheck'
    )
    with NativePeer([str(bundle_peer)]) as port:
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            expected = history.import_revision(client, 'Evening', revision, data)
            assert history.revisions(client, 'Evening') == [revision]
            assert (
                history.import_revision(client, 'Evening', revision, data) == expected
            )
            with pytest.raises(ValueError, match='history-import-conflict'):
                history.import_revision(
                    client,
                    'Evening',
                    revision.model_copy(update={'label': 'Conflicting'}),
                    data,
                )
            assert client.download('Evening', 42) == expected
