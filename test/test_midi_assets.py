"""Setup dependencies retain their declaring file's directory."""

from pathlib import Path

import pytest
import tomlkit

from cntrl.patch_switcher import cli, data_files, midi_assets


def test_referenced_files_use_the_declaring_document_not_the_working_directory(
    tmp_path: Path,
) -> None:
    definitions = tmp_path / 'library'
    definitions.mkdir()
    source = definitions / 'patches.toml'
    source.write_text(
        '[patches.play]\noperation="play-midi-file"\nfilename="intro.mid"\n'
    )
    (definitions / 'intro.mid').write_bytes(b'MThd from the library')
    root = tmp_path / 'show.toml'
    root.write_text('[patch_cycles]\nmain=["play"]\n')
    inputs = data_files.Sources(
        document=[str(root), '+', str(source)],
        target=[str(ROOT / 'firmware/test/target.json')],
    )
    trace = data_files.Trace()
    bundle = cli.compile_inputs(inputs, trace=trace)
    assert midi_assets.collect(bundle.setups, trace.files) == {
        'intro.mid': b'MThd from the library'
    }
    (definitions / 'intro.mid').unlink()
    with pytest.raises(FileNotFoundError):
        midi_assets.collect(bundle.setups, trace.files)


def test_companion_and_nested_references_are_included_without_unused_files(
    tmp_path: Path,
) -> None:
    companion = tmp_path / 'Other.toml'
    companion.write_text(
        '[patches.play]\npatches=[{operation="play-midi-file",filename="song.mid"},{operation="play-midi-file"}]\n'
    )
    (tmp_path / 'song.mid').write_bytes(b'song bytes')
    (tmp_path / 'unused.mid').write_bytes(b'not referenced')
    source = tmp_path / 'show.toml'
    source.write_text('[patch_cycles]\nmain=[{document="Other",patch="play"}]\n')
    trace = data_files.Trace()
    bundle = cli.compile_inputs(
        data_files.Sources(
            document=[str(source)], target=[str(ROOT / 'firmware/test/target.json')]
        ),
        trace=trace,
    )
    assert midi_assets.collect(bundle.setups, trace.files) == {
        'song.mid': b'song bytes'
    }


def test_shared_pedal_filenames_require_identical_bytes(tmp_path: Path) -> None:
    sources: dict[Path, bytes] = {}
    documents = []
    for name, data in [('One', b'first'), ('Two', b'second')]:
        folder = tmp_path / name
        folder.mkdir()
        path = folder / 'show.toml'
        path.write_text(
            '[patches.play]\noperation="play-midi-file"\nfilename="shared.mid"\n'
        )
        (folder / 'shared.mid').write_bytes(data)
        documents.append(f'{name}={path}')
        sources[path] = path.read_bytes()
    bundle = cli.compile_inputs(
        data_files.Sources(
            document=documents, target=[str(ROOT / 'firmware/test/target.json')]
        )
    )
    with pytest.raises(
        ValueError, match='different source files use the same pedal filename'
    ):
        midi_assets.collect(bundle.setups, sources)
    (tmp_path / 'Two/shared.mid').write_bytes(b'first')
    assert midi_assets.collect(bundle.setups, sources) == {'shared.mid': b'first'}


def test_filename_discovery_includes_looper_and_stream_sources() -> None:
    source = tomlkit.parse("""
[patches.loop]
operation = 'midi-looper'
action = 'load'
filename = 'loop.mid'
[patches.play]
operation = 'play-midi-file'
[midi.backing]
generate = {file = {filename = 'backing.mid'}}
""").unwrap()
    assert midi_assets.filenames(source) == ['backing.mid', 'loop.mid']


def test_a_patch_named_filename_is_not_a_file_dependency(tmp_path: Path) -> None:
    source = tmp_path / 'show.toml'
    source.write_text('[patches]\nfilename="A"\nA={operation="play-midi-file"}\n')
    trace = data_files.Trace()
    bundle = cli.compile_inputs(
        data_files.Sources(
            document=[str(source)], target=[str(ROOT / 'firmware/test/target.json')]
        ),
        trace=trace,
    )
    assert midi_assets.collect(bundle.setups, trace.files) == {}


def test_derived_file_overrides_and_stream_generators_are_uploaded(
    tmp_path: Path,
) -> None:
    source = tmp_path / 'show.toml'
    source.write_text(
        '[patches.base]\noperation="play-midi-file"\nfilename="base.mid"\n'
        '[patches.faster]\nparent="base"\nfilename="fast.mid"\n'
        '[midi.backing]\ngenerate={file={filename="backing.mid"}}\nto="din"\n'
    )
    for name in ('base.mid', 'fast.mid', 'backing.mid'):
        (tmp_path / name).write_bytes(name.encode())
    trace = data_files.Trace()
    bundle = cli.compile_inputs(
        data_files.Sources(
            document=[str(source)], target=[str(ROOT / 'firmware/test/target.json')]
        ),
        trace=trace,
    )
    assert midi_assets.collect(bundle.setups, trace.files) == {
        n: n.encode() for n in ('backing.mid', 'base.mid', 'fast.mid')
    }


ROOT = Path(__file__).parents[1]
