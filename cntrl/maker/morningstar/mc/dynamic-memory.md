# Morningstar dynamic-memory inventory

Supporting hardware-specific audit for
[the shared plan](../../../../plan/dynamic-memory.md).
Original source baseline: `a32af63`, 2026-11-12. The original inventory is retained
below; implementation checkpoints are recorded separately. Physical qualification
is incomplete. Paths below are relative to this directory.

## Controller allocation implementation

The initial controller change removes the three statically reserved extra
controllers and acquires exactly the extra instances required by the candidate
layout. Allocation and construction precede setup handoff. A failed preparation
keeps the active controllers. Cancelling preparation releases candidate storage.

An isolated source export of baseline commit `e33722d`, built with the same
installed toolchain, produces the following linker totals.

| Region | Baseline | Demand-sized extra controllers |
| --- | ---: | ---: |
| RAM1 variables | 319904 bytes | 319936 bytes |
| RAM1 available for local variables | 7776 bytes | 7744 bytes |
| RAM2 static variables | 118912 bytes | 112736 bytes |
| RAM2 available for allocation | 405376 bytes | 411552 bytes |

A one-controller setup acquires no extra controller heap storage, recovering
6176 bytes of RAM2 with a 32-byte increase in RAM1 variables. Multiple controllers
charge their actual allocation to `memory_used`. Replacement can temporarily
hold both old and candidate allocations. Runtime allocator overhead and stack
high-water still require device measurement.

Reproduce the baseline by exporting `git archive e33722d` into a generated
directory, using the installed pinned SDK and stock image, and invoking
`python -m cntrl.maker.morningstar.mc.build_target` from that exported tree with
its own source root and output directory. Run the same build command from the
working tree for the candidate and compare their exported `memory.txt` files.
The source export, build logs, binaries and memory reports under `build/` are
disposable generated artifacts. The original sources and build procedure remain
tracked.

The next change replaces the twelve invocation-navigation reservations with
checked nodes held only by live executors. The target linker reports 94176
bytes of static RAM2 and 430112 bytes available for allocation. RAM1 variables
are 319968 bytes, with 7712 bytes left for local variables. This recovers another
18560 bytes of static RAM2 relative to the controller-only build, including
section alignment. Each live snapshot's actual node bytes are charged to the
shared memory ledger. Cancellation and completion reclaim the node.

## Integrated implementation checkpoint

The integrated target build passes with the same pinned toolchain:

| Region | Original baseline | Integrated candidate |
| --- | ---: | ---: |
| RAM1 variables | 319904 bytes | 309728 bytes |
| RAM1 available for local variables | 7776 bytes | 17952 bytes |
| RAM2 static variables | 118912 bytes | 90752 bytes |
| RAM2 available for allocation | 405376 bytes | 433536 bytes |

This recovers 28160 bytes of static RAM2. Local-variable availability is a linker
total, not a measured stack high-water mark. Cold preparation/reclamation/UI code
uses FLASH placement; hot insertion/iteration/parsing/ISR paths retain their
normal placement. Final timing qualification must use this placement.

Configuration arrays use encoded counts, while admitted performance records use
checked stable nodes. Candidate controllers, fonts, stream graphs and
arpeggiator resources are prepared before publication. Active/undo/candidate
looper roles remain separate, but disabled/empty generations reserve no event
buffers. Explicit arm estimates are reported separately from idle allocations.

The target routes the reentrant SDK allocation APIs and checked application
allocations into one bounded RAM2 arena. Two-level bitmaps find a fitting size
class without scanning its list; adjacent free blocks coalesce immediately.
The bitmap dimensions follow the 32-bit size domain, rather than feature counts.
Allocator metadata operations have a fixed bound; clearing and copying remain
proportional to byte count and happen outside the interrupt-protected metadata
section. The first allocation initializes the arena, including global constructors.
There is no separate SDK heap or fallback to newlib's allocator.

`MemoryBuffer` charges actual arena chunks on this target and retains the old
allocation until a complete replacement is admitted. Native tests and host
estimates use the documented alignment/header model, independently of the native
operating system's allocator. SDK bootstrap use is subtracted from the available
budget; later SDK growth is reported separately. The tracked
`firmware/patch_switcher/heap_trace.cpp` tracks checked ownership in each chunk,
including reallocations and frees from a different execution context. Its totals
and validity must be checked on the physical target.

`CNTRL HEAP` is an explicit idle-only diagnostic. It reports actual checked/SDK
bytes, SDK growth, exact largest free block (`largest`), the size-class request
bound (`allocatable`), free block count, conservative peak,
allocation failures, allocator cycle maxima and interrupt allocation count.
It validates the physical block chain only when requested, outside MIDI service.
Ordinary status uses bounded counters and bitmaps. Size-class rounding can refuse
a request even when a larger physical block exists; `allocatable` gives a
conservative request that fits without list searching. `boot_checked` captures
checked allocations immediately before setup storage loads; it is separate from
setup-dependent allocations and does not establish the final runtime allowance.
The SDK high-water locator reports `sdk_peak`, `sdk_request`, `sdk_caller` and
`sdk_phase`. Resolve the caller using the image's exported ELF; a forwarding
libc helper may be the recorded caller. Phases identify startup (0), setup
storage (1), document activation (2), songs (3) and normal service (4).
Linked
allocation fault injection is available to qualification code through
`heap_fault_after`; normal firmware leaves it disabled.

