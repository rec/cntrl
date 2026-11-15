# USB reliability trial, 15 November 2026

The initial runtime trial used the public `cntrl qualify --reliability` flow,
firmware `63d098a`, setup format 48, serial `14468280`, USB location `0-1.3`,
and the existing connected hub/cable. The user confirmed DIN output-to-input
loopback and disconnected external instruments. Firmware was not flashed.

The matching firmware was composed with `cntrl flash --prepare-only` from its
existing component export. The image SHA-256 was
`54d799f4584f529645fcc4810eb25ec51d632a2a3714ac16a76f2f367b4cd553`.

## Initial trial

- All 100 client connection cycles passed their paced, burst, 4099-byte SysEx,
  and note/sustain/release comparisons.
- The complete 30-minute serial-only phase passed: 30,894 verified downloads.
- Mixed traffic stopped after 55 verified downloads because the next comparison
  received 4,174 bytes instead of 4,099.
- Raw capture showed a 75-byte prefix of zero-valued channel resets on channels
  1 and 2, using CC 64–69, 120, 121 and 123. Comparing the remaining 4,099 bytes
  against the transmitted SysEx showed an exact match. No payload byte was lost.
- These resets followed the test's own setup selection. The harness had failed
  to collect setup cleanup separately before starting the next payload.
- Fresh post-failure diagnostics still showed startup count 107, a valid heap,
  zero allocation failures, zero UART receive drops/overruns, and no quarantine.
  USB serial remained reachable. The overnight soak and pressure stage had not run.

The stopped run's report, raw traces, diagnostics, host capture and complete
backup remain in `build/usb-reliability-20261115-artifacts`, with the top-level
report at `build/usb-reliability-20261115.toml`. These generated files include
private pedal data and are not committed.

The maintained backup flow marked the backup complete but recorded two existing
unreadable movie objects (`0e27f3f6` and `bb548a72`) in `unavailable-assets.txt`.
They were not treated as readable assets by the test. Both original setups,
all 15 history revisions, readable assets, songs and selection were restored
through `cntrl setup restore` and verified unchanged through `cntrl setup verify`.

## Continuation

The corrected harness drains and records setup handoff separately, accepting
only complete zero-valued reset messages for the documented CC set. Unexpected
handoff bytes still fail, and reset messages occurring inside a payload still
fail exact comparison. MIDI mismatches now also retain payloads in the existing
`--analyze-report` format.

`usb-reliability-20261115-continued.toml` requested the remaining 30-minute mixed
phase, eight-hour soak, pressure/recovery checks, and automatic restoration.
The user subsequently requested that the soak finish its current hour and stop
for the night. The supervisor was interrupted after approximately two hours and
five minutes of soaking. Its report correctly remains `state = "interrupted"`,
`success = false`; this was a deliberate stop, not an observed USB failure.

- The complete 30-minute mixed phase passed: 727 verified downloads and
  3,013,999 MIDI bytes received exactly as sent. Setup changes produced another
  3,126 bytes of separately validated reset traffic.
- Before the requested stop, the soak completed 3,014 verified downloads.
  Its last saved snapshot recorded 12,483,191 MIDI bytes sent and received,
  with 11,688 bytes of separately validated setup cleanup.
- There was no observed USB disappearance, unexpected reboot, UART receive
  drop/overrun, MIDI drop/abort, allocation failure or active quarantine.
- A fresh `cntrl qualify --inspect` after stopping still reached firmware
  `63d098a`, startup count 107. The heap was valid, with zero allocation failures
  and zero interrupt allocations. Heap usage was 186,824 bytes versus 186,848
  at the continuation baseline; this run showed no increasing retained usage.
- The pressure/recovery phase was not started. The full eight-hour soak was
  not completed. This is a useful partial qualification, not a full pass of the
  requested profile, and it does not establish the cause of older USB failures
  or attribute their absence to an uploader/toolchain change.

The soak mostly reads data but also selects a test setup every fifth download.
Each selection persists an 84-byte selection record to SD, with additional
filesystem writes. It does not write internal program flash or upload firmware.

Generated continuation evidence remains in
`build/usb-reliability-20261115-continued-artifacts`, with the supervisor report
at `build/usb-reliability-20261115-continued.toml` and the fresh final diagnostics
at `build/usb-reliability-final-status-20261115.toml`. The terminal snapshot may
contain an in-progress operation because the stop interrupted a worker; only
the completed downloads and recorded comparisons above are counted.

After the requested stop, `cntrl setup set`, `del`, `restore` and `verify` were
run through the pinned host. Both temporary test setups were deleted. Both
original setups, all 15 history revisions, readable assets, songs and selection
were verified unchanged. `simple-setup` was selected with length 25,786 and CRC
`2c1e1c31`; startup count remained 107. Restoration retained a fresh safety
backup at
`build/usb-reliability-20261115-continued-artifacts/before-before-restore-7000106e4562`.
The two pre-existing unreadable movie objects remained recorded as unavailable;
this run did not repair them. No test worker was left running and no firmware
was flashed.

The continuation pins host revision `650f9d9` in the isolated checkout
`build/usb-reliability-host-650f9d9`, installed without editable dependencies.
That revision contains the corrected harness and the format-48-compatible host
client. Main subsequently added source-archive transfers during setup copy;
the existing pedal firmware predates that command. Pinning the host keeps this
trial on the same firmware instead of introducing an upgrade. The corrected
harness is also integrated into main. The pinned tree passed 1,742 software
tests (one skipped); the integration with newer main passed 1,803 (one skipped).
