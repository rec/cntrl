# Resident stock firmware and cntrl: investigation, 2026-10-25

The subsequent [stock-first implementation](firmware/boot_switch/README.md)
provides a configurable 50 ms switch to cntrl. This document retains the original
investigation and its qualification scope.
The later [flash usage audit](flash-usage-audit.md) identifies a candidate
boot-choice sector and distinguishes normal-operation use from updater erasure.

## Result

A small stock startup patch can hand control to cntrl linked at a higher flash
address. This was built, uploaded and verified on the MC6 Pro. The uploaded
image contained both stock firmware and relocated cntrl. The original cntrl
firmware was restored afterward, with the selected setup and its complete
downloaded bytes unchanged.

This establishes the key startup mechanism, not a completed dual-boot product.
The experiment always selected cntrl. It did not run stock's normal application,
implement a selection menu or return gesture, measure switching time, or qualify
MIDI/USB behavior across a switch. Retention of both images after a cold power
cycle and runtime readback of their flash contents were not tested.

## Scope and evidence

The stock input was
`2026-10-16_MC6_PRO_Firmware_v_3_14_0_261016501_0.hex`, SHA-256
`228d73e768fad383bc205c8fc1ef52f3a49de08c754aca11d4a069ca1aab9fc8`.
Offsets and patch candidates below apply only to that exact image. The target
is the previously identified MC6 PRO v1.2 board, serial `14468280`.

The investigation used the repository's HEX decoder, reconstructed stock ITCM
bytes, ARM disassembly, the installed Teensy 1.62.0 linker/startup sources, a
relocated build of the current cntrl firmware, and the existing upload helper.
No dependency or production source changes were needed.

Disposable binaries, assembly and the experimental linker script are in the
ignored `firmware/build/boot-selector-analysis/` directory beside this document.
They are evidence fixtures, not supported build or installation tools.

## Flash layout

The stock flash configuration declares 8 MiB. The existing linker and stock
updater reserve the upper 256 KiB. This is configured capacity, not a new chip
identification or an independent physical-capacity measurement.

| Address interval, end exclusive | Experimental use |
| --- | --- |
| `0x60000000..0x60083000` | Stock image at its original addresses |
| `0x60083000..0x60083008` | Eight-byte handoff fixture |
| `0x60083008..0x60100000` | Filled with `0xff` in the upload image |
| `0x60100000..0x60160c00` | Relocated cntrl, 396,288 bytes |
| `0x60160c00..0x607c0000` | Outside this upload image; not surveyed |
| `0x607c0000..0x60800000` | Reserved tail, excluded from the upload |

These are experimental placements, not a committed partition scheme or a promise
that the unsurveyed space is unused by every manufacturer feature.

Stock's runtime ITCM and initialized DTCM mappings remain unchanged:

- ITCM `0x00000000..0x000650e8` comes from file offset `0x148f4`.
- DTCM `0x20000000..0x200086c0` comes from file offset `0x79b60`.

The images share RAM by running one at a time after reset. They do not run
concurrently or preserve each other's live state.

## The four-byte startup hook

The ROM boot IVT's program entry remains stock's `0x60001429`. This is an
i.MX RT boot IVT, not a Cortex-M exception table containing an initial stack
pointer and reset handler in its first two words.

Stock startup configures FlexRAM and its stack, then calls `0x600018e0` from
`0x60001448`. The called function is exactly:

```asm
0x600018e0: bx lr
0x600018e2: nop
```

The experiment replaces those four bytes with:

```asm
0x600018e0: b.w 0x60083000
```

The assembled bytes are `81 f0 8e bb`. The appended fixture is:

```asm
0x60083000: ldr r0, [pc, #0]
0x60083002: bx r0
0x60083004: .word 0x601015e5
```

`0x601015e5` is this relocated cntrl build's Thumb startup entry. The fixture
does not use a stack or initialize peripherals. cntrl then performs its own
FlexRAM, stack, RAM, interrupt, clock and USB initialization.

A real selector at this hook could return through the original `lr` to continue
stock startup, or branch to cntrl. Returning to stock is an instruction-level
candidate, not a hardware-tested path from this investigation. Selector code
must execute from flash before either application's RAM initialization.

In addition to the startup hook, two four-byte boot-envelope fields were updated:

- Offset `0x1024`: combined image length, `0x160c00`.
- Offset `0x1018`: CSF placeholder pointer, `0x60160000`.

That is three four-byte patch fields in the original stock range. Eight bytes
actually differ because some bytes in the two metadata fields are unchanged.
The original 512-byte flash configuration, original reset entry, and complete
stock ITCM payload were compared and retained. The final 3,072-byte CSF
placeholder remains zero, satisfying the existing upload helper's envelope
checks. Updating that pointer is packaging, not adding a signature.

## Relocating cntrl

An experimental copy of `imxrt1062_t41.ld` changed only the FLASH region from
`ORIGIN = 0x60000000, LENGTH = 7936K` to
`ORIGIN = 0x60100000, LENGTH = 6912K`. The existing application was compiled with
that linker script through Arduino CLI's `build.flags.ld` property. The other
compile flags matched the normal patch-switcher build.

The relocated IVT points to `0x601015e5`, and its own metadata and flash load
addresses are linked in the higher region. The core's `BootData[0]` still
contains the hard-coded `0x60000000`. Therefore this fixture is entered through
the patched stock startup; it is not presented as an independently ROM-bootable
image at the higher origin. A supported build should express and check that
distinction explicitly.