`CNTRL MIDI STATUS N` adds a maximum `service_gap_us` across calls, including
intervening display/storage work. `CNTRL MIDI RESET` resets timing counters.
The status also identifies Bridge/stream input cable quarantine masks and the
generated pedal route quarantine. After an unseen key cannot record a refused
strike, reconnect/reset the source before accepting new onsets; accepted releases
continue. Heap recovery alone cannot safely identify an untracked old note-off.

The tracked native stress test is `firmware/test/test_dynamic_memory_stress.cpp`.
An optimized host run with a 430112-byte modeled budget accepted all traffic in
the 24/32-route cases. Denser 48/64-route bursts exhausted that budget and
refused new work while preserving the accepted seed's releases. A 24-point
allocation fault sweep completed without stranded voices, pending events or
recovery work. Rebuild with the usual native test flags for sanitizers, or
`-O2` without sanitizers for comparative timings. Logs and binaries in `build/`
are disposable outputs; the complete harness and native suite entry are tracked.

The original native results do not establish pedal timing. The bounded allocator
has independent sanitizer coverage for fragmentation, contents, alignment,
coalescing, failed realloc and metadata-operation counts. Its physical timing,
payload-copy costs and the final stack/heap allowance remain qualification gates.
The user chose preparation across service cycles while preserving MIDI order;
earlier preparation through a release may delay it under overload. Physical
qualification remains in progress.

## Continuation implementation checkpoint

Graph preparation, chronological insertion and blocked-output scans now yield
between node visits. Prepared deliveries and cleanup run before more graph work;
causal barriers retain MIDI order across inputs. Independent ingress keeps
`1 + routes + stream references + echo repeats` traversal nodes available in
addition to each admitted note/pedal's terminal credit. Publishing an event
transfers its stable node instead of allocating or copying another record.

Focused sanitizer regressions cover sustained SysEx publication without starvation,
two-output pedal release with allocations failing, rejection of old queued output
work after reconnect, and finite file reversal whose preparation finishes later
than the source. Nested-payload tests verify allocation-free node transfer and
exactly-once destruction. The full native suite and 1360 Python tests passed
locally with non-editable dependencies; physical timing remains unqualified.

The checked ARM layouts are Route 184 bytes, output bookkeeping 32 bytes and a
processing node 144 bytes. Compiler estimates include those sizes and the
graph-derived protected workspace. Both normal and diagnostic builds pass:

| Region | Normal continuation | Diagnostic continuation |
| --- | ---: | ---: |
| RAM1 variables | 308416 bytes | 312608 bytes |
| RAM1 available for local variables | 19264 bytes | 15072 bytes |
| RAM2 static variables | 90752 bytes | 90752 bytes |
| RAM2 available for allocation | 433536 bytes | 433536 bytes |

The first link crossed another 32 KiB instruction-RAM bank. Existing cold-code
placement now covers storage inventory/native asset commands, font staging and
validation/transfer commands, history import and Expression layout initialization.
Queue insertion, minimum-index operations and interrupt handlers retain their
normal placement. Normal instruction code uses 195376 bytes; the diagnostic
uses 196296 bytes, retaining six instruction-memory banks. These totals are not
measured stack high-water values or a proof of a wall-clock service bound.

Reproduce with `uv run --frozen --no-editable cntrl build --target-dir
cntrl/maker/morningstar/mc --source-root . --output build/continuation-target`,
adding `--diagnostics` for the diagnostic image. Retain its exported ELF with
the image. Source changes are tracked; generated images and reports are outputs.
Do not overwrite the working normal recovery image during qualification.

## Normal-firmware MIDI failure, 2026-11-13

Before installing either continuation image, the bounded short-message probe
reproduced a failure on the working normal/newlib Expression image, SHA-256
`e48514e4a0d0f7bf12bd18ed5103863e222c50b717b0413e488890085441cc83`.
The original setup, history and readable assets were backed up to
`build/midi-loss-baseline-backup-20261113/` before selecting the tracked temporary
`qualification/dynamic-memory-thru.toml` setup. The two previously unreadable,
unreferenced movie files remained untouched.

The virtual-source framing check passed. The first physical, paced trial expected
304 bytes: 128 Program Changes followed by 16 Control Changes. It received only
the first 120 Program Changes, 240 bytes ending in `c777`; the entire remaining
64-byte suffix was absent. An empty SysEx (`f0f7`) was captured separately during
the idle interval. Burst and long-SysEx trials did not run. The following serial
diagnostic timed out, so post-trial route counters were unavailable and the loss
cannot yet be localized to USB input, DIN transmission/receive or USB return.
The raw trial was saved before that request in
`build/midi-loss-baseline-probe-20261113.toml`.

During the failure, macOS still enumerated both the application USB device and
its serial port with the same USB session ID, `679780132959`. A subsequent
read-only inspection opened no CoreMIDI client, received no serial response and
stalled while closing the port. The process sample in
`build/midi-loss-baseline-inspect-close.sample.txt` places its main thread in
Python's `os_close`, then macOS `close`; that process eventually exited and
released the port. Its final report is
`build/midi-loss-baseline-after-20261113.toml`. Enumeration alone does not prove
that the firmware loop or USB endpoints were progressing. This reproduction
establishes that the communication failure also occurs on the previous allocator
and firmware, but does not establish its cause. Neither continuation image was
installed for this trial.

