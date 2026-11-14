"""Collect pinned research examples and inventory their raw configuration features.

This is an offline research inventory by default, not a converter or device tool.
--collect downloads the source manifest's pinned files through the existing gh CLI.
"""

import base64
import gzip
import hashlib
import json
import re
import subprocess
from collections import Counter
from fnmatch import fnmatchcase
from pathlib import Path

import tomlkit
import tyro
from pydantic import BaseModel


class Inventory(BaseModel, frozen=True):
    directory: Path = Path(__file__).with_name('backup_examples')
    collect: bool = False


def inspect_backup(document: dict[str, object]) -> dict[str, object]:
    """Count encoded features without interpreting firmware-specific payloads."""
    groups: dict[tuple[str, int], list[tuple[str, dict]]] = {}
    flags: Counter[str] = Counter()
    tokens: Counter[str] = Counter()
    slots: Counter[str] = Counter()
    keys: dict[str, set[str]] = {}
    banks: list[str] = []

    def visit(value: object, pointer: str, context: str) -> None:
        if isinstance(value, list):
            for i, v in enumerate(value):
                visit(v, f'{pointer}/{i}', context)
        elif isinstance(value, dict):
            if 'bankNumber' in value:
                banks.append(str(value.get('bankName', '')))
                keys.setdefault('bank', set()).update(value)
                if value.get('bankClearToggle'):
                    flags['bankClearToggle'] += 1
            if 'presetNum' in value:
                keys.setdefault(
                    'expression' if value.get('isExp') else 'preset', set()
                ).update(value)
                for k in ('toToggle', 'toBlink', 'toMsgScroll'):
                    if value.get(k):
                        flags[k] += 1
            if 't' in value and 'm' in value:
                slots[context] += 1
                if value['t'] != 0:
                    groups.setdefault((context, value['t']), []).append(
                        (pointer, value)
                    )
                return
            for k, v in value.items():
                if k in (
                    'shortName',
                    'longName',
                    'toggleName',
                    'shiftName',
                ) and isinstance(v, str):
                    tokens.update(re.findall(r'%[A-Za-z]+\d*', v))
                child_context = 'bank' if k == 'bankMsgArray' else context
                if k == 'msgArray':
                    child_context = (
                        'expression'
                        if value.get('isExp') or context == 'expression'
                        else 'preset'
                    )
                if k == 'expPresetArray':
                    child_context = 'expression'
                visit(v, f'{pointer}/{k}', child_context)

    visit(document.get('data'), '/data', 'other')
    messages: list[dict[str, object]] = []
    for (context, kind), entries in sorted(groups.items()):
        payloads = [
            json.dumps(
                m.get('data', {k: v for k, v in m.items() if re.fullmatch(r'd\d+', k)}),
                separators=(',', ':'),
            )
            for _, m in entries
        ]
        layouts = Counter(
            f'data[{len(m["data"])}]' if isinstance(m.get('data'), list) else 'd-fields'
            for _, m in entries
        )
        messages.append(
            {
                'context': context,
                'type': kind,
                'count': len(entries),
                'actions': dict(
                    sorted(
                        Counter(str(m.get('a', 'absent')) for _, m in entries).items()
                    )
                ),
                'toggle_positions': dict(
                    sorted(
                        Counter(str(m.get('tg', 'absent')) for _, m in entries).items()
                    )
                ),
                'channels': dict(
                    sorted(
                        Counter(str(m.get('c', 'absent')) for _, m in entries).items()
                    )
                ),
                'layouts': dict(layouts),
                'distinct_payloads': len(set(payloads)),
                'first_pointer': entries[0][0],
                'first_payload': payloads[0],
            }
        )
    data = document.get('data')
    return {
        'format': 'native'
        if 'schemaVersion' in document
        else 'supplementary-authoring',
        'schema_version': document.get('schemaVersion', 'absent'),
        'device_model': document.get('deviceModel', 'absent'),
        'dump_type': document.get('dumpType', 'absent'),
        'body_sha256': hashlib.sha256(
            json.dumps(data, sort_keys=True, separators=(',', ':')).encode()
        ).hexdigest(),
        'data_sections': sorted(data) if isinstance(data, dict) else [],
        'controller_settings_fields': sorted(data.get('controller_settings', {}))
        if isinstance(data, dict)
        else [],
        'bank_count': len(banks),
        'bank_names': banks,
        'field_names': {k: sorted(v) for k, v in sorted(keys.items())},
        'message_slots': dict(sorted(slots.items())),
        'enabled_flags': dict(sorted(flags.items())),
        'display_tokens': dict(sorted(tokens.items())),
        'messages': messages,
    }


