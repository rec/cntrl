import struct
from fnmatch import fnmatchcase
from pathlib import Path
from re import findall

import pytest

from cntrl.firmware.intel_hex import AddressRange, FirmwareImage, encode_hex, read_hex
from cntrl.maker.morningstar.mc import boot_switch, toolchain


def test_diagnostic_linker_reserves_the_sdk_crash_record_only_when_enabled() -> None:
    original = (
        'FLASH (rx): ORIGIN = 0x60000000, LENGTH = 7936K\n'
        '.text.code : { *(.flashmem*) } > FLASH\n'
        '_heap_end = ORIGIN(RAM) + LENGTH(RAM);\n'
    )
    assert '_heap_end = ORIGIN(RAM) + LENGTH(RAM);' in boot_switch.linker_script(
        original
    )
    assert '_heap_end = ORIGIN(RAM) + LENGTH(RAM) - 128;' in boot_switch.linker_script(
        original, diagnostics=True
    )
    with pytest.raises(ValueError, match='heap boundary'):
        boot_switch.linker_script(
            original.replace('_heap_end', '_unexpected'), diagnostics=True
        )


@pytest.mark.parametrize('diagnostics', [False, True])
def test_build_enables_memory_qualification_only_when_requested(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, diagnostics: bool
) -> None:
    monkeypatch.setattr(toolchain, 'ensure', lambda root: tmp_path / 'arduino-cli')
    command = toolchain.compile_command(
        tmp_path,
        tmp_path,
        tmp_path / 'work',
        tmp_path / 'image.ld',
        diagnostics=diagnostics,
    )
    flags = next(v for v in command if v.startswith('build.flags.cpp='))
    assert ('-DCNTRL_MEMORY_QUALIFICATION' in flags) is diagnostics


def test_linker_places_only_cold_container_methods_in_flash() -> None:
    original = (
        'FLASH (rx): ORIGIN = 0x60000000, LENGTH = 7936K\n'
        '.text.code : { *(.flashmem*) } > FLASH\n'
        '.text.itcm : { *(.text*) } > ITCM\n'
    )
    linked = boot_switch.linker_script(original)
    assert 'FLASH (rx): ORIGIN = 0x60100000, LENGTH = 6912K' in linked
    flash, itcm = linked.split('.text.itcm')
    for section in (
        '_ZN5cntrl11CheckedListI*7reserveEj*',
        '_ZN5cntrl11CheckedListI*4trimEj*',
        '_ZN5cntrl11CheckedListI*7releaseEv*',
        '_ZN5cntrl12CheckedArrayI*7reserveEj*',
        '_ZN5cntrl4midi16reverse_sequenceI*',
        '_ZN5cntrl4midi7RoutingI*EC[12]E*',
        '_ZN5cntrl4midi7RoutingI*ED[12]Ev*',
        '_ZN5cntrl4midi18AdvancedProcessing5StateI*EC[12]E*',
        '_ZN5cntrl4midi18AdvancedProcessing5StateI*ED[12]Ev*',
    ):
        assert f'*(.text.{section})' in flash
    for name in (
        'lfo_status',
        'parameter_bindings',
        'validate_lfos',
        'bind_lfo',
        'service_lfos',
        'service_parameters',
        'numeric_fields',
        'reverse_group',
        'process_step',
        'service_generators',
        'finish_parameters',
    ):
        assert (
            f'*(.text._ZN5cntrl4midi18AdvancedProcessing{len(name)}{name}I*)' in flash
        )
    assert 'append' not in flash and 'erase' not in flash
    assert 'generated' not in flash and 'notify_release' not in flash
    assert '*(.text.startup._GLOBAL__sub_I__Z15switch_scan_duev)' in flash
    assert '*(.text*)' in itcm
    with pytest.raises(ValueError, match='linker script'):
        boot_switch.linker_script(original.replace('*(.flashmem*)', ''))


@pytest.mark.parametrize(
    ('suffix', 'in_flash'),
    [
        (
            '12build_ownersIZNS3_6decodeEPKhjNS0_7OutputsEjEUljE_EEbjT_',
            True,
        ),
        ('20prepare_output_stateEj', True),
        ('6Reader5matchERNS2_4StepE', True),
        ('7receiveEjNS0_6PacketEy', False),
        ('7enqueueENS3_5EventE', False),
        ('15prepare_releaseENS3_10ProcessingE', False),
        ('16prepare_pressureENS3_10ProcessingE', False),
    ],
)
def test_graph_configuration_placement_excludes_live_midi(
    suffix: str, in_flash: bool
) -> None:
    original = (
        'FLASH (rx): ORIGIN = 0x60000000, LENGTH = 7936K\n'
        '.text.code : { *(.flashmem*) } > FLASH\n'
        '.text.itcm : { *(.text*) } > ITCM\n'
    )
    flash = boot_switch.linker_script(original).split('.text.itcm')[0]
    patterns = findall(r'\*\((\.text\.[^)]*)\)', flash)
    # These are emitted ARM section names, including template substitutions.
    section = f'.text._ZN5cntrl4midi7RoutingINS0_18AdvancedProcessingEE{suffix}'
    assert any(fnmatchcase(section, p) for p in patterns) is in_flash


