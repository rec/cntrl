"""One tagged Patch schema compiles, resolves and round-trips every operation."""

import struct
from pathlib import Path
from zlib import crc32

import pytest
import tomlkit

from cntrl.patch_switcher import compiler, models, resolved


@pytest.fixture
def target() -> models.Target:
    return models.Target.model_validate(
        compiler.read_document(Path('firmware/test/target.json'))
    ).model_copy(update={'functions': ['stock-firmware'], 'buttons': list('ABCDEF')})


def test_every_variant_survives_export_and_recompile(target: models.Target) -> None:
    patches = {
        'program': {
            'type': 'program-change',
            'instrument': 'piano',
            'program': 7,
            'bank': 2,
            'volume': 99,
            'pan': -4,
        },
        'cc': {
            'type': 'control-change',
            'output': 'din',
            'channel': 16,
            'controller': 64,
            'value': 127,
        },
        'bend': {'type': 'pitch-bend', 'output': 'trs', 'channel': 2, 'value': -4321},
        'nrpn': {
            'type': 'nrpn',
            'output': 'din',
            'channel': 3,
            'parameter': 130,
            'value': 388,
        },
        'sysex': {'type': 'sysex', 'output': 'din', 'data': list(range(128)) * 3},
        'raw': {'type': 'midi', 'output': 'din', 'bytes': [0x90, 60, 100]},
        'brightness': {'type': 'state', 'operation': 'brightness', 'value': 255},
        'interface': {
            'type': 'state',
            'operation': 'usb-midi-interface',
            'value': True,
        },
        'stock': {'type': 'stock-firmware', 'exit_buttons': 'F+A'},
        'sequence': {
            'type': 'sequence',
            'name': 'All together',
            'patches': [
                {'type': 'reference', 'patch': 'program'},
                {
                    'type': 'sequence',
                    'patches': [
                        {'type': 'reference', 'patch': 'cc'},
                        {'type': 'reference', 'patch': 'raw'},
                    ],
                },
            ],
        },
    }
    show = {
        'instruments': {
            'piano': {
                'instrument': 'synth',
                'setup': 'factory',
                'output': 'din',
                'channel': 1,
            }
        },
        'definitions': {
            'setups': [
                {
                    'instrument': 'synth',
                    'name': 'factory',
                    'banks': {'2': {'msb': 1, 'lsb': 2}},
                }
            ]
        },
        'patches': patches,
        'groups': {'Live': [{'type': 'reference', 'patch': n} for n in patches]},
    }
    data = compiler.compile_show(show, {}, [], target)
    exported = tomlkit.parse(resolved.export(data)).unwrap()
    assert compiler.compile_show(exported, {}, [], target) == data
    assert exported['patches']['nrpn'] == patches['nrpn']
    assert exported['patches']['sysex']['data'] == patches['sysex']['data']
    assert exported['patches']['stock']['exit_buttons'] == 'A+F'
    assert len(exported['groups']['Live']) == len(patches)


def test_switch_only_setup_needs_no_catalogue(target: models.Target) -> None:
    show = {
        'switches': {
            'clean': {
                'button': 'A',
                'on': {
                    'type': 'control-change',
                    'output': 'din',
                    'channel': 1,
                    'controller': 64,
                    'value': 127,
                },
            }
        }
    }
    data = compiler.compile_show(show, {}, [], target)
    assert compiler.HEADER.unpack_from(data)[4:7] == (0, 0, 0)
    assert (
        compiler.compile_show(
            tomlkit.parse(resolved.export(data)).unwrap(), {}, [], target
        )
        == data
    )


@pytest.mark.parametrize(
    'message',
    [
        [],
        [60, 100],
        [0x90, 60],
        [0x90, 60, 100, 0x90, 61, 100],
        [0xF0, 1],
        [0xF0, 128, 0xF7],
        [0xF7],
        [0xF4],
        [0xF9],
    ],
)
def test_raw_midi_requires_one_complete_message(
    target: models.Target, message: list[int]
) -> None:
    with pytest.raises(compiler.CompilationError, match='complete MIDI message'):
        compiler.compile_show(
            {'patches': {'bad': {'type': 'midi', 'output': 'din', 'bytes': message}}},
            {},
            [],
            target,
        )


@pytest.mark.parametrize(
    'value',
    [
        'clean',
        [],
        {'instrument': 'a', 'program': 1},
        {'type': 'other'},
        {'type': 'state', 'operation': 'brightness', 'value': True},
        {'type': 'state', 'operation': 'next-patch', 'value': 1},
    ],
)
def test_patch_types_are_explicit_and_fields_are_closed(value: object) -> None:
    with pytest.raises(ValueError):
        compiler.normalize_patch(value)