def inventory(directory: Path) -> dict[str, list[dict]]:
    """Verify original bytes and produce a deterministic, offline evidence index."""
    manifest = tomlkit.parse((directory / 'manifest.toml').read_text())
    backups: list[dict[str, object]] = []
    captures: list[dict[str, object]] = []
    for entry in manifest['files']:
        content = (directory / entry['path']).read_bytes()
        if entry['kind'] != 'license':
            content = gzip.decompress(content)
        if (
            len(content) != entry['bytes']
            or hashlib.sha256(content).hexdigest() != entry['sha256']
        ):
            raise ValueError(f'{entry["path"]}: source bytes differ from manifest')
        if entry['kind'] == 'backup':
            backups.append(
                {
                    'path': entry['path'],
                    'profile': entry['profile'],
                    **inspect_backup(json.loads(content)),
                }
            )
        elif entry['kind'] == 'sysex':
            captures.append(
                {
                    'path': entry['path'],
                    'bytes': len(content),
                    'prefix_hex': content[:8].hex(),
                    'framed': content.startswith(b'\xf0') and content.endswith(b'\xf7'),
                }
            )
    return {'backups': backups, 'captures': captures}


def collect(directory: Path) -> None:
    sources = tomlkit.parse((directory / 'sources.toml').read_bytes().decode())[
        'sources'
    ]
    files: list[dict[str, object]] = []
    for source in sources:
        repository, revision = source['repository'], source['revision']
        response = subprocess.run(
            ['gh', 'api', f'repos/{repository}/git/trees/{revision}?recursive=1'],
            stdout=subprocess.PIPE,
            check=True,
        )
        tree = json.loads(response.stdout)
        if tree.get('truncated'):
            raise ValueError(f'{repository}: truncated source tree')
        selected = [
            e
            for e in tree['tree']
            if e['type'] == 'blob'
            and any(fnmatchcase(e['path'], p) for p in source['include'])
        ]
        for entry in sorted(selected, key=lambda e: e['path']):
            original = entry['path']
            response = subprocess.run(
                ['gh', 'api', f'repos/{repository}/git/blobs/{entry["sha"]}'],
                stdout=subprocess.PIPE,
                check=True,
            )
            content = base64.b64decode(json.loads(response.stdout)['content'])
            if len(content) != entry['size']:
                raise ValueError(
                    f'{repository}/{original}: download size differs from Git tree'
                )
            kind = (
                'backup'
                if original.endswith('.json')
                else 'sysex'
                if original.endswith('.sysex')
                else 'license'
            )
            relative = Path(source['name']) / original
            if kind == 'backup':
                relative = relative.with_suffix('.backup.gz')
            elif kind == 'sysex':
                relative = relative.with_suffix('.sysex.gz')
            destination = directory / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(
                gzip.compress(content, mtime=0) if kind != 'license' else content
            )
            files.append(
                {
                    'source': source['name'],
                    'profile': source['profile'],
                    'original': original,
                    'path': relative.as_posix(),
                    'kind': kind,
                    'bytes': len(content),
                    'sha256': hashlib.sha256(content).hexdigest(),
                    'git_blob': entry['sha'],
                }
            )
        print(f'{repository}@{revision}: {len(selected)} files', flush=True)
    (directory / 'manifest.toml').write_bytes(tomlkit.dumps({'files': files}).encode())


if __name__ == '__main__':
    options = tyro.cli(Inventory)
    if options.collect:
        collect(options.directory)
    else:
        report = inventory(options.directory)
        (options.directory / 'inventory.toml').write_text(tomlkit.dumps(report))
        for b in report['backups']:
            print(
                b['path'],
                b['device_model'],
                b['bank_count'],
                [(m['context'], m['type'], m['count']) for m in b['messages']],
            )
            print(
                'flags:',
                b['enabled_flags'],
                'tokens:',
                b['display_tokens'],
                'actions:',
                sorted({a for m in b['messages'] for a in m['actions']}),
                'positions:',
                sorted({t for m in b['messages'] for t in m['toggle_positions']}),
            )