The user confirmed that the Status page's `Up:` timer kept advancing during the
communication failure. The main loop was therefore still servicing the display;
this occurrence was not a complete firmware freeze. USB endpoint progress and
the location of the missing messages remain unmeasured.

After reconnecting the hub, the normal `simple-setup` selection was restored and
the temporary qualification setup was deleted. On 2026-11-14, recovery verification
through `cntrl setup verify --preserve-memory-estimates` confirmed both setups,
all 15 history revisions, backed-up assets, songs and selection against the
pre-test backup. The reported status was `CNTRL READY 41 25477 b32ccca8 starts=89
starts_status=ready`. The firmware image was unchanged.

Use the existing `cntrl setup restore` and `cntrl setup verify` commands with
`--preserve-memory-estimates` when returning a current-format backup to the same
firmware build. This keeps the recorded accounting in setups, history and song
sources, avoiding a dependency on an older host CLI when the current compiler's
RAM estimates have changed. The default upgrade path still refreshes estimates.

## USB MIDI flush overlap, 2026-11-14

The pinned Teensy SDK calls `usb_midi_flush_output()` from its USB SOF interrupt.
The board also called it from the main loop. The SDK flush updates the shared
transfer descriptor, transmit head and available-byte count without excluding
that interrupt. An interrupt during the main-loop flush can therefore enter the
same update. This is a concrete overlap in the software path, but has not yet
been established as the cause of the physical communication failures.

The board now masks the USB1 interrupt around its main-loop flush and restores
the previous mask afterward. Masking only the global interrupt flag would not
hold across SDK helpers, which can unconditionally re-enable global interrupts.
Other interrupts remain available during the flush.

`firmware/test_usb_midi_flush.cpp` compiles the production board driver with SDK
mocks that inject a pending SOF inside the flush. The unguarded implementation
fails with `USB SOF interrupted the main-loop MIDI flush`; the guarded version
passes, including initially masked IRQs and an unconfigured USB device. This
test establishes interrupt exclusion at the SDK call boundary, not physical DMA
or host-driver behavior. The adapter's nine native test programs pass with
ASan/UBSan, and the Python suite passes all 1425 tests.

For an isolated physical comparison, the working normal/newlib source export
was copied to `build/usb-flush-control-source/` and only the guard's changes to
`target.cpp`, `usb_forward_device.cpp` and `usb_forward_device.h` were applied.
The existing pinned SDK was reused. Build through the application:

```sh
uv run --frozen --no-editable cntrl build --target-dir build/usb-flush-control-source/cntrl/maker/morningstar/mc --source-root build/usb-flush-control-source --output build/usb-flush-control-target
```

The resulting image SHA-256 is
`5131ec97592d393d2eaa06e7ef4690a52c9ec2f9d42951e448936a18c53fce03`.
Its linker totals are 306880 bytes of RAM1 variables, 195248 bytes of ITCM code,
20800 bytes available for local variables, 90752 bytes of static RAM2 and
433536 bytes available for allocation. Stack high-water remains unmeasured.
This was the pre-flash checkpoint. Physical comparison must use a fresh backup and the
`cntrl` flash, qualification and restore commands; keep the existing working
image as the rollback.

## Control USB comparison, 2026-11-14

The approved comparison used `cntrl flash --skip-build` with the control source
root and target directory above. Its fresh backup is
`build/usb-flush-control-target/before/`: both setups, all 15 history revisions,
readable assets, songs and the `simple-setup` selection. The two previously
unreadable, unreferenced movie files were left untouched.

The first upload returned a USB write error while in HalfKay, before any MIDI
test. A deliberate retry through `cntrl upload-helper ... upload` reached the
reset command, then returned status 1 when the board disappeared. Application
USB returned with startup count 90. The uploader did not attest successful
installation, so this comparison cannot prove that the guard caused a change
in behavior. The expected control image hash remains `5131ec97592d393d2eaa06e7ef4690a52c9ec2f9d42951e448936a18c53fce03`.
Restore and verification through the current `cntrl setup` commands with
`--preserve-memory-estimates` passed before testing.

With the temporary forwarding setup selected, the captured results were:

| Trial | Sent | Received | Observation |
| --- | ---: | ---: | --- |
| MIDI-only paced short messages | 304 | 306 | Exact payload preceded by `f0f7`; strict mismatch. |
| Serial plus MIDI, ordinary short-message check | 304 | 306 | Same extra prefix; stopped before notes. |
| Serial plus MIDI, diagnostic paced trial | 304 | 304 | Exact match. |
| Serial plus MIDI, diagnostic burst trial | 304 | 304 | Exact match. |
| Serial plus MIDI, diagnostic SysEx trial | 4099 | 4092 | Missing bytes; strict mismatch. |

The MIDI-only report is `build/usb-flush-control-midi-only-20261114.toml`;
the ordinary combined report is `build/usb-flush-control-combined-20261114.toml`;
the diagnostic trial report is
`build/usb-flush-control-combined-probe-20261114.toml`. The virtual-source framing
check passed where exercised. No bytes were filtered from these reports.

`cntrl qualify --analyze-report` aligns the SysEx capture with five missing bytes
`68696a6b6c` at sent offset 746 and two missing bytes `7778` at sent offset 3833.
The first 746 bytes and final 264 bytes match exactly. This payload repeats every
128 data bytes, so alignment positions can be ambiguous; these are byte-stream
comparisons, not proof of where transport loss occurred.

