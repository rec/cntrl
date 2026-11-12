"""Locate explicitly referenced MIDI files at their authoring documents."""

import json
from pathlib import Path

import tomlkit

from . import models, resolved


def filenames(value: object) -> list[str]:
    result: list[str] = []
    pending = [value]
    while pending:
        item = pending.pop()
        if isinstance(item, dict):
            patch = item.get('operation') in (
                'play-midi-file',
                'midi-looper',
            ) or isinstance(item.get('parent'), str)
            names = [item.get('filename')] if patch else []
            if isinstance(generator := item.get('generate'), dict) and isinstance(
                file := generator.get('file'), dict
            ):
                names.append(file.get('filename'))
            for name in names:
                if not isinstance(name, str) or not name:
                    continue
                models.PlayMidiFile.basename(name)
                if name not in result:
                    result.append(name)
            pending.extend(item.values())
        elif isinstance(item, list):
            pending.extend(item)
    return sorted(result)


def referenced(setups: dict[str, bytes]) -> list[str]:
    return sorted(
        {
            n
            for d in setups.values()
            for n in filenames(tomlkit.parse(resolved.export(d)).unwrap())
        }
    )


def collect(setups: dict[str, bytes], sources: dict[Path, bytes]) -> dict[str, bytes]:
    required = referenced(setups)
    origins: dict[str, list[Path]] = {n: [] for n in required}
    for source, content in sources.items():
        document = (
            json.loads(content)
            if source.suffix == '.json'
            else tomlkit.parse(content.decode()).unwrap()
        )
        for name in filenames(document):
            if name in origins and (path := source.parent / name) not in origins[name]:
                origins[name].append(path)
    result: dict[str, bytes] = {}
    cases: dict[str, str] = {}
    for name in required:
        if name.lower().startswith('.cntrl-'):
            raise ValueError(f'{name}: reserved MIDI transfer filename')
        if name.lower() in cases and cases[name.lower()] != name:
            raise ValueError(
                f'{name}: MIDI filenames differ only in case from {cases[name.lower()]}'
            )
        cases[name.lower()] = name
        if not origins[name]:
            raise ValueError(f'{name}: cannot locate the declaring source document')
        for path in origins[name]:
            data = path.read_bytes()
            if not data or len(data) > 0xFFFFFFFF:
                raise ValueError(f'{path}: MIDI file must contain 1..4294967295 bytes')
            if name in result and result[name] != data:
                raise ValueError(
                    f'{name}: different source files use the same pedal filename: '
                    f'{origins[name]}'
                )
            result[name] = data
    return result
