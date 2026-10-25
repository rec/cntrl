import struct
from pathlib import Path

import pytest
from pydantic import ValidationError

from cntrl.maker.morningstar.mc import boot_switch
from cntrl.maker.morningstar.mc.firmware import AddressRange, FirmwareImage, read_hex


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
    assert struct.unpack_from('<I', combined, 0x7C424)[0] == 0x60083001
    assert struct.unpack_from('<I', combined, 0x15F0)[0] == 0x20013DA0
    assert struct.unpack_from('<I', combined, 0x1990)[0] == 0x20013DA0
    path = tmp_path / 'combined.hex'
    path.write_text(boot_switch.encode_hex(combined))
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


@pytest.mark.parametrize('pair', ['A+A', 'AF', 'F+A+B', 'A+G', 'a+f'])
def test_switch_pair_requires_two_distinct_labels(pair: str) -> None:
    with pytest.raises(ValidationError, match='distinct switches'):
        boot_switch.Build(pair=pair)


def test_switch_pair_is_configurable() -> None:
    assert boot_switch.Build().pair == 'A+F'
    assert boot_switch.Build(pair='B+E').pair == 'B+E'