def test_reference_errors_are_aggregated_and_cycles_name_the_chain(
    target: models.Target,
) -> None:
    show = {
        'patches': {
            'a': {'type': 'reference', 'patch': 'b'},
            'b': {'type': 'reference', 'patch': 'a'},
            'missing': {'type': 'reference', 'patch': 'absent'},
            'channel': {
                'type': 'control-change',
                'output': 'din',
                'channel': 0,
                'controller': 1,
                'value': 1,
            },
        }
    }
    with pytest.raises(compiler.CompilationError) as failure:
        compiler.compile_show(show, {}, [], target)
    text = str(failure.value)
    assert 'patches.a -> patches.b -> patches.a' in text
    assert "'absent' does not exist" in text
    assert 'patches.channel' in text and 'channel' in text


def test_external_dependencies_use_stable_keys_and_detect_cross_document_cycles(
    target: models.Target,
) -> None:
    local = {'patches': {'a': {'type': 'reference', 'document': 'OTHER', 'patch': 'b'}}}
    other = {'patches': {'b': {'type': 'midi', 'output': 'din', 'bytes': [250]}}}
    assert compiler.compile_show(local, {}, [], target, documents={'other': other})
    other['patches']['b'] = {'type': 'reference', 'document': 'local', 'patch': 'a'}
    with pytest.raises(compiler.CompilationError, match='reference cycle'):
        compiler.compile_show(
            local, {}, [], target, documents={'local': local, 'other': other}
        )
    with pytest.raises(compiler.CompilationError, match='companion document'):
        compiler.compile_show(local, {}, [], target)


def test_persistent_switch_reserves_entire_raw_output_through_references(
    target: models.Target,
) -> None:
    show = {
        'patches': {
            'raw': {
                'type': 'sequence',
                'patches': [{'type': 'sysex', 'output': 'din', 'data': [125, 1]}],
            }
        },
        'switches': {
            'raw': {
                'button': 'A',
                'mode': 'momentary',
                'on': {'type': 'reference', 'patch': 'raw'},
                'off': {
                    'type': 'control-change',
                    'output': 'trs',
                    'channel': 1,
                    'controller': 1,
                    'value': 0,
                },
            },
            'other': {
                'button': 'B',
                'on': {
                    'type': 'control-change',
                    'output': 'din',
                    'channel': 16,
                    'controller': 100,
                    'value': 0,
                },
            },
        },
    }
    with pytest.raises(compiler.CompilationError, match='ownership conflicts'):
        compiler.compile_show(show, {}, [], target)


def test_momentary_and_latched_off_can_target_different_controls(
    target: models.Target,
) -> None:
    show = {
        'switches': {
            'test': {
                'button': 'A',
                'mode': 'momentary',
                'on': {
                    'type': 'control-change',
                    'output': 'din',
                    'channel': 1,
                    'controller': 64,
                    'value': 127,
                },
                'off': {
                    'type': 'pitch-bend',
                    'output': 'trs',
                    'channel': 3,
                    'value': 0,
                },
            }
        }
    }
    assert compiler.compile_show(show, {}, [], target)
    show['switches']['test']['off'] = {
        'type': 'sequence',
        'patches': [{'type': 'stock-firmware', 'exit_buttons': 'A+F'}],
    }
    with pytest.raises(compiler.CompilationError, match='only press assignments'):
        compiler.compile_show(show, {}, [], target)


def test_nrpn_compiles_as_one_ordered_group(target: models.Target) -> None:
    data = compiler.compile_show(
        {
            'patches': {
                'filter': {
                    'type': 'nrpn',
                    'output': 'din',
                    'channel': 3,
                    'parameter': 130,
                    'value': 388,
                }
            }
        },
        {},
        [],
        target,
    )
    offset = resolved.graph_offset(data)
    node = offset + struct.unpack_from('<I', data, offset + compiler.GRAPH.size)[0]
    assert data[node + 4 + 32 : node + 4 + 32 + 12] == bytes(
        [0xB2, 99, 1, 0xB2, 98, 2, 0xB2, 6, 3, 0xB2, 38, 4]
    )


def test_composite_inherits_child_categories_but_keeps_parent_label(
    target: models.Target,
) -> None:
    show = {
        'categories': {'sax': {}, 'whole-system': {}},
        'patches': {
            'child': {
                'type': 'control-change',
                'output': 'din',
                'channel': 1,
                'controller': 1,
                'value': 2,
                'categories': ['sax'],
            },
            'parent': {
                'type': 'sequence',
                'name': 'Parent',
                'categories': ['whole-system'],
                'patches': [{'type': 'reference', 'patch': 'child'}],
            },
        },
        'groups': {'Live': [{'type': 'reference', 'patch': 'parent'}]},
        'pages': {'patch': {'fields': ['display_name', 'categories']}},
    }
    data = compiler.compile_show(show, {}, [], target)
    assert b'Parent\0' in data and b'sax, whole-system\0' in data
    assert (
        compiler.compile_show(
            tomlkit.parse(resolved.export(data)).unwrap(), {}, [], target
        )
        == data
    )


def test_press_switches_can_share_controls(target: models.Target) -> None:
    value = {'type': 'midi', 'output': 'din', 'bytes': [250]}
    show = {
        'switches': {
            'a': {'button': 'A', 'on': value},
            'b': {'button': 'B', 'on': value},
        }
    }
    assert compiler.compile_show(show, {}, [], target)


