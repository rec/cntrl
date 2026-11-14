"""Import the MC6 Pro PC-scroll banks supported by the cntrl runtime."""

import hashlib
import json
import re
import struct
from pathlib import Path
from typing import Annotated
from zlib import crc32

import tomlkit
import tyro
from pydantic import BaseModel

from ....patch_switcher import models


class ImportBackup(BaseModel, frozen=True):
    backup: Annotated[Path, tyro.conf.Positional]
    bank: int = 0
    output: Path


def convert(document: dict, bank: int) -> models.Setup:
    """Convert all banks; bank selects the initial bank of the Banked Page."""
    if (
        document.get('schemaVersion'),
        document.get('deviceModel'),
        document.get('dumpType'),
    ) != (1, 6, 'allBanks'):
        raise ValueError('expected a schema-1 MC6 Pro all-banks backup')
    data = document['data']
    banks = {b['bankNumber']: b for b in data['bankArray']}
    settings = data['controller_settings']['data']
    general = settings['controller_settings']['data']
    if settings['bank_arrangement']['isActive']:
        raise ValueError('active bank arrangements are not supported yet')
    if sorted(banks) != list(range(len(banks))) or bank not in banks:
        raise ValueError('expected consecutive bank slots and a valid initial bank')
    channels = {
        c['data']['channel']: c['data'] for c in settings['midi_channels']['data']
    }
    definitions = {
        c['data']['index']: c['data'] for c in settings['scroll_counters']['data']
    }
    ports = {
        p['data']['portNum']: p['data']['type'] for p in settings['omniports']['data']
    }
    if any(t not in (1, 12) for t in ports.values()):
        raise ValueError('only expression or disabled Omniports are supported')
    counters: dict[str, object] = {}
    patches: dict[str, object] = {}
    bank_pages: list[list[str]] = []

    def label(text: str) -> str:
        # Official editor icon 46 ('.') is the right chevron.
        text = text.replace('\\.', '>')
        if re.search(r'%(?!F[0-9A-Fa-f])', text):
            raise ValueError(f'unsupported name placeholder in {text!r}')
        return re.sub(
            r'%F([0-9A-Fa-f])',
            lambda m: '{counters.counter' + str(int(m[1], 16)) + '}',
            text,
        )

    for number, source in sorted(banks.items()):
        context = f'/data/bankArray/{number}'
        if source['bankClearToggle'] or any(m['t'] for m in source['bankMsgArray']):
            raise ValueError(
                f'{context}: bank messages or clear-toggle are not supported yet'
            )
        bindings: dict[str, object] = {}
        for preset in source['expPresetArray']:
            index = preset['presetNum']
            if ports.get(index) != 1:
                continue
            messages = [m for m in preset['msgArray'] if m['t']]
            if not messages:
                continue
            if (
                len(messages) != 1
                or messages[0]['t'] != 1
                or messages[0]['a'] != 0
                or messages[0]['tg'] != 2
                or any(messages[0]['data'][3:])
            ):
                raise ValueError(
                    f'{context}/expPresetArray/{index}: '
                    'only one continuous CC assignment is supported'
                )
            message = messages[0]
            channel = message['c']
            if (
                channels[channel]['sendToPort'] != 3
                or channels[channel]['remap']
                or channels[channel]['isMidiChannelOffset']
            ):
                raise ValueError(
                    f'{context}/expPresetArray/{index}: unsupported expression routing'
                )
            controller, minimum, maximum = message['data'][:3]
            bindings[f'configurable-{index + 1}'] = models.ExpressionAssignment(
                output='din',
                mirror_output='usb-device',
                channel=channel,
                controller=controller,
                minimum=min(minimum, maximum),
                maximum=max(minimum, maximum),
                reverse=minimum > maximum,
            ).model_dump(exclude_none=True)
        for preset in source['presetArray']:
            messages = [m for m in preset['msgArray'] if m['t']]
            index = preset['presetNum']
            location = f'{context}/presetArray/{index}'
            if any(preset[k] for k in ('toToggle', 'toBlink', 'toMsgScroll')):
                raise ValueError(
                    f'{location}: only non-toggle assignments are supported'
                )
            actions: list[dict[str, object]] = []
            for message in messages:
                pointer = f'{location}/msgArray/{message["m"]}'
                if message['a'] != 1 or message['tg'] != 2:
                    raise ValueError(
                        f'{pointer}: unsupported trigger or toggle position'
                    )
                payload = message['data']
                if message['t'] == 33:
                    operation, counter = payload[0] >> 4, payload[0] & 15
                    if operation not in (0, 1, 2):
                        raise ValueError(
                            f'{pointer}: unsupported PC-scroll opcode {operation}'
                        )
                    channel = message['c']
                    routing = channels[channel]
                    if (
                        routing['sendToPort'] != 3
                        or routing['remap']
                        or routing['isMidiChannelOffset']
                    ):
                        raise ValueError(
                            f'{pointer}: only unremapped DIN + USB routing is supported'
                        )
                    name = f'counter{counter}'
                    definition = definitions[counter]
                    counters[name] = models.Counter(
                        type='program-change',
                        output='din',
                        channel=1,
                        initial=definition['start'] + 1,
                        minimum=definition['min'] + 1,
                        maximum=definition['max'] + 1,
                    ).model_dump(exclude_none=True)
                    for output in ('din', 'usb-device'):
                        actions.append(
                            {
                                'type': 'counter-message',
                                'counter': name,
                                'output': output,
                                'channel': channel,
                                'change': ('none', 'increase', 'decrease')[operation]
                                if output == 'din'
                                else 'none',
                                'wrap': payload[2] == 0,
                            }
                        )
                elif message['t'] in (10, 11):
                    direction = 1 if message['t'] == 10 else -1
                    actions.append(
                        {'operation': 'next-bank' if direction > 0 else 'previous-bank'}
                    )
                else:
                    raise ValueError(
                        f'{pointer}: unsupported message type {message["t"]}'
                    )
            bindings[str(index)] = {
                'name': label(preset['shortName']),
                'display_name': label(preset['longName'] or preset['shortName']),
                'style': {
                    'font_name': 'AtkinsonNextSemiBold',
                    'foreground_color': palette_color(preset['nameColor']),
                    'background_color': palette_color(preset['backgroundColor']),
                },
                'on': {'patches': actions},
            }
        pages = []
        for page in range(source['pageLimit'] or 4):
            root = {
                'name': source['bankName'] or ' ',
                'patches': [],
                'bindings': {
                    k: v for k, v in bindings.items() if k.startswith('configurable-')
                }
                | {'abcdef'[i]: bindings[str(page * 6 + i)] for i in range(6)},
                'style': {
                    'foreground_color': palette_color(source['textColor'])
                    if source['isColorEnabled'] and source['textColor'] != 127
                    else '0x800080',
                    'background_color': palette_color(source['backgroundColor'])
                    if source['isColorEnabled'] and source['backgroundColor'] != 127
                    else 'white',
                },
                'binding_view': {
                    'maximum_size': 2 * (18 - general['presetMaxFontSize']),
                    'detail_size': 18 - general['middleLayerFontSize'],
                    'status_size': (12, 10, 8, 8)[general['bankPageFontSize']],
                    'status_style': {'foreground_color': 'blue'},
                    'align': 'switch' if general['lcdAlign'] else 'left',
                    'show_labels': general['showPresetLabels'],
                    'status': 'B{bank:03d} PG{page}'
                    if general['bankPageFontSize'] != 3
                    else '',
                },
            }
            key = next(
                (n for n, p in patches.items() if p == root), f'bank{number}_page{page}'
            )
            patches[key] = root
            pages.append(key)
        bank_pages.append(pages)
    return models.Setup.model_validate(
        {
            'pages': {
                'performance': {'type': 'patch-switcher', 'min_windows': 3},
                'morningstar': {'type': 'banked', 'patch': 'Morningstar'},
            },
            'patch_cycles': {'Imported': ['Morningstar']},
            'patches': {
                'Morningstar': {
                    'type': 'banked',
                    'name': 'Morningstar',
                    'patches': patches,
                    'banks': bank_pages,
                    'initial_bank': bank,
                    'chords': {}
                    if general['dualLock']
                    else {
                        'next-bank': ['b', 'c'],
                        'previous-bank': ['a', 'b'],
                        'next-bank-page': ['e', 'f'],
                        'previous-bank-page': ['d', 'e'],
                        'menu': ['d', 'f'],
                    },
                }
            },
            'fill_in_styles': False,
            'fast_switches': True,
            'counters': counters,
            'trs_ports': {
                f'configurable-{i + 1}': 'expression'
                for i, t in ports.items()
                if t == 1
            },
        }
    )