def test_switch_window_configuration() -> None:
    assert (
        boot_switch.Build.model_validate(
            {'switch_window': '0.02 seconds'}
        ).switch_window
        == 0.02
    )
    for v in (0, 0.113, 0.0011, True, '1 Hz'):
        with pytest.raises(ValueError):
            boot_switch.Build.model_validate({'switch_window': v})


@pytest.fixture
def images() -> tuple[FirmwareImage, FirmwareImage]:
    data = bytearray(0x83000)
    data[0x18E0:0x18E4] = bytes.fromhex('704700bf')
    for a, v in (
        (0x7C424, 0x2649),
        (0x15F0, 0x20013CA0),
        (0x1990, 0x20013CA0),
        (0x1024, 0x83000),
        (0x1018, 0x60082400),
    ):
        struct.pack_into('<I', data, a, v)
    stock = FirmwareImage(
        origin=0x60000000,
        entry=0x60001000,
        data=bytes(data),
        regions=[AddressRange(start=0x60000000, end=0x60083000)],
        record_counts={},
        sha256=boot_switch.STOCK_SHA256,
    )
    custom = FirmwareImage(
        origin=0x60100000,
        entry=0x60101000,
        data=bytes(0x3000),
        regions=[AddressRange(start=0x60100000, end=0x60103000)],
        record_counts={},
        sha256='0' * 64,
    )
    return stock, custom


def test_combined_image_preserves_stock_outside_the_patch_fields(
    images: tuple[FirmwareImage, FirmwareImage], tmp_path: Path
) -> None:
    stock, custom = images
    combined = boot_switch.combine(stock, custom, b'HOOK', 0x60083001)
    fields = (0x18E0, 0x7C424, 0x15F0, 0x1990, 0x1024, 0x1018)
    restored = bytearray(combined[: len(stock.data)])
    for a in fields:
        restored[a : a + 4] = stock.data[a : a + 4]
    assert restored == stock.data
    assert combined[0x100000:] == custom.data
    assert combined[0x83000:0x83004] == b'HOOK'
    assert struct.unpack_from('<2I', combined, 0x84000) == (0x434E4254, 0xBCB1BDAB)
    assert combined[0x84008:0x85000] == b'\xff' * (4096 - 8)
    assert struct.unpack_from('<I', combined, 0x7C424)[0] == 0x60083001
    assert struct.unpack_from('<I', combined, 0x15F0)[0] == 0x20013DA0
    assert struct.unpack_from('<I', combined, 0x1990)[0] == 0x20013DA0
    path = tmp_path / 'combined.hex'
    path.write_text(encode_hex(combined, 0x60000000, 0x60001000))
    decoded = read_hex(path)
    assert decoded.data == combined
    assert decoded.entry == 0x60001000


def test_unrecognized_stock_is_rejected(
    images: tuple[FirmwareImage, FirmwareImage],
) -> None:
    stock, custom = images
    with pytest.raises(ValueError, match='does not match'):
        boot_switch.combine(
            stock.model_copy(update={'sha256': '0' * 64}), custom, b'HOOK', 0x60083001
        )


def test_changed_patch_site_is_rejected(
    images: tuple[FirmwareImage, FirmwareImage],
) -> None:
    stock, custom = images
    data = bytearray(stock.data)
    data[0x18E0] ^= 1
    with pytest.raises(ValueError, match='patch bytes'):
        boot_switch.combine(
            stock.model_copy(update={'data': bytes(data)}), custom, b'HOOK', 0x60083001
        )


@pytest.mark.parametrize('origin', [0x60000000, 0x607C0000])
def test_custom_image_cannot_overlap_stock_or_the_reserved_tail(
    images: tuple[FirmwareImage, FirmwareImage], origin: int
) -> None:
    stock, custom = images
    with pytest.raises(ValueError, match='layout'):
        boot_switch.combine(
            stock, custom.model_copy(update={'origin': origin}), b'HOOK', 0x60083001
        )
