# Stock-first switching to cntrl

The combined image starts Morningstar firmware normally. After releasing the
configured switches once, press the pair within 50 ms to restart into cntrl.
The default is A+F, in either order, with no hold. The window includes exactly
50 ms and measures presses after 15 ms of stable input.

The hook exists only in the stock image. Once cntrl starts, it uses its existing
button-reading code without the switching gesture. Returning to stock through
cntrl's UI is not implemented. A subsequent ordinary reset or power cycle
starts stock again; the switch request is consumed once, not persisted.

## Build

From the repository root, using the installed firmware toolchain:

```bash
make -C cntrl/maker/morningstar/mc/firmware boot-switch
```

Configure a different pair at build time:

```bash
make -C cntrl/maker/morningstar/mc/firmware boot-switch SWITCH_PAIR=B+E
```

The output is `../build/boot-switch/combined.hex`. Configuration requires a new
combined build and upload; it is not a performance-setup field or runtime setting.
Only the exact stock v3.14.0 HEX identified in the investigation is supported.
The builder rejects a different input hash or unexpected bytes at a patch site.
The original stock HEX is a local input and is not committed.

Upload through the running [upload helper](../upload-helper.md), placing the
combined image at its fixed `../build/patch-switcher.hex` path after preserving
the previous performance image. Request the exact combined image digest, then
check the pedal's USB identity. Stock reports `Morningstar MC6 Pro`; cntrl
reports `cntrl experimental` and responds to the normal `CNTRL STATUS` query.

## Input behavior

Only the configured pair is deferred inside stock. Its complete physical input
stream is delayed by 65 ms: the 15 ms stable-input interval plus the 50 ms pair
recognition window. Both edges, short taps and bounce retain their ordering and
duration for stock's existing button processing. Other switch bits pass through
unchanged. This avoids delivering the first switch's musical action before
recognizing the pair. It adds 65 ms to normal actions on these two switches.

If the second recognized press arrives too late, the switches retain their
ordinary stock behavior. A successful gesture starts USB disconnection and reset
immediately, with a 100 ms USB-disconnect interval before reset. That interval
is not an additional hold requirement. Actual time until cntrl is ready also
includes normal boot and USB reenumeration.

## Patch layout

Stock stays at `0x60000000`. cntrl is rebuilt at `0x60100000`; its startup,
button handling and other application sources are unchanged. A small flash
hook starts at `0x60083000`. The combined image preserves the known-good flash
configuration and remains below the reserved `0x607c0000` tail.

The composer changes six four-byte stock fields:

| File offset | Change |
| --- | --- |
| `0x18e0` | Empty early startup hook branches to the boot gate |
| `0x7c424` | Switch-reader vtable entry points to the wrapper |
| `0x15f0` | Cleared BSS end extends from `0x20013ca0` to `0x20013da0` |
| `0x1990` | MPU stack guard moves to the extended BSS end |
| `0x1024` | Boot data describes the complete combined image length |
| `0x1018` | CSF placeholder points to the final zero-filled trailer |

The wrapper calls the original stock reader at Thumb address `0x2649` and uses
stock's millisecond counter at `0x200130d8`. Its 156-byte zero-initialized state
occupies the newly reserved BSS, not guessed free heap or vendor EEPROM. Linker
assertions bound it to 256 bytes and the executable hook to one 4 KiB page.

On activation, the wrapper writes the one-shot marker `0x4354524c` to SRC_GPR5
and requests a system reset. The early boot gate clears only that exact marker
and enters cntrl's linked startup address; other values continue stock startup.
The core's distinct `0x0BAD00F1` crash marker is not interpreted as a switch.
No flash or SD writes occur when switching.

Updates and a return-to-stock UI are deliberately outside this implementation.
Stock's updater and external single-image uploads can replace the installation.

## Verification

Native sanitizer tests cover both press orders, the inclusive boundary, a late
pair, simultaneous presses, startup-held switches, short singles, bounce and
timer wraparound. Python tests check the configurable pair, exact patch sites,
stock-byte preservation, slot placement and HEX encoding/decoding. The combined
HEX also passes the existing upload helper's boot-envelope checks.

Initial installation on 2026-10-25 used SHA-256
`ba51a2aebb23fef350252bac763ab3379d8f6b00179f2604562d9c805dae3fd5`.
The running upload helper returned success, and the expected device enumerated
as `Morningstar MC6 Pro` afterward. The user then confirmed that A+F switched to
cntrl. USB changed to `cntrl experimental`, and its status was
`CNTRL READY 9 26866 36424cc1`, matching the pre-installation setup length and
checksum. The 226 Python tests, native target and portable firmware suites,
Ruff, formatting, type checking and pyupgrade checks passed. cntrl's existing
button-reading and application sources have no changes.

Physical MIDI silence during the switching gesture, exact boot duration, and a
cold power-cycle test were not independently measured in this deployment.
