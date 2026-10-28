"""Compile two-state assignments using resolved MIDI messages."""

import struct

from . import display, models


def compile_actions(
    show: models.Show,
    instruments: dict[str, models.Instrument],
    patches: list[bytes],
    names: list[str | None],
    target: models.Target,
) -> tuple[list[bytes], list[display.DisplayPatch]]:
    if len(show.actions) > target.max_actions:
        raise ValueError('actions exceed target capacity')
    records: list[bytes] = []
    views: list[display.DisplayPatch] = []
    buttons: set[str] = set()
    resources: list[tuple[str, int, set[int], bool]] = []
    for key, assignment in show.actions.items():
        name = key if assignment.name is None else assignment.name
        for text in (key,):
            if (
                not text
                or len(text) > 63
                or any(
                    c not in target.characters or not 32 <= ord(c) <= 126 for c in text
                )
            ):
                raise ValueError(
                    'action identities and names require 1..63 supported characters'
                )
        if (
            not name
            or len(name.encode('utf-8')) > 63
            or any(ord(c) < 32 or ord(c) == 127 for c in name)
        ):
            raise ValueError('action names require 1..63 bytes of display text')
        if assignment.button in buttons:
            raise ValueError('duplicate action button')
        if target.buttons and assignment.button not in target.buttons:
            raise ValueError('action button is unavailable on this target')
        buttons.add(assignment.button)
        commands: list[bytes] = []
        patch_indices: list[int] = []
        destinations: list[tuple[str, int]] = []
        instrument_indices: list[int] = []
        controllers: set[int] = set()
        is_patch = isinstance(assignment.on, models.PatchCommand)
        for command in (assignment.on, assignment.off):
            if isinstance(command, models.PatchCommand) != is_patch:
                raise ValueError('action On and Off must have the same command kind')
            if isinstance(command, models.PatchCommand):
                matches = [i for i, n in enumerate(names) if n == command.patch]
                if len(matches) != 1:
                    raise ValueError(
                        'action patch must identify exactly one named patch: '
                        f'{command.patch}'
                    )
                patch = patches[matches[0]]
                if struct.unpack_from('<H', patch)[0] == 65535:
                    raise ValueError('function patches cannot be action states')
                patch_indices.append(matches[0])
                instrument = list(instruments.values())[
                    struct.unpack_from('<H', patch)[0]
                ]
                instrument_indices.append(struct.unpack_from('<H', patch)[0])
                data = bytearray()
                for number, value in ((0, patch[5]), (32, patch[6])):
                    if value != 255:
                        data.extend((0xB0 | (instrument.channel - 1), number, value))
                        controllers.add(number)
                data.extend((0xC0 | (instrument.channel - 1), patch[4]))
                for number, value in ((7, patch[7]), (10, patch[8])):
                    if value != 255:
                        data.extend((0xB0 | (instrument.channel - 1), number, value))
                        controllers.add(number)
                commands.append(bytes(data))
            else:
                if command.instrument not in instruments:
                    raise ValueError(f'unknown action instrument: {command.instrument}')
                instrument = instruments[command.instrument]
                instrument_indices.append(list(instruments).index(command.instrument))
                controllers.add(command.controller)
                commands.append(
                    bytes(
                        (
                            0xB0,
                            command.controller,
                            command.value,
                        )
                    )
                )
            destinations.append((instrument.output, instrument.channel))
        if destinations[0] != destinations[1]:
            raise ValueError('action states must target the same output and channel')
        output, channel = destinations[0]
        if any(
            o == output and c == channel and ((p and is_patch) or bool(controllers & r))
            for o, c, r, p in resources
        ):
            raise ValueError('conflicting action MIDI ownership')
        resources.append((output, channel, controllers, is_patch))
        records.append(
            RECORD.pack(
                ord(assignment.button) - ord('A'),
                int(assignment.mode == 'latched') | (2 if is_patch else 0),
                instrument_indices[0],
                0 if is_patch else 3,
                0 if is_patch else 3,
                struct.pack('<H', patch_indices[0]) if is_patch else commands[0],
                struct.pack('<H', patch_indices[1])
                if is_patch
                else commands[1].ljust(12, b'\0')
                + struct.pack('<H', instrument_indices[1]),
                name.encode('utf-8'),
                0,
            )
        )
        views.append(
            display.DisplayPatch(
                context=f'actions.{key}',
                values={
                    'group.name': 'Actions',
                    'display_name': name,
                    'action.name': f'{assignment.button}: {name}',
                    'action.mode': assignment.mode,
                    'action.button': assignment.button,
                    'action.state': 'Unknown / Unavailable',
                },
                style=assignment.style,
            )
        )
    return records, views


RECORD = struct.Struct('<BBHBB14s14s64sH')
