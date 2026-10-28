# Persistent firmware selection

The combined image starts cntrl patch mode on a fresh installation or when no
valid choice is stored. Explicit choices persist across resets and power cycles.

From patch mode, open A+D, select **Original firmware**, then press C on its page
to start stock, or F to go back. The module commits and verifies the stock choice
in the existing selection journal, disconnects USB and resets. It sends no MIDI.
While stock is running, release the configured pair once, then press it within
50 ms (either order, no hold) to commit patch mode and reset back into cntrl.
The window includes exactly 50 ms and measures presses after 15 ms of stable input.
Function patches supply the exit pair per visit. The standalone module uses A+F.

The gesture hook exists only in stock. cntrl's button reader has no A+F gesture.
The Original firmware module is target-specific and can be excluded with
`ORIGINAL_FIRMWARE=0` when building. Installing a combined image reinitializes
the journal to the patch-mode default; update preservation remains outside scope.

## Build

From the repository root, using the installed firmware toolchain:

```bash
make -C cntrl/maker/morningstar/mc/firmware boot-switch
```

The output is `../build/boot-switch/combined.hex`. Function patches configure the
exit pair at runtime through their `exit_buttons` argument.
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
ordinary stock behavior. A successful gesture commits and verifies the boot
choice, then starts USB disconnection and reset, with a 100 ms USB-disconnect
interval before reset. That interval
is not an additional hold requirement. Actual time until cntrl is ready also
includes normal boot and USB reenumeration.

## Patch layout

Stock stays at `0x60000000`. cntrl is rebuilt at `0x60100000`; its startup,
button handling and other application sources are unchanged. A small flash
hook starts at `0x60083000`. The combined image preserves the known-good flash
configuration and remains below the reserved `0x607c0000` tail. The linker and
composer reserve `0x60084000..0x60085000` for the boot choice. Neither application
nor the executable hook overlaps this sector.

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
stock's millisecond counter at `0x200130d8`. Its zero-initialized state
occupies the newly reserved BSS, not guessed free heap or vendor EEPROM. Linker
assertions bound it to 256 bytes and the executable hook to one 4 KiB page.

The early boot gate reads the selection sector using flash-only code and a
stack already initialized by stock startup. It continues stock or enters
cntrl's linked startup according to the last valid record. It does not erase,
program flash, initialize SD or use the old one-shot SRC_GPR5 marker.

## Selection record

The sector starts with the 32-bit ownership marker `0x434e4254` and its bitwise
inverse. The combined image installs that header followed by erased (`0xff`)
space. The wrapper checks this ownership header before any runtime programming;
it never erases or programs an unrecognized sector.

The remaining 511 slots are eight-byte records: a 32-bit choice and its bitwise
inverse. Stock is `0x53544b31`, cntrl is `0x43545231`. Empty, unknown and torn
records do not change the last valid choice. No valid choice defaults to cntrl patch mode.
New records go after the last occupied slot, including interrupted writes.

The stock wrapper programs its record from a RAM stack buffer through the
audited ITCM-resident flash writer at Thumb address `0x4da91`, then reads back
the selection before resetting. The writer waits for flash completion and
invalidates the destination data cache. An already selected choice is not
written again. SD and manufacturer EEPROM are untouched.

There is no runtime sector erase or compaction. If the header is invalid, the
journal is full, or readback fails, the gesture is consumed until release and
the pedal stays in stock. It does not switch with an uncommitted choice. The
previous valid record survives an interrupted append. Reinitializing the
journal is currently a firmware installation operation, not a normal switch.

The Original firmware module uses cntrl's ITCM-resident EEPROM flash writer to
append a stock record, without touching the manufacturer's EEPROM region. It
refuses to switch while setup storage is busy, edits are unsaved or performance
MIDI sends are queued. It also reserves all records for the stock return and its consumption;
there is no automatic journal erase or compaction. Failure leaves patch mode
running and displays an error.

