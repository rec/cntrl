"""Exact authoring recovery and immutable transfer over production USB frames."""

from hashlib import sha256
from pathlib import Path
from zlib import crc32

import pytest
from native_peer import NativePeer

from cntrl.patch_switcher import (
    binary_format,
    data_files,
    global_config,
    source_archive,
    transport,
)
from scripts import native_tests


@pytest.fixture(scope='module')
def source_peer(tmp_path_factory: pytest.TempPathFactory) -> Path:
    root = Path(__file__).parents[1]
    executable = tmp_path_factory.mktemp('source-peer') / 'peer'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/bundle_server.cpp'),
            '-o',
            str(executable),
        ]
    )
    return executable


def test_source_transfer_is_immutable_and_does_not_publish_corrupt_uploads(
    source_peer: Path, tmp_path: Path
) -> None:
    content = b'authoring source' * 2000
    identifier = sha256(content).hexdigest()
    name = (identifier + '.bin').encode().hex()
    with NativePeer([str(source_peer), str(tmp_path)]) as port:
        with transport.SerialConnection(Path(port)) as connection:
            client = transport.SetupClient(connection)
            with pytest.raises(FileNotFoundError):
                client.download_source(identifier)
            assert (
                client.request(f'CNTRL SOURCE BEGIN {name} {len(content)} 00000000 0')
                == 'CNTRL SEND'
            )
            for offset in range(0, len(content), connection.usb_block_size):
                client.send_data(
                    transport.Kind.source,
                    content[offset : offset + connection.usb_block_size],
                )
            with pytest.raises(ValueError, match='source-checksum'):
                client.ok('CNTRL SOURCE END')
            with pytest.raises(FileNotFoundError):
                client.download_source(identifier)
            client.copy_source(identifier, content)
            client.copy_source(identifier, content)
            assert client.download_source(identifier) == content
            with pytest.raises(ValueError, match='source-name'):
                client.request(
                    f'CNTRL SOURCE BEGIN {name} {len(content)} {crc32(content):08x} 1'
                )
    assert (tmp_path / 'cntrl/source' / f'{identifier}.bin').read_bytes() == content


def test_recovery_preserves_exact_sources_baseline_and_saved_pedal_edits(
    tmp_path: Path,
) -> None:
    source = tmp_path / 'authored.toml'
    default = tmp_path / 'global.toml'
    original = b'# keep my comment\nbrightness = 192\n'
    source.write_bytes(original)
    trace = data_files.Trace(
        files={source: original, default: b'pickup = false\n'},
        defaults={'Live': default},
        roots={'Live': source},
    )
    recipe = data_files.Sources(document=[f'Live={source}'], target=[])
    archive = source_archive.create(
        recipe, trace, {'Live': binary_format.wrap(b'uploaded')}
    )
    assert (
        source_archive.create(recipe, trace, {'Live': binary_format.wrap(b'uploaded')})
        == archive
    )
    saved = source_archive.attach(binary_format.wrap(b'pedal edits'), archive)
    source.write_bytes(b'brightness = 1\n')
    client = ArchiveClient({sha256(archive).hexdigest(): archive})
    recovered = source_archive.download(client, {'Live': saved})
    for relative, content in recovered.items():
        path = tmp_path / 'recovered' / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    directory = tmp_path / 'recovered/sessions' / sha256(archive).hexdigest()
    recovered_recipe = source_archive.recovered_recipe(directory)
    expression = data_files.expressions(recovered_recipe.document)[0]
    assert expression.paths[0].read_bytes() == original
    defaults, relocations = source_archive.session_context(expression)
    assert defaults['Live'].read_bytes() == b'pickup = false\n'
    assert relocations[source].read_bytes() == original
    errors: list[str] = []
    renamed = expression.model_copy(update={'name': 'LiveCopy'})
    inherited = global_config.load_setup(renamed, True, errors)
    assert errors == [] and inherited['pickup'] is False
    assert source_archive.recovered_baselines(directory) == {
        'Live': binary_format.wrap(b'uploaded')
    }
    assert (directory / 'pedal/Live.bin').read_bytes() == saved
    assert binary_format.has_opaque(saved) is False
    binary_format.require_structural_edit(saved)
    assert (
        source_archive.identifier(binary_format.replace_core(saved, b'changed'))
        == sha256(archive).hexdigest()
    )


def test_mixed_recovered_origins_keep_each_setups_defaults(tmp_path: Path) -> None:
    shared_origin = Path('/original/rig/global.toml')
    trace = data_files.Trace()
    documents = []
    for name, brightness in [('One', 10), ('Two', 20)]:
        directory = tmp_path / name
        directory.mkdir()
        root, defaults = directory / 'setup.toml', directory / 'global.toml'
        trace.files[root] = b'# original overrides\n'
        trace.files[defaults] = f'brightness = {brightness}\n'.encode()
        trace.defaults[name] = defaults
        trace.roots[name] = root
        trace.relocations_by_setup[name] = {
            Path('/original/rig/setup.toml'): root,
            shared_origin: defaults,
        }
        documents.append(f'{name}={root}')
    archive = source_archive.create(
        data_files.Sources(document=documents, target=[]),
        trace,
        {'One': b'one', 'Two': b'two'},
    )
    client = ArchiveClient({sha256(archive).hexdigest(): archive})
    recovered = source_archive.download(
        client, {n: source_archive.attach(b'setup', archive) for n in ('One', 'Two')}
    )
    for relative, content in recovered.items():
        path = tmp_path / 'recovered' / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    directory = tmp_path / 'recovered/sessions' / sha256(archive).hexdigest()
    recipe = source_archive.recovered_recipe(directory)
    for expression, expected in zip(
        data_files.expressions(recipe.document), [10, 20], strict=True
    ):
        errors = []
        settings = global_config.load_setup(expression, True, errors)
        assert errors == []
        assert settings['brightness'] == expected


@pytest.mark.parametrize(
    'records',
    [
        [binary_format.Record(tag=2, data=b'bad')],
        [binary_format.Record(tag=2, data=bytes(32))] * 2,
    ],
)
def test_malformed_source_references_are_rejected(
    records: list[binary_format.Record],
) -> None:
    document = binary_format.Document(
        kind=binary_format.Kind.setup,
        records=[binary_format.Record(tag=1, required=True, data=b'core'), *records],
    )
    with pytest.raises(ValueError, match='source snapshot reference'):
        document.pack()


class ArchiveClient(transport.SetupClient):
    def __init__(self, archives: dict[str, bytes]) -> None:
        self.archives = archives

    def download_source(self, snapshot: str) -> bytes:
        return self.archives[snapshot]
