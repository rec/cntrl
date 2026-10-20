# MC6 Pro firmware updating and recovery

## Conclusion

**Ordinary broken application firmware has a documented recovery route through
the PROGRAM button and USB uploader. Recovery without opening this particular
unit is not yet established. Do not flash an experimental image yet.**

The supplied application implements software entry into the Teensy bootloader
and can retain that entry during some crashes. Neither mechanism survives every
possible broken replacement image. The application-managed SD update is not an
independent rescue loader or an automatic rollback system.

This report is software-only analysis, dated 2026-10-20. No uploader executable
was run; no reset request, firmware transfer, flash operation or fuse operation
was sent to a device. Read-only macOS USB inventory checks did not establish a
live connection: System Profiler reported access errors and IORegistry's text
listing showed no matching Morningstar/Teensy entry. This does not establish that
the pedal is physically absent or defective.

Address conventions and the application image hash are in
[the pin map](mc6-pro-pin-map.md#address-conventions-and-evidence).
Unqualified instruction addresses below are runtime ITCM addresses.

## Evidence and limits

The inspected application is
`2026-10-16_MC6_PRO_Firmware_v_3_14_0_261016501_0.hex`.
The photographed PCB is `MC6 PRO v1.2`, dated `2024-10-22`.

The vendor's
[Firmware Uploader v1.1.3 Windows installer](https://github.com/Morningstar-Engineering/Firmware-Uploader/releases/tag/v1.1.3)
was downloaded and unpacked as an archive, not installed or executed. Its
Electron application and bundled macOS `tycmd` were inspected statically.
The Windows package includes that macOS executable alongside Windows/Linux
variants; the separate macOS DMG was not inspected.

| Artifact | SHA-256 |
| --- | --- |
| Windows installer, 45,879,661 bytes | `c8014dbde315f9c43a4c9ade3e6d25c5889efabccb4f8a258c5bdebf6cdb9c88` |
| `resources/app.asar` | `5bb8231217aec1f5f7f0b6b47ab935e73f6f6747f49846498db51496f8bc7537` |
| Bundled macOS `resources/extraResources/tycmd` | `ff266e8d549d1aeb648059c01cb42e34025b43ac396475734bf80aee9477fce2` |

The bundled executable contains version string `0.9.0-9-g058715b`. Its comparison
source is TyTools commit `058715ba55fcf662e053b9f0d29772a39c67ce62`.
Matching version text is not proof the vendor binary has no local modifications.
Vendor application findings below come from its packaged JavaScript; TyTools
source findings are identified separately. Third-party binaries and firmware
are not committed to this repository.

## 1. Desktop uploader: independent bootloader transport

The packaged renderer is `dist/main.156b8dab5806004dc4fb.js` inside the ASAR.
Its `uploadFirmware` method launches:

```text
tycmd upload <selected-file> --board @<selected-device-location>
```

Its `isFileValid` method checks the `hex` filename extension. Device monitoring
uses `tycmd list --verbose --output json --watch`. The application does not
implement its own MC6 Pro flash protocol here: it delegates to TyTools.

The matching public
[TyTools class implementation](https://github.com/Koromix/tytools/blob/058715ba55fcf662e053b9f0d29772a39c67ce62/src/libty/class_teensy.c)
recognizes Teensy 4.1 images by their FlexSPI header and configured 8 MiB flash
size. Its HalfKay configuration permits addresses from `0x60000000` up to the
exclusive limit `0x607c0000`, using 1,024-byte payload blocks. This is a generic
board-format check, not proof of MC6 Pro wiring compatibility or functional code.
The public HEX parser also checks record checksums. No Morningstar signing-key
verification appears in the inspected GUI upload method or these source paths.

In the matching
[upload orchestration](https://github.com/Koromix/tytools/blob/058715ba55fcf662e053b9f0d29772a39c67ce62/src/libty/board.c),
TyTools requests bootloader entry if the running board cannot yet accept an
upload. If that fails, it asks for a manual button press. After uploading it
normally sends a command to run the application. It does not demonstrate an
automatic return to the old image when the new application fails.

**Inference:** a correctly built independent Teensy-compatible RT1062 HEX is
plausibly accepted through this route. This is not confirmed acceptance by the
specific unit's closed bootloader, nor proof of its security-fuse state.

### Software bootloader entry in the supplied application

The USB device ISR is `0x4eb9c`, installed at IRQ 113. Its CDC control path
accepts a seven-byte line-coding request at `0x4efc0`. Completion processing
at `0x4f10a` through `0x4f158` recognizes a requested baud rate of **134** and
sets a bootloader-entry countdown. USB start-of-frame processing at `0x4ecec`
through `0x4ecfa` decrements it; expiry branches to `0x4f1c4` and calls
trampoline `0x64e90` at `0x4f1dc`.

That trampoline reaches flash function **`0x60002004`**. It reads OCOTP CFG5
bit 1 at `0x401f4460`. With that bit clear it executes **`BKPT #251`** at
`0x6000200e`, matching Teensy's normal bootloader-entry hook. The other branch
disables interrupts and USB, changes the RAM mapping, and invokes the NXP ROM
entry with argument `0xeb120000`. A conditional branch in a file does not tell
us which fuse value exists in the physical processor.

This matches the public
[Teensy USB implementation](https://github.com/PaulStoffregen/cores/blob/master/teensy4/usb.c)
and TyTools' serial bootloader request. It explains how normal USB updating can
work without pressing the internal button. The 134 value is a control request,
not the physical MIDI baud rate.

The crucial limitation is that this USB handler is **part of the replaceable
application**. A bad clock setup, broken vector table, disabled interrupts,
uninitialized USB or overwritten handler can make the request ineffective.
Power cycling alone does not restore the handler or the previous application.

### Some crashes retain software recovery

The application installs default exception handler `0x4e73c`. It captures fault
information, switches to a PIT-based timing loop, and directly calls the USB ISR
at `0x4e81e`. After 80 approximately 100 ms intervals it resets the processor.
This matches the public
[Teensy fault handler](https://github.com/PaulStoffregen/cores/blob/master/teensy4/startup.c).

Thus some faults leave an approximately eight-second window in which the
bootloader request can still be serviced. This is useful, but conditional:
USB must already be initialized, its state and supporting memory must remain
usable, and execution must reach this handler. Early boot failures, damaged
exception state, nested faults and arbitrary CPU lockups are not covered.
Keeping a fault handler in our eventual implementation would help; it would not
replace a crash-independent recovery mechanism.

## 2. Application-managed SD update

This route receives a firmware file, stores it as `firmware/mc6pro.hex`, and
updates from that file after the user confirms with switch A. It depends on
working application code, its filesystem, display and switch handling.

| Function or location | Established behavior |
| --- | --- |
| `0x22d64`, called at `0x2f24a` | Receive/update workflow using the SD file |
| `0x2fc3a` onward | Display received-file and confirmation prompts |
| `0x2fd00` through `0x2fd0e` | Wait for switch result; A accepts, others cancel |
| `0x2ff2c` | Call SD updater `0x1b180` after confirmation |
| `0x425ac` | Find available staging space in external flash |
| `0x1b19e` through `0x1b1b4` | Open `firmware/mc6pro.hex` |
| `0x1b21e` | Invoke HEX parser/stager `0x428b8` |
| `0x427e0` | Parse HEX line and verify its byte checksum |
| `0x42758` | Process HEX record type and track address extent/EOF |
| `0x42670` | Write staged data in four-byte flash units |
| `0x429ac` | Call RAM-resident replacement routine `0x264` |
| `0x264` | Erase/write application flash, clean remaining staging area, reset |

The strings and implementation match a customized
[FlasherX](https://github.com/joepasquariello/FlasherX) update strategy, but the
binary must be treated as authoritative rather than assuming every upstream
validation is present.

### Flash layout and why staging is not rollback

| Region | Role established by this image |
| --- | --- |
| `0x60000000` through `0x60082fff` | Supplied application image, including startup and USB-entry helper |
| Above occupied flash, rounded up to 4 KiB, below `0x607c0000` | Candidate staging area |
| `0x607c0000` through `0x607fffff` | Excluded from this updater's normal staging/cleanup; also used by flash-backed EEPROM emulation |

Allocator `0x425ac` scans downward from `0x607bfffc` for non-erased data,
then rounds the buffer start upward to a 4 KiB boundary. The buffer's actual
location depends on the unit's existing flash contents, which are not included
in the supplied HEX. If the rest of this region is erased after this image,
the candidate interval would start at `0x60083000` and contain `0x73d000`
bytes. That is a conditional calculation, not a flash dump from the pedal.

Replacement routine `0x264` copies from that staging area to **`0x60000000`**,
erasing nonblank 4 KiB sectors as needed and writing four bytes at a time.
It then erases remaining sectors below `0x607c0000` and resets via SCB_AIRCR.
The old startup code, USB-entry helper and application are overwritten.

There is no preserved old-image slot or boot-success/rollback decision in this
traced path. Loss of power during replacement can leave an incomplete boot
image. RAM-resident flash-writing code makes self-update possible; it is not
a separately protected bootloader available after a power cycle.

The excluded upper 256 KiB must not be described as a verified Morningstar
recovery image. Its contents were not supplied. PJRC's own bootloader designs
can store a restore program at the flash tail, but that does not identify the
contents of this board's flash or promise restoration of Morningstar firmware.

### What the checks do, and do not, establish

The staged parser checks HEX syntax/checksums and record types. A checksum is
neither a signature nor proof that the code boots. No cryptographic image check,
post-boot health check or automatic rollback was found between this parser and
the replacement call. The upstream maximum-address/target-ID checks must not
be assumed: they are not present as such in this traced parser-to-copy path.
Do not use it for malformed-image experiments.

The separate **`Invalid Signature...`** UI is not an image-signature check:
`0x313d8` calls `0x1526c`, which compares ten fixed bytes from `0x200004b0`
against EEPROM addresses `0x46` through `0x4f`, using flash emulation and the
external EEPROM. Failure enters the UI at `0x31060`. It is a stored identifier
check, not authentication of the selected firmware file. No identifier values
need to be copied into our independent hardware driver.

The supplied HEX is readable and its reserved HAB CSF area is zero-filled, as
recorded in [the original analysis](firmware-analysis.md). That is evidence
about this file, not a readout of the unit's fuses or its separate bootloader.

## 3. Physical recovery and the enclosure question

Morningstar's current
[recovery instructions](https://help.morningstar.io/en/article/firmware-upload-failure-controller-does-not-boot-up-1b7h2fe/)
say to remove the backplate and use the board's button to enter the bootloader.
They also describe holding the button while connecting USB if a short press
does not work. This is the documented rescue for an unresponsive application.

An older indexed
[MC6 Pro manual](https://manuals.morningstar.io/mc-midi-controller/mc6-pro-user-manual)
instead describes an external access point near the 3.5 mm sockets. The newer
instructions and the supplied photo of **S1, labelled PROGRAM**, inside the
v1.2 unit are more relevant evidence here. The photos do not establish whether
there is a usable external access hole. Do not promise enclosure-closed recovery
based on the older manual. An exterior-only visual check could resolve the
access question without removing the backplate.

[PJRC documents a separate bootloader chip](https://www.pjrc.com/store/ic_mkl02_t4.html)
for custom RT1062 boards. That architecture would explain a PROGRAM button
working even when the application is broken. The firmware, uploader and vendor
recovery instructions strongly support a Teensy-style recovery arrangement;
the supplied application file does not contain or authenticate a dump of that
separate chip. Its exact model, firmware and electrical connection remain
unverified on this PCB.

Do not generalize PJRC's long-button factory-restore procedure to this pedal.
Its effects are not established here. Also do not change OCOTP fuses, security
state, debug routing or flash protection to investigate recovery. Such changes
can defeat the very recovery path we need to retain.
[PJRC distinguishes ordinary application failures from irreversible security
configuration mistakes](https://www.pjrc.com/teensy/td_code_security.html).

## Recovery decision table

| Failure | Enclosure-closed recovery supported by current evidence? |
| --- | --- |
| Bad preset creates an execution loop | Stock safe mode may help; switch B is read by the application at `0x3281a` and sets flag `0x20013390` |
| Main application loop hangs, but USB interrupts still work | USB bootloader request can still work |
| Fault reaches the intact stock fault handler after USB starts | USB request may work during its approximately eight-second polling window |
| Replacement never initializes USB, corrupts startup or disables interrupt service | No independent software-only entry has been established; documented PROGRAM-button recovery is the fallback |
| Update loses power after application erase begins | Same physical fallback is documented; there is no proven automatic rollback |
| Security/debug fuses or physical circuitry are damaged | Ordinary firmware recovery cannot be promised |

Safe mode is not a ROM boot menu. It is reached well into startup and cannot
rescue arbitrary broken replacement startup code. No independent SD rescue
loader or automatic selection of an old image was established in this image.

## Next steps without opening or flashing

The next useful device check would be a **non-flashing bootloader round trip**:
observe the running USB identity, request bootloader mode, observe its identity,
then issue only the bootloader's run/reset command to resume the unchanged
application. This was not attempted here. It changes execution state but does
not intentionally program flash. Stop first if any uploader is configured to
upload automatically on detecting a bootloader.

The matching TyTools CLI separates these operations:

```text
tycmd list --verbose --output json
tycmd reset --bootloader --board @<verified-location>
tycmd list --verbose --output json
tycmd reset --board @<verified-location>
```

These are a future test sequence, not commands to run blindly. The actual
executable, target location and absence of an auto-uploading process must be
established first. Successful entry from healthy firmware verifies a convenient
path, **not** the ability to recover from firmware that cannot service USB.

Before any first experimental flash, the remaining gate is an independently
available recovery entry for this unit. If no external PROGRAM access exists,
an absolute requirement never to open the enclosure conflicts with guaranteed
recovery from arbitrary bad startup code. Software analysis cannot remove that
constraint.

Once that gate is resolved, the smallest sensible first implementation would
retain standard USB bootloader entry and fault handling, perform no fuse writes,
leave configurable-port/relay outputs untouched, and only report over USB.
Preserve the original vendor HEX and controller backups outside the pedal.
This is a proposed progression, not an implemented or validated test image.

The inspected HalfKay host path supplies flash-upload and reset operations,
not a demonstrated RAM-only application loader. Likewise, placing a file on SD
does not make it execute independently of a working loader. A resident recovery
stub could improve later development, but installing one is itself a flash
operation and would be a separate architecture decision, not a way around this
first recovery gate.
