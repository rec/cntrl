# Application flash usage audit, 2026-10-25

The subsequent [persistent boot selection](firmware/boot_switch/README.md)
implementation reserves the candidate sector. This report records the audit
that preceded that implementation.

## Finding

For the exact stock firmware and current combined image below, the traced
normal-operation flash writers do not reach `0x60084000..0x60085000` (end
exclusive). This 4 KiB sector is a suitable candidate for an explicit boot-choice
reservation in our combined layout, with firmware updates excluded from scope.

It is not an unconditionally unused or protected sector. Stock's updater can
erase it. No sector has been reserved by this audit, no persistence code has
been added, and the pedal's actual contents at that address were not read.

## Inputs and scope

- Stock file: `2026-10-16_MC6_PRO_Firmware_v_3_14_0_261016501_0.hex`.
- Stock HEX SHA-256:
  `228d73e768fad383bc205c8fc1ef52f3a49de08c754aca11d4a069ca1aab9fc8`.
- Combined HEX SHA-256:
  `ba51a2aebb23fef350252bac763ab3379d8f6b00179f2604562d9c805dae3fd5`.
- Stock ITCM: file offset `0x148f4`, runtime `0x00000000..0x000650e8`.
- Stock initialized DTCM: file offset `0x79b60`, runtime
  `0x20000000..0x200086c0`.

Hashes and HEX checksums were checked again during this audit. Instructions were
examined at their RAM execution addresses, not just their flash storage offsets.
The pinned Teensy 1.62.0 core sources were used to identify familiar routines;
the stock instructions determine the reported addresses and ranges.

This was a host-side static audit. It did not flash, reset, read or write the
pedal, invoke an EEPROM test, or change running firmware. The existing upload
helper performs uploads, not arbitrary flash readback, so it cannot provide a
sector-content dump for this audit.

## Observed application layout

| Region, end exclusive | Established use |
| --- | --- |
| `0x60000000..0x60083000` | Supplied stock image and its combined-image patches |
| `0x60083000..0x60084000` | Page allocated to our boot gate and reader wrapper |
| `0x60084000..0x60100000` | Current combined builder fills this gap with `0xff` |
| `0x60100000..0x60160c00` | Current relocated cntrl image |
| `0x60160c00..0x607c0000` | Outside the combined image; contents not surveyed |
| `0x607c0000..0x607ff000` | Stock flash-backed EEPROM emulation, 63 sectors |
| `0x607ff000..0x60800000` | Final sector; ownership not established here |

Both the proposed sector and the entire gap were checked byte-for-byte as
`0xff` in the decoded combined HEX. This describes the build artifact, not a
live flash dump or proof that another firmware will preserve it.

The hardware's configured geometry is 8 MiB, with 4 KiB sectors. Exact flash
chip identification and physical geometry measurement remain outside this audit.

## Low-level application flash writers

The stock application contains these identified FlexSPI1 routines:

| ITCM entry | Function | Direct call or tail-call sites |
| --- | --- | --- |
| `0x4da90` | Program bytes using LUT command `0x32` | `0x2b8`, `0x426c4`, `0x4dce2`, `0x4dd06` |
| `0x4db74` | Erase a 4 KiB sector using command `0x20` | `0x2a4`, `0x308`, `0x42650`, `0x4dcbe` |
| `0x4d964` | Poll status (`0x05`), reset FlexSPI AHB state, reenable interrupts | `0x4db36`, `0x4dc00` |

The programming routine masks addresses to 24 bits. The erase routine masks to
24 bits and sector alignment. Neither routine itself restricts the operation
to EEPROM or staging space; the caller supplies that restriction.

Direct callers classify into two paths: EEPROM emulation and firmware update.
No stored Thumb function pointers to these three entries were found in the
stock image. No additional normal-operation caller was identified.

## EEPROM: bounded away from the candidate

Stock EEPROM routines are initializer `0x4d9c8`, byte reader `0x4da10`, and byte
writer `0x4dc1c`. Their instructions establish:

- Logical address limit `0x10bb` inclusive, 4,284 logical bytes.
- Sector index `(logical_address >> 2) % 63`.
- Sector address `0x607c0000 + sector_index * 4096`.
- Append writes of two-byte records; on compaction, erase and rewrite the same
  sector. Calls are `0x4dd06` for appending, `0x4dcbe` for erase, and `0x4dce2`
  for compacted records.

Enumerating all accepted logical addresses reaches exactly sectors 0 through
62, or `0x607c0000..0x607ff000`. Initializer `0x4d9c8` scans those same 63
sectors. It does not establish ownership of the final `0x607ff000` sector.