During this SysEx trial, route 0's drop counter rose from zero to one while its
received and transmitted counters remained equal. In this target, route 0 is
the DIN **output**, and route 6 is the USB-device output. The forwarding counters
are charged to destinations, not to the input that supplied an event. The result
is consistent with a USB packet rejected before the DIN output queue, rather
than establishing a DIN receiver overflow. The actual rejected USB packet and
UART timing were not captured by this normal build, so not all seven missing
bytes have been localized. No allocation failure was reported.

Serial diagnostics stayed reachable after these trials, and the startup count
remained 90. The earlier USB stall was not reproduced in these bounded checks;
the data path is nevertheless not qualified because the extra SysEx and missing
bytes remain unresolved. `simple-setup` was reselected, the temporary setup was
deleted, and restore/verification against the fresh backup passed again with
`CNTRL READY 41 25477 b32ccca8 starts=90 starts_status=ready`.

The qualifier was then extended with independent trial selection and read-only
capture analysis. All 1499 tests passed after integrating main's test-runner and
emulator changes; lint, formatting, typing and pyupgrade checks also passed.
A second fresh backup, `build/usb-flush-targeted-backup-20261114/`, preceded the
remaining tests. No additional firmware upload was performed.

| Independent trial | Sent | Received | Observation |
| --- | ---: | ---: | --- |
| MIDI-only notes | 396 | 398 | Exact 396-byte payload after an extra `f0f7`; strict mismatch. |
| Serial plus MIDI notes | 396 | 396 | Exact match, including all 64 note-ons, 64 note-offs and sustain changes. |
| MIDI-only SysEx | 4099 | 4094 | Extra empty SysEx before the payload; payload length seven bytes short. |
| Serial plus MIDI SysEx | 4099 | 4094 | Five bytes short; strict mismatch. |

These captures are `build/usb-flush-targeted-{midi-only,serial}-{notes,sysex}-20261114.toml`.
The MIDI-only note capture has an exact 396-byte common suffix: no note or sustain
message was missing. The last serial SysEx capture aligns with byte `6c` missing
at sent offset 750 and bytes `6768696a` missing at offset 1641, using zero-based
offsets. Its first 750 bytes and final 2454 bytes match exactly. The repeating
payload still limits interpretation of alignment positions. In particular, the
generic alignment of the MIDI-only SysEx capture is not a minimal edit proof and
must not be interpreted as the number of inserted or deleted wire bytes.

Route 0's drop counter increased once during each independent SysEx trial, while
route 6 reported no drop and both routes' received/transmitted totals matched.
The rejected USB event could be separate from the payload gaps; its CIN and
contents need capturing before attributing those gaps to validation. UART
overrun/drop evidence is also still needed. Loss with no active serial client
shows that host serial activity is not necessary for this failure; the composite
USB serial interface was present in both modes.

No MIDI trial caused a USB disappearance or reboot. Serial diagnostics stayed
reachable with startup count 90 and no allocation failure. The temporary setup
was deleted again, and the normal setup was restored and verified against the
second backup. The original rollback image remains at
`build/expression-dirty-normal-target/firmware.hex`. The transport is not yet
qualified: the unexpected empty SysEx, long-message loss and uploader failure
remain open, and bounded-allocator/continuation hardware stress has not resumed.

## Instrumented transport control, 2026-11-14

The tracked [diagnostic control](diagnostic_control/README.md) reconstructs the
working normal/newlib firmware with its corrected layout, cached Expression page
and USB flush guard. It adds byte counts and CRCs at raw USB input, accepted USB
input, DIN UART writes, DIN UART reads and USB return submissions. It retains the
first rejected USB event with parser state and stream offsets, and measures UART
software drops, hardware overrun latches and the receive polling gap at the first
observed loss. Recording performs no allocation or serial logging; the watchdog
remains disabled and trace collection is inactive at boot.

`cntrl qualify ... --trace-build EXPORT` checks the local image hash and compiled
recipe ID, resets the trace, runs a bounded trial and saves the final trace even
when the MIDI bytes mismatch. The MIDI-only mode closes serial before opening MIDI
and opens serial again only after MIDI closes. Raw MIDI evidence has a separate
report so a later USB timeout cannot erase it. Multiple rejected events are
counted, but only the first event's details are retained.

This instrumentation is needed to separate validation rejections from loss between
successful DIN writes and UART reads. The earlier route counters alone do not
establish which boundary loses SysEx payload bytes.

The local export `build/sysex-transport-control-main/` has trace recipe ID
`f63a369f265cee31` and image SHA-256
`fa8b7b21c201165abb180c4512d9a1b46ed1f21b4354a7dc593681e98931acdf`.
Its linked application sections include 195,456 bytes of ITCM code, 31,424 bytes
of `.data`, 275,456 bytes of `.bss`, and 90,752 bytes of RAM2 `.bss.dma`.
These are static section sizes, not runtime free-space or stack high-water
measurements. The adjacent generated source/SDK directory is retained so the
existing flash flow uses the matching compiler for backup and restoration.

## Physical qualification procedure