Update handling remains outside this implementation.
Stock's updater and external single-image uploads can replace the installation.

## Verification

Native sanitizer tests cover both press orders, the inclusive boundary, a late
pair, simultaneous presses, startup-held switches, short singles, bounce and
timer wraparound. Python tests check the configurable pair, exact patch sites,
stock-byte preservation, slot placement and HEX encoding/decoding. The combined
HEX also passes the existing upload helper's boot-envelope checks.
Selection tests cover the ownership header, interrupted bit programming,
last-valid-record ordering, torn/unknown records and a full journal. The stock
gesture test also checks that cancelling a failed save cannot replay its keys.

Initial installation on 2026-10-25 used SHA-256
`ba51a2aebb23fef350252bac763ab3379d8f6b00179f2604562d9c805dae3fd5`.
The running upload helper returned success, and the expected device enumerated
as `Morningstar MC6 Pro` afterward. The user then confirmed that A+F switched to
cntrl. USB changed to `cntrl experimental`, and its status was
`CNTRL READY 9 26866 36424cc1`, matching the pre-installation setup length and
checksum. The 226 Python tests, native target and portable firmware suites,
Ruff, formatting, type checking and pyupgrade checks passed. cntrl's existing
button-reading and application sources have no changes.

That initial deployment predates persistent selection. Its physical MIDI
silence and exact boot duration were not independently measured.

The persistent-selection image was installed on 2026-10-25, SHA-256
`cafb8c8ce9f5f59c62c89aebb3a3d239f85a9cd2f42d7a7d14751655baccf516`.
After one USB write error, the helper retry succeeded and the pedal enumerated
as `Morningstar MC6 Pro`, matching a fresh stock selection. The 226 Python tests,
native target tests (including the new selection tests), Ruff, formatting, type
checking and pyupgrade passed. The user confirmed that A+F entered cntrl and
that, after disconnecting all power and reconnecting, cntrl started directly
without another gesture. USB then returned `CNTRL READY 9 26866 36424cc1`,
matching the setup length and checksum before this installation. Interrupted
record writes were tested in simulation, not by removing power during an actual
flash program operation.

## Function visits (format 15)

The journal keeps its eight-byte value/complement records. Config records use
`0x46430000 | pair | song_flag`: pair is an A..F logical six-bit mask with exactly
two bits set, and song_flag is `0x8000` for song continuation. The config slot is
the visit token. `0x46520000 | token` marks a stock return;
`0x46580000 | token` consumes it. Unknown or torn records remain ignored. Config
records reset the preceding visit state. The default pair is A+F.

Entry reserves five slots: config, stock choice, return marker, patch-mode choice
and consumed marker. The stock hook translates logical A..F to the traced GPIO
bits and retains its existing release-to-arm, 15 ms debounce and 50 ms/no-hold
behavior. No patch-mode A+F handler is installed.

For song visits, `/cntrl/firmware-visit.bin` stores a verified small record with
setup identity/checksum, song library generation/checksum and occurrence IDs.
The following referenced selection also carries its source document checksum.
Write, sync, read-back, journal commit and return handling run as separate poll
steps. No collection or full runtime snapshot is written. The old record is
irrelevant unless a matching unconsumed stock-return token exists.

Return waits for button release, validates the saved context, durably consumes
the marker, then activates the next patch. Power loss after consumption can skip
a recall but cannot replay it on the next boot. A module visit never resumes a
song. Flash programming uses the existing ITCM writers; no sector erase or
journal compaction occurs at runtime.

The format-15 combined image builds successfully. Python and native tests cover
function dispatch, song continuation, stale library detection, torn markers,
consumption and journal capacity. It has not been flashed: the user requested
finishing the branch without flashing after concurrent pedal work was detected.
The image is in this worktree's `build/boot-switch/combined.hex`. The build prints
its SHA-256; rebasing onto subsequent firmware fixes changes it.
