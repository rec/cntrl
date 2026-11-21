"""Recover inherited rig sources and binary assets without the original machine."""

from hashlib import sha256
from pathlib import Path

import pytest
import tomlkit

from cntrl.animation import asset
from cntrl.patch_chooser import (
    cli,
    data_files,
    global_config,
    midi_assets,
    setup_sync,
    source_archive,
)
from cntrl.patch_chooser.setup_compiler import compile_inputs


class ArchivedSource:
    def __init__(self, archive: bytes) -> None:
        self.archive = archive

    def download_source(self, identifier: str) -> bytes:
        assert identifier == sha256(self.archive).hexdigest()
        return self.archive


def test_recovered_rig_recompiles_with_its_original_defaults_and_assets(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    rig = tmp_path / 'rig'
    rig.mkdir()
    instruments = rig / 'instruments'
    instruments.mkdir()
    (instruments / 'synth.toml').write_text(
        "[instruments.synth]\noutput='din'\nchannel=1\n"
    )
    (instruments / 'live.toml').write_text('[instruments.synth]\nchannel=2\n')
    defaults = rig / 'global.toml'
    defaults.write_text("""instrument_files = ['instruments/synth.toml']
brightness = 161
splash_movie = ['intro.cnsa']
[patches.sound]
instrument = 'synth'
program = 10
[patches.play]
operation = 'play-midi-file'
filename = 'intro.mid'
""")
    monkeypatch.setattr(global_config, 'user_path', lambda: defaults)
    root = rig / 'show.toml'
    root.write_text("""instrument_files = ['instruments/live.toml']
[patches.sound]
program = 12
[patch_cycles]
main = ['sound', 'play']
""")
    geometry = asset.Geometry(width=8, height=8)
    movie = asset.encode_frames(bytes(128), [100], geometry)
    (rig / 'intro.cnsa').write_bytes(movie)
    midi = bytes.fromhex('4d546864000000060000000100604d54726b0000000400ff2f00')
    (rig / 'intro.mid').write_bytes(midi)
    target_geometry = rig / 'geometry.toml'
    target_geometry.write_text(tomlkit.dumps({'animation': geometry.model_dump()}))
    inputs = data_files.Sources(
        document=[f'Live={root}'],
        target=[str(ROOT / 'firmware/test/target.json'), '+', str(target_geometry)],
    )
    trace = data_files.Trace()
    movies: dict[int, bytes] = {}
    original = compile_inputs(inputs, movies=movies, trace=trace)
    assert midi_assets.collect(original.setups, trace.files) == {'intro.mid': midi}
    assert trace.files[rig / 'intro.cnsa'] == movie
    assert instruments / 'synth.toml' in trace.files
    assert instruments / 'live.toml' in trace.files
    archived = source_archive.create(inputs, trace, original.setups)
    recovered = tmp_path / 'recovered'
    files = source_archive.download(
        ArchivedSource(archived),
        {'Live': source_archive.attach(original.setups['Live'], archived)},
    )
    cli.publish_download(files, recovered)
    rig.rename(tmp_path / 'unavailable-original-machine')
    different = tmp_path / 'different-defaults.toml'
    different.write_text('brightness = 2\n')
    monkeypatch.setattr(global_config, 'user_path', lambda: different)
    session = next((recovered / 'sessions').iterdir())
    restored_trace = data_files.Trace()
    restored = compile_inputs(
        data_files.Sources(document=[str(session / 'Live.toml')]),
        movies={},
        trace=restored_trace,
    )
    assert setup_sync.canonical(restored.setups['Live']) == setup_sync.canonical(
        original.setups['Live']
    )
    assert midi_assets.collect(restored.setups, restored_trace.files) == {
        'intro.mid': midi
    }


ROOT = Path(__file__).resolve().parents[1]