def test_named_program_bank_lookup_reserves_the_emitted_bank_control(
    target: models.Target,
) -> None:
    show = {
        'instruments': {
            'piano': {
                'instrument': 'synth',
                'setup': 'factory',
                'output': 'din',
                'channel': 1,
            }
        },
        'definitions': {
            'setups': [
                {'instrument': 'synth', 'name': 'factory', 'banks': {'2': {'msb': 1}}}
            ],
            'programs': [
                {
                    'instrument': 'synth',
                    'setup': 'factory',
                    'program': 7,
                    'bank': 2,
                    'name': 'Clean',
                }
            ],
        },
        'switches': {
            'program': {
                'button': 'A',
                'mode': 'momentary',
                'on': {
                    'type': 'program-change',
                    'instrument': 'piano',
                    'program': '^Clean$',
                },
                'off': {'type': 'program-change', 'instrument': 'piano', 'program': 1},
            },
            'bank': {
                'button': 'B',
                'on': {
                    'type': 'control-change',
                    'output': 'din',
                    'channel': 1,
                    'controller': 0,
                    'value': 2,
                },
            },
        },
    }
    with pytest.raises(compiler.CompilationError, match='ownership conflicts'):
        compiler.compile_show(show, {}, [], target)


def test_external_terminal_validation_uses_the_source_context(
    target: models.Target,
) -> None:
    local = {
        'patches': {
            'borrowed': {'type': 'reference', 'document': 'other', 'patch': 'bad'}
        }
    }
    other = {
        'patches': {
            'bad': {
                'type': 'control-change',
                'output': 'unavailable',
                'channel': 1,
                'controller': 64,
                'value': 1,
            }
        }
    }
    with pytest.raises(
        compiler.CompilationError, match='other:patches.bad.output: unavailable output'
    ):
        compiler.compile_show(local, {}, [], target, documents={'other': other})


@pytest.mark.parametrize('reference_override', [False, True])
def test_edited_program_does_not_move_its_old_synthesizer_name(
    target: models.Target,
    reference_override: bool,
) -> None:
    source = compiler.read_document(Path('firmware/test/show.json'))
    definitions = compiler.read_document(Path('firmware/test/definitions.json'))
    assert isinstance(source, dict)
    if reference_override:
        source['patches'] = {'clean': source['groups']['wind'][0]}
        source['groups']['wind'][0] = {
            'type': 'reference',
            'patch': 'clean',
            'name': 'Special',
        }
    data = bytearray(compiler.compile_show(source, definitions, [], target))
    offset = resolved.graph_offset(data)
    _, _, count, _, _, _ = compiler.GRAPH.unpack_from(data, offset)
    root = struct.unpack_from('<H', data, offset + compiler.GRAPH.size + count * 4)[0]
    node = (
        offset
        + struct.unpack_from('<I', data, offset + compiler.GRAPH.size + root * 4)[0]
    )
    if reference_override:
        root = 0
        node = offset + struct.unpack_from('<I', data, offset + compiler.GRAPH.size)[0]
    _, _, _, _, instruments, groups, patches, _, _, _ = compiler.HEADER.unpack_from(
        data
    )
    cache = 76 + instruments * 68 + groups * 68
    meta = cache + patches * 82
    mappings = struct.unpack_from('<H', data, meta + 4)[0]
    bank = meta + compiler.METADATA.size + instruments * 64 + mappings * 8
    # These are the same authoritative graph/cache edits as selecting PC 2 and
    # clearing the bank in the pedal editor. The authored display label stays Flute.
    data[node + 6 : node + 9] = bytes((1, 255, 255))
    struct.pack_into('<I', data, node + 11, 0xFFFFFFFF)
    data[cache + 4 : cache + 7] = bytes((1, 255, 255))
    struct.pack_into('<I', data, bank, 0xFFFFFFFF)
    struct.pack_into('<I', data, 20, crc32(data[28:]))
    exported = tomlkit.parse(resolved.export(bytes(data))).unwrap()
    recompiled = compiler.compile_show(exported, {}, [], target)
    restored = tomlkit.parse(resolved.export(recompiled)).unwrap()
    recompiled_offset = resolved.graph_offset(recompiled)
    recompiled_node = (
        recompiled_offset
        + struct.unpack_from(
            '<I', recompiled, recompiled_offset + compiler.GRAPH.size + root * 4
        )[0]
    )
    assert (
        recompiled[recompiled_node + 4 : recompiled_node + 15]
        == data[node + 4 : node + 15]
    )
    assert recompiled[cache + 12 : cache + 76] == data[cache + 12 : cache + 76]
    assert restored['groups']['wind'][0]['display_name'] == (
        'Special' if reference_override else 'Flute'
    )
    if reference_override:
        assert restored['patches']['clean']['program'] == 2
    else:
        assert restored['groups']['wind'][0]['program'] == 2
    assert compiler.compile_show(restored, {}, [], target) == recompiled
