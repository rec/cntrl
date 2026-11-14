# MIDI transport control

This target adds transport measurements to the working normal/newlib firmware.
It retains the one-window tool pages, legacy layout/controls, cached Expression
page and USB MIDI flush guard. It does not enable the bounded allocator or a
watchdog, change the setup format, or change forwarding decisions.

The tracked build entry reconstructs baseline `d6c6d06` and the recorded UI and
flush-guard overlays, applies `trace.patch`, and copies the pinned SDK into a
separate source tree. A generated `platform.local.txt` invokes the original
provisioned compiler in place because the copied Windows compiler cannot resolve
its multilib headers. SDK core patches remain isolated in the copy. Existing
rollback SDKs and images are untouched. The
repository must contain those commits; the manual CI workflow fetches history
for this reason. No CI workflow is triggered by a push.

Use a fresh output name for each build:

```sh
uv run --frozen --no-editable cntrl build --target-dir cntrl/maker/morningstar/mc/diagnostic_control --output build/sysex-transport-control
```

Keep the adjacent generated `build/sysex-transport-control-source/` directory
with the export. The flash entry uses its matching compiler for backup/restore.
The export itself contains the ordinary portable artifacts and a build manifest
with a trace ID and expected image SHA-256. The trace ID is a compiled source
recipe label, not a cryptographic readback of every installed flash byte.

The usual backup/flash/restore flow is:

```sh
uv run --frozen --no-editable cntrl flash --target-dir cntrl/maker/morningstar/mc/diagnostic_control --output build/sysex-transport-control --skip-build --port /dev/cu.usbmodem144682801
```

This requires the existing user-launched upload helper. It preserves uploader
exit status and retains its fresh backup in `before/`, including after an upload
error. Do not start another uploader while one is running.

## Captures

Use the tracked `qualification/dynamic-memory-thru.toml` setup with the physical
DIN output connected to DIN input. Back up the current pedal data before selecting
the temporary setup. Then capture one SysEx trial:

```sh
uv run --frozen --no-editable cntrl qualify --target-dir cntrl/maker/morningstar/mc -- --run --expect-enabled --probe-short-messages --probe-trial sysex --midi-only --trace-build build/sysex-transport-control --destination 'cntrl experimental DIN' --source 'cntrl experimental DIN' --report build/sysex-midi-only-trace.toml
```

Omit `--midi-only` for the comparison with a serial client open during MIDI.
The capture checks the export's image checksum and the pedal's compiled trace ID
before sending MIDI. It resets trace state, closes that serial connection, runs
the selected probe, and stops/reads the trace after the MIDI client closes.
Consequently the MIDI-only case has no overlapping MIDI/serial clients.

The report is saved before MIDI starts. A separate `.midi.toml` file retains raw
trial bytes even if the later serial read fails. Byte mismatches remain failures;
the transport evidence is collected before that failure is propagated. A reboot
invalidates comparisons across the capture. Incomplete trace responses cannot be
interpreted as zero losses.

```sh
uv run --frozen --no-editable cntrl qualify --target-dir cntrl/maker/morningstar/mc -- --analyze-report build/sysex-midi-only-trace.toml --report build/sysex-midi-only-analysis.toml
```

The analysis includes byte alignments and the transport comparison. Repeating
payloads can produce ambiguous or nonminimal alignments; use counters/checksums
to localize a boundary, not to infer individual lost-byte positions from length
alone. Restore and verify the original setup, history, assets, songs and selection
after testing, and delete only the temporary qualification setup.

## Measurements

The five counters/checksums cover raw USB MIDI input, USB packets accepted by
bridge validation, successful DIN UART write calls, bytes read from the DIN UART,
and successful USB-return driver submissions. They cover all bytes on the measured
DIN route between RESET and STOP, including unsolicited traffic. Driver acceptance
does not acknowledge bytes on the physical wire. Non-SysEx parser normalization,
such as running status, can legitimately change a byte stream.

The first rejected event retains its full USB event bytes, source, destination,
parser SysEx/discard state, timestamp, and offsets in the raw/accepted streams.
Every rejection on the measured route is counted. More than one rejection means
the event detail is incomplete; there is no unbounded event log.

UART software drops count discarded receive-ring bytes. Hardware overrun latches
count observations, not an exact number of lost bytes. The first observed change
of each counter also records the gap since the previous receive poll and buffer
occupancy. These are main-loop observations, not precise ISR timestamps.

The diagnostic state has fixed semantic records for these transport boundaries
and first failures. Recording allocates no memory and prints no serial data.
RESET requires an idle output; STOP freezes only measurements and does not stop
MIDI forwarding. Trace state is inactive at boot. Linked RAM and code totals must
be checked for each built image because instrumentation can affect timing.