Use `uv run --frozen --no-editable cntrl qualify --target-dir cntrl/maker/morningstar/mc`
to discover the MIDI endpoints. Target options follow `--`. The DIN output must
connect to the DIN input for the forwarding checks below.

To isolate an active USB serial client from MIDI traffic, first select an
already-enabled forwarding setup, close the setup command, then run:

```sh
cntrl qualify --target-dir cntrl/maker/morningstar/mc -- --run --expect-enabled --probe-short-messages --midi-only --destination 'cntrl experimental DIN' --source 'cntrl experimental DIN' --report build/midi-only-probe.toml
```

This uses the same virtual-source check and paced/burst/SysEx/note payloads as the
combined probe, but opens no serial connection and records no serial counters.
It saves the observed bytes and stops at the first mismatch. The firmware's
serial interface remains part of its USB descriptor; this isolates the host
serial client, not the presence of the composite interface. Inspect serial
diagnostics separately after the MIDI client has closed.

Use `--probe-trial paced`, `burst`, `sysex` or `notes` with
`--probe-short-messages` to run one trial independently. This makes it possible
to collect a note-lifecycle or SysEx capture when an earlier trial would stop
the sequence. Notes include 64 onsets, their 64 releases and sustain on/off on
two channels. Unexpected bytes still fail the strict comparison.

Analyse saved probe trials without opening MIDI or serial connections:

```sh
cntrl qualify --target-dir cntrl/maker/morningstar/mc -- --analyze-report build/usb-flush-control-combined-probe-20261114.toml --report build/usb-flush-control-combined-analysis-20261114.toml
```

The original capture is read only. The analysis reports exact lengths and common
prefix/suffix sizes, plus an alignment of inserted, deleted or replaced spans.
Repeated payloads can have several valid alignments. The result does not infer
a transport stage or turn an unexpected byte stream into a passing result.

For a crash or hang investigation, build with `cntrl build --diagnostics` using
the same target and output options. This opt-in image reserves 128 bytes of RAM2
for the SDK fault record and six retained execution breadcrumbs. Its exported
`firmware.elf` must stay with the image for address decoding. Normal builds omit
these hooks and keep their existing reset behavior.

`cntrl qualify --target-dir cntrl/maker/morningstar/mc -- --inspect --report
build/inspection.toml` reads serial counters and retained crash evidence without
opening MIDI endpoints or arming the watchdog. A `--run` on a diagnostic image
arms an eight-second watchdog and records execution phase, packet, heap operation
and loop progress. It stops recording afterwards; the watchdog remains enabled
and is fed only when the cooperative loop completes. After a failure, keep power
connected, allow the warm restart, and use `--inspect` before another run clears
the retained record. A watchdog reset gives breadcrumbs rather than a fault PC.
Instrumentation changes timing, so use a normal image for final performance
measurements.

Before relying on the watchdog, run the explicit idle self-test:

```bash
uv run --frozen cntrl qualify --target-dir cntrl/maker/morningstar/mc -- --watchdog-test --report build/watchdog-selftest.toml
```

This acknowledges a deliberate hang, releases the serial session without waiting
for another target command, and requires the next startup count, the watchdog
reset cause, retained phase 21 and no fault PC. `QUAL STATUS` also reports the
watchdog, clock and reset-route registers. A masked reset route is rejected;
the diagnostic does not change it. `QUAL HANG` starts only after its reply has
been copied to the USB driver, and keeps interrupts enabled while withholding
the cooperative-loop watchdog refresh. No MIDI endpoints are opened by this test.

The diagnostic checkpoint before continuations uses the shared headers exported by
`git archive 72a949a firmware/include` into a generated source-root directory,
with the current tracked `firmware/include/cntrl/midi_runtime.h` overlaid for UART
loss reporting, and the current target diagnostic sources and SDK patches. Pass
that directory as `--source-root` with `--diagnostics` to reproduce the checkpoint
without including unfinished shared continuation changes. Keep the resulting
image and ELF together; generated source exports and reports under `build/` are
disposable and derive from the tracked Git sources.

The tracked `qualification/dynamic-memory-thru.toml` setup enables the ordinary
interface. Copy it with `cntrl setup copy`, using the built `target.toml`, then
select `dynamic-memory-thru` with `cntrl setup set`. Run:

```bash
uv run --frozen cntrl qualify --target-dir cntrl/maker/morningstar/mc -- --run --expect-enabled --destination 'cntrl experimental DIN' --source 'cntrl experimental DIN' --report build/dynamic-memory-thru-physical.toml
```

This checks exact Program Change, modulation, 64 paired note strikes, sustain
and streamed SysEx bytes. It does not measure wire timestamps or clock jitter.

For intermittent short-message failures, add `--probe-short-messages` to the
same `--run` command and use a separate report filename. This mode checks a
Mac-only virtual-source loopback, then compares paced and burst deliveries of
the short-message sequence and one complete long SysEx message. It preserves
unsolicited idle bytes, exact sent and
received bytes, and serial counters before and after each trial. Each received
trace is saved before requesting the post-trial diagnostics, so a subsequent
USB timeout does not lose that evidence. Like the other `--run` checks, this
probe arms the watchdog when the installed firmware supports diagnostics.

The November 12 investigation found exact short-message delivery in repeated
paced and burst trials, but also reproduced USB disappearance during a later
paced trial. The installed shared-heap image had no reported allocation failures
and a valid heap before that failure. Earlier diagnostics using newlib had
already lost bytes from a long SysEx test, so MIDI loss is not established as a
regression introduced by the bounded heap.