Normal startup also explicitly selects the external I2C EEPROM backend:
constructor `0x3aa28` is called at `0x31424`, and `0x3143a..0x31442` sets the
wrapper's backend flags. The external read/write path uses I2C address `0x54`.
See the [EEPROM trace](mc6-pro-pin-map.md#eeprom-confirmed-external-path-and-flash-emulation-alternative).
Even when the flash-emulation alternative is reached, its bounded addresses
remain far above the proposed boot-choice sector.

The external EEPROM's free logical addresses and actual chip capacity were not
audited as alternative storage. This report does not recommend using them.

## Firmware update: reaches the candidate

The existing update path is confirmed by the direct caller chain:

1. SD updater `0x1b180` calls staging allocator `0x425ac` at `0x1b188`.
2. Allocator scans downward from `0x607bfffc` (then `0x607bfff8`), finds the
   highest occupied data and chooses the following 4 KiB-aligned staging start.
   Its upper boundary is `0x607c0000`.
3. HEX processing `0x428b8` calls staged writer `0x42670` at `0x4295a`.
   The latter calls low-level programming at `0x426c4`.
4. At `0x429ac`, replacement routine `0x264` is called with destination
   `0x60000000`. It programs the replacement via `0x2b8`, erasing sectors as
   needed through `0x2a4`.
5. Its cleanup loop calls sector erase at `0x308` and continues through offsets
   at or below `0x7bffff`, stopping before `0x607c0000`.
6. Abort/buffer cleanup goes through `0x4265c` to `0x42624`; its sector erase
   call is `0x42650`.

Therefore stock firmware replacement can erase the proposed sector, just as it
can erase our hook and cntrl slot. With the current combined image intact, its
high occupied cntrl region places ordinary staging above the low candidate;
the replacement cleanup still removes the candidate. This is not a persistent
reservation that survives stock updates.

No change to updater behavior was made, following the current scope. External
HalfKay programming and vendor recovery remain outside the application audit
and must also be treated as capable of replacing the selection storage.

## Additional paths checked

The decoded stock image was scanned on halfword boundaries for constants in
FlexSPI1's `0x402a8000..0x402a8400` register range, and the corresponding code
was inspected. The identified references were:

- Flash startup literal `0x60001c68`: used for reads of FlexSPI1 configuration
  at `0x60001a80`, `0x60001ab2`, and `0x60001ac4` while configuring FlexSPI2.
  The resulting writes target FlexSPI2 at `0x402a4000`, not application flash.
- ITCM literals `0x4d9b8`, `0x4d9c4`, `0x4db60`, `0x4db70`, and `0x4dc04`:
  the status/program/erase routines above.

The ITCM disassembly was also searched for MOVW/MOVT construction of the
FlexSPI base halves and for the expected IP-command sequence constants. No
additional application-flash writer was identified by those checks.

LUT commands in the flash configuration block include program and erase
operations, but having those entries does not execute them. The located
runtime program/erase code rewrites and triggers LUT sequence 15; no separate
chip-erase caller was identified.

Strings and references associated with formatting were checked: the located
format code uses block-device operations, sector sizes of 512 bytes and FAT
metadata (including the FAT FSInfo signatures). It is not an additional direct
FlexSPI program/erase path. No LittleFS/SPIFFS signatures or additional
low-level flash-driver callers were found. Absence of strings is supporting
evidence only, not proof that every filesystem backend has been excluded.

For cntrl, the current linked ELF contains `eeprom_initialize` but no linked
`eepromemu_flash_write`, sector-erase routine, EEPROM byte writer or LittleFS
implementation. Target setup, selection and splash storage use SD. The stock
switch hook writes only its RAM state and reset/USB registers. Neither current
application provides a normal-operation flash writer into the proposed gap.

## What this permits, and what remains unproved

This evidence supports explicitly allocating the candidate sector in a later
persistence implementation for this exact combined build. That implementation
must bound both image slots and the hook away from the reservation, provide a
power-loss-tolerant record, and verify the actual destination before its first
erase/write. Merely choosing an address in currently erased space is insufficient.

The audit is not a formal whole-program proof. Computed addresses, unexpected
control flow, undocumented ROM/bootloader behavior and other stock versions
cannot be excluded by literal and direct-call analysis alone. No live flash
contents, stock-state write traces or power-interruption tests were collected.
No sector should be called globally unused or protected on this evidence.

The final reserved sector and the EEPROM region should not be used merely
because an erased or apparently unused portion might be observed there.

## Additional work beyond the prompt

None.