The current MPU configuration covers a 16 MiB flash region starting at
`0x60000000`, which includes both placements. Both applications initialize their
own RAM and interrupt state; there is no need to relocate stock's RAM code or
patch its many absolute references merely to keep cntrl resident elsewhere.

## Candidate return path from stock

The existing shift-register reader is at ITCM `0x2648`, Thumb pointer `0x2649`.
Its single matching pointer in the stock image is at file offset `0x7c424`,
initialized DTCM address `0x200028c4`. The reader constructor installs vtable
address `0x200028bc`; this pointer is the read entry at vtable offset eight.
Stock button logic calls the corresponding virtual reader method.

This offers a narrow candidate patch: replace that pointer with a flash wrapper
which invokes the original reader, observes a deliberately reserved gesture,
then requests a reset into cntrl. It avoids overwriting the reader's prologue or
relocating its PC-relative instructions. A direct branch from ITCM to the flash
wrapper would exceed a Thumb `b.w` instruction's range, whereas the virtual
function pointer can name the full flash address.

This wrapper was not implemented or tested. It still needs correct ABI handling,
gesture consumption before stock musical actions, a release gate across boot,
and a safe reset sequence. A hold alone does not prevent stock from acting on
the initial presses. The gesture must be designed around the stock event path,
not merely detected after its ordinary effects.

The reset request also needs an audited handover location. The core already
uses `SRC_GPR5` for crash reboot state; it must not simply be repurposed. A
one-shot request surviving software reset and a preference surviving complete
power loss are separate requirements. No EEPROM location, persistent selector
record, or default selection policy was allocated by this investigation.

## Update behavior is the main product constraint

Stock's SD replacement routine at ITCM `0x264` writes the new application at
`0x60000000`, then erases remaining nonblank sectors below `0x607c0000` before
reset. It would erase relocated cntrl and the selector. Its staging allocator
at `0x425ac` also assumes the existing single-application layout.

Consequently, an ordinary stock SD update cannot remain enabled unchanged.
There is a small candidate way to refuse it: updater entry `0x1b180` has
ordinary returning failure paths with result zero. Replacing its first four
bytes with `movs r0, #0; bx lr` would refuse that entry before staging or erasing.
This was not applied or tested. The caller's prompts and every other possible
flash-writing path still need auditing; silently appearing to accept an update
is not a finished user experience.

External HalfKay uploads are independent of these stock patches. An ordinary
single-image upload must be treated as replacing the dual-image installation.
For an initial implementation, a combined installation/update image is simpler
than supporting independent field updates to both slots. This would be an
explicit change to the current build and deployment architecture.

The reserved tail remains outside the proposed application placements. Its
EEPROM and other contents were not backed up or inspected by this experiment,
and stock startup's complete persistent-write behavior has not been audited.
Preserving cntrl's setup download does not establish preservation of every
manufacturer setting.

## Hardware experiment and restoration

Before flashing, the pedal reported `CNTRL READY 9 26866 36424cc1`, selected
`pman`. Its sole setup was downloaded, checked against the catalogue length and
checksum, and retained as `before-pman.bin` in the ignored experiment directory.
The current application HEX was backed up separately.

The combined HEX was decoded again and passed the existing upload helper's
boot-envelope checks. Its length was 1,444,864 bytes. SHA-256:

`133ce401bfe9c89f5f39d4a8a98dddc225c9f88ba323f1b9065579517c458ef7`.

Software bootloader entry and the expected HalfKay identity were verified before
upload. The uploader reported reset/disappearance with exit status 1, as in
previous working uploads. Application USB subsequently reported
`CNTRL READY 9 26866 36424cc1`; selection was `pman`, and all 26,866 downloaded
setup bytes matched the pre-experiment backup. Since the low image was stock
and cntrl existed only in the higher region, this confirms the relocated
application started through the patched hook.

The original cntrl HEX was then restored. SHA-256:

`39f4c72d434e57791e1436a8ab53180228dba945a02ed872eb7087c1acaf2a1d`.

The first restore attempt failed with a USB write error and left the expected
pedal in HalfKay. A second explicitly requested upload reached reset/disappearance.
The final USB check again returned `CNTRL READY 9 26866 36424cc1`, selected
`pman`, and a byte-for-byte matching setup download. The fixed build upload path
also contains the original HEX again. There is no active selector installation.
Flash outside the restored image was not read back, so this does not assert
whether experiment bytes remain there.

## Recommendation and remaining gates

Proceed with an in-place stock startup hook and relocated cntrl rather than
attempting to relocate the manufacturer's binary. A small selector can dispatch
before either application initializes; stock's virtual switch reader provides
a plausible return hook. This avoids reflashing when changing songs.

Before treating this as an installation suitable for other users:

1. Run the patched stock continuation path and verify its normal operation.
2. Implement and test image selection and reset handover in both directions,
   including cold boot, held-button release, invalid images and interrupted
   selection writes if persistence is included.
3. Audit and constrain stock flash writes and firmware updates; verify vendor
   settings survive repeated switching and installation/restoration.
4. Build a reproducible, exact-stock-hash-checked combined image, with explicit
   slot limits and checks that neither image can overlap another or the tail.
5. Measure button-to-ready switching time and USB reenumeration. Check startup
   MIDI, pending messages, stuck-note behavior and host application reconnection.
6. Verify recovery and update behavior for the supported hardware/stock versions.

Those gates are implementation work beyond this investigation. The live proof
does not require another architectural feasibility experiment before that work
can be designed, but does not remove its qualification requirements.

## Additional work beyond the prompt

None.