There is a concrete UART overflow risk: Serial1 uses the SDK's 64-byte RX ring
with 63 usable slots, whereas measured MIDI service gaps reach roughly 32.8 ms.
At 31,250 baud that interval spans about 102 bytes. The SDK silently discards
bytes when the ring fills. This can explain intermittent DIN return loss, but
the tests have not yet correlated a specific overflow with a captured mismatch.
It does not explain the USB disappearance. The leading empty SysEx observed on
some first deliveries also remains unlocalized; do not filter it from expected
results or treat it as confirmed host behavior.

The explicit watchdog self-test passed on the UART diagnostic image (HEX
SHA-256 `328d8344139cc0bc2b370ad6f278c568d141e1ead305a7a40f2eb1792d68fc9f`):
startup count advanced from 45 to 46, reset cause was `0x10`, retained phase was
21 and no fault was recorded. One subsequent paced/burst pair delivered all 304
bytes in each trial with zero UART software drops and hardware overrun latches.
A later paced trial lost communication after 44 received bytes; its trace was
saved, but post-failure UART and watchdog status could not be read. That result
does not establish UART overflow as the cause. Further MIDI traffic was stopped
at that point to prepare restoration of normal firmware and the original setup.

Data recovery initially finished on the retained normal format-41 image using
newlib, without the bounded heap or qualification watchdog. Its HEX SHA-256 is
`1757d72b2d42afb58b632d7bfcedd90f59493ada163a0295ccb0987f7cb3d2f3`.
The normal image reported the older heap diagnostics and rejected `QUAL STATUS`,
confirming the diagnostic image was no longer running. The uploader still
returned a nonzero status after reset/disappearance; this was not a successful
uploader attestation of the requested hash.

The earlier format-36 image did not stay reachable with the newer saved data;
the user chose the compatible normal format-41 recovery instead. Restore and
verification used the matching CLI exported from `8e1f07f`, with its frozen
dependencies, and the immutable `build/dynamic-memory-upgrade-backup` directory.
The original format-36 backup was upgraded through that CLI's existing format-41
restore path. Both setups, 15 history revisions, backed-up assets and songs
were restored. The two qualification setups were removed, then the restore was
repeated with only the original two entries to relink their setup-table indices.
The final comparison passed with `simple-setup` selected and
`CNTRL READY 41 25405 7ee6114a starts=83 starts_status=ready`.
The startup count stayed at 83 during restoration and verification. The user
then cycled from TRS Ports back to Patch Switcher and confirmed it was displayed.
MIDI stress testing stopped; the communication failure is not considered fixed.

The user then found that the region migration had changed the working page layout
and controls. The normal image was rebuilt with the one-window tool policy in
`b857268` and the legacy layout/control repair in `63f43ca`. The retained region
architecture now keeps Patch Switcher on the left, tools on the right, and the
old full-width Patch Switcher position in the F cycle. The user confirmed the
layout and F cycle physically. The new image's HEX SHA-256 is
`9d3a942d775a56a948c3ab73551fd4b39761862d3a170a36f37ef323e433d927`;
the pedal returned with startup count 86. The first upload failed on a USB write;
the second reached reset/disappearance with a nonzero uploader status again.

The already-converted format-41 data retained its saved region layout, so the
original format-36 backup was restored through the corrected upgrader. Existing
history imports correctly rejected different bytes under the same revision
number. With the user's approval, both converted history collections were
cleared and their original 15 numbered revisions restored with their original
dates and labels. The pre-repair format-41 backup is retained separately under
`build/page-regions-normal-target/before/`.
Final `cntrl setup verify` passed for selection, setups, history, backed-up
assets and songs, reporting
`CNTRL READY 41 25477 b32ccca8 starts=87 starts_status=ready`.

The Expression page update in `ca33347` was then overlaid on this normal recovery
source and built under `build/expression-dirty-normal-target/`. Its HEX SHA-256 is
`e48514e4a0d0f7bf12bd18ed5103863e222c50b717b0413e488890085441cc83`.
The approved `cntrl flash --skip-build` flow saved a fresh `before/` backup and
prepared `restore/`. The first upload hit the same USB write failure; the
`cntrl upload-helper` retry reached reset/disappearance, again with nonzero
uploader status. The pedal returned with startup count 88, and
`cntrl setup verify` against `restore/` confirmed both setups, all 15 history
revisions, backed-up assets, songs and selection unchanged. No restore writes
were necessary. The user confirmed a smooth TRS Ports to Expression transition
and readable fixed-layout text. The two previously unreadable, unreferenced
movie files remained unavailable to the backup, as before.

