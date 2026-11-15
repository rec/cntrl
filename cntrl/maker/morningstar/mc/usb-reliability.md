# USB reliability qualification

The public entry point is `cntrl qualify`. These are explicit hardware tests,
not CI jobs. Previewing a profile does not discover devices, open MIDI/serial,
send traffic, reset or flash anything. Live execution requires `--run`.

Wait until the pedal is exclusively available. The suite changes setups and
the pressure phase deliberately fills queues and memory. DIN output must be
connected back to DIN input; disconnect external instruments for this fixture.

## Profiles and preview

Use the maintained runtime profile:

```bash
uv run --frozen --no-editable cntrl qualify --target-dir cntrl/maker/morningstar/mc -- --reliability cntrl/maker/morningstar/mc/qualification/usb-reliability.toml --report build/usb-reliability.toml
```

This prints the plan. It does not create the live report or access hardware.
Set `target_export` to an existing export matching the installed firmware and
record the actual hub/USB port and cable in the profile. Relative paths resolve
against the profile's directory. `UNRECORDED` labels block live execution.
The supplied profiles expect an export at `build/usb-reliability-target`.
Do not substitute a newly built, different firmware image merely to fill that path.
Update the non-editable installation after source changes with
`uv sync --frozen --extra emulator --no-editable --reinstall-package cntrl`.

When the pedal is free and the profile/export have been reviewed, add `--run`
to the same command. Use a fresh report name for every live run.

| Runtime stage | Default workload |
| --- | --- |
| Preparation | Capture diagnostics, take a fresh complete backup, install two distinctly named forwarding setups, select the first and record file hashes. Existing fixture names cause rejection. |
| Connections | 100 fresh MIDI client lifetimes, alternating MIDI-only and simultaneous MIDI/serial probes. Each runs exact paced, burst, 4099-byte SysEx and note/sustain/release comparisons. Serial clients also close/reopen. |
| Serial | 30 minutes repeatedly downloading the bundle and all backed-up readable font/movie/MIDI assets with frame CRC, file checksum and content-hash checks. |
| Mixed | 30 minutes of those downloads while a separate paced MIDI sender uses the same MIDI client. Only the main thread owns serial. |
| Soak | Eight hours of the mixed workload. The default MIDI pattern averages approximately 1643 bytes/s while sending, below DIN's 3125 bytes/s wire limit. It includes SysEx, notes/sustain/releases, program changes and CC. Each transfer drains before exact comparison. |
| Pressure | Hold 64 notes under sustain; submit 200 bursts to every cntrl USB output and send the owed releases halfway through the flood. Require all releases/sustain-off to survive, observed admission pressure, a valid heap and an immediate successful exact MIDI probe afterward. |
| Restoration | Delete only the two temporary setups, restore recorded memory estimates and verify the original selection, setups, history, assets and songs. |

Every fifth completed serial transfer switches between the two forwarding setups
after MIDI drains. This exercises setup teardown, status-page rendering and page
transitions. It does not automate every physical button or exercise every page.
In mixed/soak stages, each selection also drains and records its MIDI cleanup
before the next payload starts. Only complete, zero-valued channel resets for
CC 64–69, 120, 121 and 123 are accepted in that handoff window. All other bytes
fail the stage; cleanup messages within a payload still fail exact comparison.
The bounded pressure stage is not a lossless-throughput test: allocation rejection
and optional MIDI drops are allowed there, but not lost owned releases, UART
losses, quarantine, an invalid heap or a reboot. If the chosen burst count no
longer reaches admission pressure, the stage fails rather than claiming overload
qualification. Change the profile and perform a new reviewed run.

## Uploader is separate

Preview `qualification/usb-uploader.toml` with the same command structure. Its
default is three backup/flash/restore/verify cycles of the supplied existing
image. It intentionally reboots, so it cannot be combined with runtime stages.
The export must include its `distribution/` resources. The normal installation
flow composes those resources and verifies that they reproduce the selected
image before any device access; each trial then uses `cntrl flash`'s maintained
backup, install, restore and verification implementation.
The user-launched upload helper must be running as documented in
[upload-helper.md](firmware/upload-helper.md). The profile's serial number selects
the unit explicitly; the uploader retains its normal image ownership checks.

All images and target/font inputs are copied into the run's artifact directory
and hashed before any device access. The report records the expected image hash,
target/input hashes, Git revision, host/Python version, uploader path/hash and
user-supplied hub/cable labels. The image hash identifies the selected file; it
is not cryptographic readback of the previously running firmware.

## Failure evidence and pass criteria

Serial qualification uses the existing frame decoder with retries disabled.
The maintained copy, restore and verification operations accept the same recorded
connection, so preparation and recovery do not introduce competing serial clients
or silently recover a bad frame. Normal CLI defaults remain unchanged.

Each stage runs in a child process. A heartbeat tracks progress on the serial
owner; MIDI transmission alone cannot conceal a blocked serial operation.
Runtime stalls preserve a host stack sample, USB enumeration, worker log, latest
firmware diagnostics and raw serial/MIDI traffic before termination. Termination
is also enforced at the phase deadline, even if a busy worker keeps producing
heartbeats without completing its operation. Downloads get the configured phase
duration plus the maintenance allowance; connection stages get the stall budget.
Termination and SIGKILL waits are bounded too; a kernel-blocked worker's PID is retained for
manual recovery. An uploader is never terminated by this supervisor: a stall is
recorded as failure and the supervisor waits for its existing upload flow to
finish. Interrupting that wait leaves the upload worker running and reports its
PID. Do not start recovery while it is still uploading.

Raw traffic rotates between two trace files, using 64 MiB total per stage by
default. Records have a monotonic timestamp, direction and hexadecimal bytes.
Large captures are split with `direction@offset/length` labels to retain the
same bound even at a small configured trace size.
Malformed frame bytes are recorded before closing the serial connection.
An exact MIDI mismatch also saves `trials` with both payloads, usable with
`cntrl qualify --target-dir cntrl/maker/morningstar/mc -- --analyze-report STAGE_RESULT`.
Aggregate traffic counts and last diagnostics stay in the stage report even
when older raw traffic rotates out. Backup and input files are retained separately.

Success requires both a passing stage report and a clean worker exit. It rejects
malformed/missing reports, unexpected startup counts, missing endpoints, UART
drops/overruns, quarantine, allocation failures outside pressure, and any exact
MIDI/file mismatch. The sole startup exception is an exact `F0 F7` during the
initial one-second capture; later payload bytes are never stripped.

On the first failure, no next test, reset, retry or automatic restoration occurs.
The top-level report identifies the failed stage and offers existing `cntrl setup`
commands for restoration when a complete backup exists. Review evidence first;
ensure any retained worker has stopped before using the serial port. Successful
runtime runs restore and verify automatically. A clean report qualifies that
workload, firmware and USB arrangement, not every possible host or hub.

## Software verification

Offline tests cover profile units and stage selection, default preview safety,
counter/reboot failure policy, exact held-note extraction, concurrent loopback
traffic, bounded raw capture, frozen font inputs and real child-process hangs.
The test workers perform no USB/MIDI calls; failure capture is replaced with a
local evidence writer. Full physical qualification remains pending until the
pedal is free. The implementation itself does not schedule a hardware run.

## Additional work beyond the prompt

None.
