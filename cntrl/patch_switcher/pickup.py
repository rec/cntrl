"""Pickup policy for live MIDI controls in compiled setup documents."""

import struct


def pack(
    global_policy: bool | None, pairing_ms: int, policies: list[bool | None]
) -> bytes:
    if global_policy is None and pairing_ms == 2 and all(p is None for p in policies):
        return b''
    return HEADER.pack(
        b'CPUP', len(policies), encode(global_policy), pairing_ms
    ) + bytes(encode(p) for p in policies)


def encode(policy: bool | None) -> int:
    return 0 if policy is None else 2 if policy else 1


def section_end(data: bytes, offset: int) -> int:
    if data[offset : offset + 4] != b'CPUP':
        return offset
    if offset + HEADER.size > len(data):
        raise ValueError('truncated pickup metadata')
    _, count, policy, _ = HEADER.unpack_from(data, offset)
    end = offset + HEADER.size + count
    if (
        end > len(data)
        or policy > 2
        or any(p > 2 for p in data[offset + HEADER.size : end])
    ):
        raise ValueError('invalid pickup metadata')
    return end


HEADER = struct.Struct('<4sHBH')