To reproduce the recovery tooling without modifying the working checkout,
export `git archive 8e1f07f` into a generated source directory under `build/`.
Its `../reccy` source dependency must point to the existing tracked sibling
project; the local recovery used a generated `build/reccy` symlink to it. Run
`uv sync --frozen --no-editable` and the existing `cntrl setup restore`, `setup del` and
`setup verify` commands from that export, using the normal image's `target.toml`.
For the final page repair, overlay the `b857268` versions of
`firmware/include/cntrl/page_layout.h`, `firmware/include/cntrl/document.h`,
`cntrl/patch_switcher/models.py` and `cntrl/patch_switcher/layout.py`, then apply
the `63f43ca` changes and, for the Expression update, the production changes in
`ca33347`. Keep the remaining production sources at `8e1f07f` and use
an SDK containing only that revision's declared patches, preserving newlib and
normal reset behavior. The local export used a private SDK copy with the later
UART diagnostic patch reversed. Build through `cntrl build`, then use
`cntrl flash --skip-build` and the existing upload helper if the first upload
fails. The repaired data must come from the original format-36 backup through
this export's `cntrl setup restore`, not the already-converted format-41 backup.
Source exports, virtual environments, decoded images, reports and dependency
symlinks under `build/` are disposable derivatives of tracked sources, not the
only copies of recovery implementation code. Preserve the original data backup.

The separate `qualification/dynamic-memory-dense.toml` setup has 24 parallel
USB-to-DIN routes with 16 identity transforms each, plus a DIN-to-USB return
route. Copy and select it with the same setup commands, then run:

```bash
uv run --frozen cntrl qualify --target-dir cntrl/maker/morningstar/mc -- --run --expect-enabled --destination 'cntrl experimental DIN' --source 'cntrl experimental DIN' --stress-only --stress-seconds '30 seconds' --report build/dynamic-memory-dense-physical.toml
```

The dense graph changes output multiplicity. Stress-only reports explicitly skip
byte verification and capture CPU/service-gap, heap and refusal counters instead.
The flood sends Program Change and modulation messages without holding notes.
For recovery measurement, add `--stress-bursts 1` to send one bounded burst to
each endpoint, then observe until the configured duration ends. Reports separate
sending and observation time. An uncapped flood can leave substantial queued
traffic in the host USB stack after sending stops, obscuring target recovery.
Save the original setup selection before testing and restore it afterwards;
temporary probe setups can then be removed with `cntrl setup del`.

Managed flashing retains original binaries and history in `before/`, and validates
a separate current-format `restore/` tree before entering the bootloader. Wire
upgrades preserve existing patch node IDs and inactive new gesture actions;
current RAM estimates are refreshed from the saved fonts and target description.
Standalone restore/verify commands require that current `--target` description.

The retained fixed storage has specific roles: physical display pixel caches
and DMA transfers; byte/mask/record format domains; three looper generation roles;
USB framing and retry units; redundant flash journals; and explicit text/history
retention policies. Endpoint-sized constructor vectors are created once from
actual hardware counts and are included in SDK/bootstrap heap reporting. They
do not grow on player actions. Shared presentation no longer allocates per frame.

## Original product target reservations

| Location | Present storage or constraint | Required treatment |
| --- | --- | --- |
| `firmware/patch_switcher/target.cpp` | Primary controller plus `DMAMEM extra_controllers[max_controllers - 1][sizeof(Controller)]`; placement constructs all three extras. | Allocate only the configured controller count. Placement `new` here does not allocate storage; removing only the constructor loop would leave the reservation in place. |
| `firmware/patch_switcher/target.cpp` | `DMAMEM invocation_navigation[Bridge::owners]`: twelve complete navigation snapshots. Owner numbering reserves eight switch owners followed by controller owners. | Allocate actual active invocation snapshots using the shared stable-owner design. Update switch/controller delivery and return logic together. |
| `firmware/patch_switcher/target.cpp` | Firmware visit record embeds two navigation states and fixed setup/song references; document/song/transfer objects also have static storage. | Stage dynamic return state before leaving a setup. Persist explicit values and counts, with no raw dynamic object serialization. Review optional module state by lifetime. |
| `firmware/patch_switcher/target.cpp` | `RenderPixels` has current and previous full-display caches in RAM1: 61,440 pixels each, two bytes per pixel, 245,760 bytes together. `DMAMEM render_work` holds the largest display: 20,480 pixels, 40,960 bytes. | These sizes derive from the three physical 160-by-128 displays. Measure the need and lifetime of each copy, especially previous pixels for transitions. Any lazy buffer must be prepared before use; do not remove rendering buffers without preserving fades and transfer timing. |
| `firmware/patch_switcher/displays.cpp` and `firmware/usb_probe/displays.h` | Two `DMAMEM DisplayBand` objects support display transfer and preparation. | Keep simultaneous producer/DMA lifetimes and required alignment/cache maintenance. A dynamic buffer must remain at a stable address until completion. No allocation or freeing in the completion interrupt. |
| `firmware/patch_switcher/target.cpp` | Global USB session and splash receiver/router embed the shared 4096-byte transfer and text/frame workspaces. | Establish which transfers can coexist and acquire only the required workspaces. Preserve any in-flight serial/USB data before reusing or freeing storage. |
| `firmware/patch_switcher/target.cpp` | Four `song_readers` and a four-pointer handle array are supplied to `SlotStorage`. | Map actual simultaneous reader roles, then allocate handles when needed. Do not replace a concurrency requirement with one shared mutable reader. File open/close may allocate inside the storage library. |
| `firmware/patch_switcher/target.cpp` | Stream identities are indexed by the shared sixteen-route ceiling. Physical button/port arrays and four expression assignments follow the actual hardware. | Replace route-indexed state with actual endpoint counts. Keep geometry/capability-sized physical input state unless the target configuration makes a device optional. |
| `firmware/patch_switcher/target.cpp` | SD identity formatting uses nine fixed text values; names, paths and replies use small bounded scratch. | Keep justified bounded formatting and allocation-free diagnostics. Do not heap-allocate every temporary string during an error report. |
| `firmware/patch_switcher/usb_forward_device.cpp` | High/full-speed descriptor scratch is 512 bytes each; product scratch is 128 bytes. USB identity code also contains immutable descriptors. | Separate descriptor format/endpoint constraints from optional scratch. If made dynamic, construct before enumeration and keep storage alive for the USB driver's full access period. |
| `firmware/patch_switcher/target_capabilities.h` and host target data | RAM capacity is 524,288 bytes and the host runtime allowance is 294,912 bytes. Target capabilities specify six buttons, four ports, three displays, ten endpoints and switch-count restrictions. | Keep physical capabilities separate from configurable collection limits. Replace the coarse runtime allowance with evidence-based fixed/core, configured and transient costs as the shared plan removes reservations. |