def run(command: ImportBackup) -> None:
    content = command.backup.read_bytes()
    setup = convert(json.loads(content), command.bank)
    imported = models.Banked.model_validate(setup.patches['Morningstar'])
    if command.output.exists() and any(command.output.iterdir()):
        raise ValueError(f'import output directory must be empty: {command.output}')
    command.output.mkdir(parents=True, exist_ok=True)
    document = tomlkit.document()
    document.update(
        setup.model_dump(mode='json', exclude_none=True, exclude_defaults=True)
    )
    banks = tomlkit.array()
    for i, b in enumerate(imported.banks):
        name = models.Sequence.model_validate(imported.patches[b[0]]).name or ''
        label = ' '.join(name.split()) or 'unnamed'
        banks.add_line(b, comment=f'Bank {i}: {label}')
    banks.add_line(indent='')
    document['patches']['Morningstar']['banks'] = banks
    (command.output / 'setup.toml').write_text(tomlkit.dumps(document))
    report = {
        'backup': str(command.backup.resolve()),
        'sha256': hashlib.sha256(content).hexdigest(),
        'selected_bank': command.bank,
        'included_banks': len(imported.banks),
        'scope': 'PC-scroll press assignments, continuous expression CCs '
        'and all bank/preset-page slots in one banked patch',
        'presentation': 'saved palette, switch alignment, short and long names, '
        'three display bands; photo-matched Atkinson Next SemiBold labels '
        'and Arial middle-band text substitute for the stock typeface',
        'controller_settings': 'uses scroll counters, expression roles, bank '
        'arrangement, channel routing and display settings; '
        'other global settings are not imported',
    }
    (command.output / 'import.toml').write_text(tomlkit.dumps(report))
    # Virtual ADC inputs span 0..1023; these are not physical pedal calibration.
    calibration = struct.pack('<IHH', 1, 0, 1023)
    session = command.output / 'session' / 'cntrl'
    session.mkdir(parents=True)
    for jack in setup.trs_ports:
        index = int(jack.rsplit('-', 1)[1])
        (session / f'expression-{index}.cal').write_bytes(
            calibration + struct.pack('<I', crc32(calibration))
        )
    (command.output / 'start.toml').write_text(
        tomlkit.dumps(
            {
                'until': '100 milliseconds',
                'events': [
                    {
                        'at': '20 milliseconds',
                        'kind': 'button',
                        'target': 'a',
                        'value': 1,
                    },
                    {
                        'at': '60 milliseconds',
                        'kind': 'button',
                        'target': 'a',
                        'value': 0,
                    },
                ],
            }
        )
    )
    print(
        f'Imported {len(imported.banks)} banks as one patch into '
        f'{command.output / "setup.toml"}'
    )


def palette_color(index: int) -> str:
    """Decode the solid colors used by the saved VL70 banks (editor AQ table)."""
    # Dim black (64) is still black. Other dim colors need mesh rendering.
    if index not in PALETTE:
        raise ValueError(f'unsupported presentation palette color {index}')
    return PALETTE[index]


PALETTE = {
    0: '0x000000',
    1: '0x00FF00',
    3: '0x800080',
    7: '0xFFFFFF',
    8: '0xFFA500',
    29: '0x00FFFF',
    48: '0xF0E68C',
    54: '0xFF8C00',
    56: '0xFA8072',
    64: '0x000000',
}