## Runtime allocation and dependency boundaries

The target's `filesystem.h` creates file and directory implementations through
owning pointers. Stream playback opens independent source/index handles and
constructs its player. Browsing recursively creates directory levels; cancellation
closes and destroys them. These paths are reached from pedal controls and host
commands while MIDI may be active, so storage allocation failure must propagate
without retiring the active source.

TRS mode reconfiguration constructs/destroys optional `FlexSerial` instances.
USB host enumeration/disconnection, display transfer/DMA setup, SD/SdFat file
operations and driver callbacks are transitive allocation boundaries. Instrument
their actual linked allocation behavior, including first use, rather than assuming
an absence of allocation from the target's explicit `new` sites. Do not move
enumeration or DMA lifetime management to an unchecked shared allocator.

The toolchain builds C++ with `-fno-exceptions`. In the inspected Teensy 1.62.0
core, ordinary `operator new` and `new[]` return `malloc` results; deletion uses
`free`, and aligned forms use aligned allocation. Consequently an ordinary
container growth failure is not a recoverable `bad_alloc` contract on this target.
The shared plan must cover both explicit allocation and library/container growth.
This audit does not propose changing toolchain or dependency versions.

`std::stable_sort` in the shared stream/timeline code can request temporary
storage. Target/native tests must cover the real nonthrowing checked path, not
rely on differences in the host standard library's allocation failure behavior.

## RAM1, RAM2 and measurement

`firmware/memory.ld` derives the budget from the linked heap
boundaries. Static `DMAMEM` objects consume RAM2 before the ordinary heap can use
it; ordinary static data and stack usage affect RAM1. Moving an object to dynamic
storage changes which region bears its cost, and may introduce DMA/cache alignment
requirements. Record actual linker placement rather than assuming every dynamic
or fixed buffer lives in the same region.

At the original audit, the status code in `target.cpp` estimated RAM2 free space
from the unextended heap plus `mallinfo().fordblks`. It did not report the largest
free block or distinguish the `MemoryBuffer` ledger from raw/STL allocations.
The current status uses bounded arena counters, while the explicit idle heap
diagnostic validates the physical chunks and reports checked and SDK usage.
RAM1 free space uses the current stack pointer relative to static data, which is
not a worst-case stack high-water mark. A large navigation or pane copy can matter
even if a later status query looks healthy.

Qualification needs linked static totals, allocator capacity/metadata, current
and peak live bytes, largest allocatable block, failure counts, stack high-water
and transient old/new peaks. Report region and alignment restrictions explicitly.
Do not add the full .cnsa movie size to RAM2: the player streams movies from SD;
charge resident pixel/colour/transfer state and any retained indexes instead.

Measure repeated setup changes, source retirement, display fades, monitor open/
close, loop and arpeggiator replacement, SD browsing and USB attach/detach under
pressure. Verify the audio/MIDI service loop's worst-case delay, not only absence
of a crash. Physical execution requires separate authorization and remains a
completion gate for the implementation.

## Other authored firmware and justified fixed data

| Area | Finding and scope decision |
| --- | --- |
| `firmware/boot_switch/` | Gesture history reserves 128 entries. Review the required boot gesture window, but keep boot/recovery independent of an exhausted application heap. Flash selection records and the 128-word NOR configuration are persisted/ROM layouts, not unused feature instances. |
| `firmware/usb_probe/` | Display/button arrays follow the physical device; small line/row scratch is bounded diagnostic working state. Shared display bands must retain DMA lifetime semantics. |
| `firmware/scroll_probe/` | The diagnostic image has dimensions 1280 by 256, occupying 327,680 bytes, plus physical buttons and small text/status buffers. It is a measurement fixture, not the product's live musical state. Preserve the reproducible experiment unless a separately requested change needs different image allocation. |
| `firmware/button_timing/` | Button-specific timing state follows the diagnostic's selected physical inputs. It is not an application collection limit. |
| Splash firmware/board and shared animation components | Panel dimensions and stream counts reflect the display layout. Full movie data stays on SD; shared colour state can still allocate at playback start and is included in the shared runtime inventory. |
| Compiled fonts, USB identity and flash journal layouts | Immutable ROM/flash data and required redundant records remain fixed. They must not become heap objects just to eliminate constants. |
| `archive/` and native test fixtures | Historical snapshots and test-only arrays do not reserve memory in the product binary. Exclude them from product totals, while retaining their role as test/reference material. |

## Additional work beyond the prompt

None.
