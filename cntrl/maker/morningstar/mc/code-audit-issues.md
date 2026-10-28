# Hardware appendix to the code audit

Part of [plan/issues.md](../../../../plan/issues.md), reviewed 2026-10-28 at
`eab498633baa2e7556a7f0ff2cadc83209a7ea94`. Findings below are source-traced;
they were not reproduced on the pedal. Layout and SD-space usage are excluded.
Paths below are relative to this directory unless stated otherwise.

## M1. P2: tool takeover can suspend an unfinished SysEx termination

**Evidence:** `firmware/patch_switcher/target.cpp:57` (`enter_panic`),
`:76` (`enter_cable_test`), `:103` (`output_idle`), and `Board::poll`;
generic `firmware/include/cntrl/midi_bridge.h:88`, `reset_route()` and `service()`.

The entry guard considers only pending pedal batches. It does not consider bridge
queues or an already-started wire SysEx. Disabling the bridge queues a terminating
F7 when necessary, but while Panic/cable test is open, `Board::poll` runs the tool
instead of `bridge_.service()`. The termination is delayed until leaving the tool,
even if the tool remains Off. Tool traffic can start without the promised explicit
SysEx closure; the deferred F7 can appear only after returning.

**Correction to consider:** finish the bridge's output-ownership handoff before
allowing tool transmission, including pending termination and driver-buffered
bytes. Test takeover after the first SysEx packet and with a blocked transmitter.
Do not simply wait for ordinary incoming bridge traffic to become idle forever.
This is a gap against the interrupted-SysEx and exclusive ownership contracts.

## M2. P1: a disconnected USB-host output can indefinitely block module entry

**Resolved:** the target delivers USB-host connection changes to the bridge.
Disconnect discards remaining host pedal batches and records the drops; reconnect
cannot replay them. Native tests cover disconnect before and during submission.

**Evidence:** `firmware/patch_switcher/target.cpp:119` (`send`), `:134`
(`submit`), `:191` (`poll`), and the output-idle guards in `poll_target`;
`firmware/usb-midi-interface.patch:156` makes `try_write_packed()` return false
when the host transmitter has no `txpipe`.

If a pedal batch is accepted for USB host route 8 and the destination disconnects
before submission completes, the sender keeps retrying that route. Host Task/read
runs, but there is no host-disconnect reset/drop policy for the queued pedal batch.
The bridge's `connected()` notification describes the computer-facing USB device
only and clears pedal queues only on routes 6/7. A route-8 batch can therefore
keep `output_idle()` false until the host device reconnects, preventing entry to
Panic, Songs and other guarded operations. It may also replay an old selection
when a device reconnects.

**Correction to consider:** define host-output disconnect semantics and retire
undeliverable batches on that event, with an error/drop indication. Test disconnect
between acceptance and delivery, including a partially delivered batch.

## M3. P2: reply replacement can splice two serial responses together

**Evidence:** `firmware/patch_switcher/target.cpp:182` (`reply`) and the partial
serial write at the end of `Board::poll`; generic Documents history progress
replies in `history_tick()`.

`reply()` overwrites the single buffer and resets its offset even when a previous
response is only partly transmitted. Host backpressure plus a subsequent progress
or final reply can produce a prefix of one line followed by another line. The
normal client waits for responses, but history jobs generate WAIT replies without
another request, so that convention alone does not prevent this condition.

**Correction to consider:** finish an in-progress frame before accepting its
replacement; progress notifications may be coalesced before transmission, while
final replies must be retained. A writable/completion callback or a polled
ready-state is sufficient. Test a serial sink accepting one byte at a time.

## M4. P2: font upload errors/timeouts retain the writer

**Resolved:** failed commands close the writer, and the target polls a ten-second
inactivity timeout. Native font-peer assertions cover timeout and malformed DATA.

**Evidence:** `firmware/patch_switcher/font_storage.h:23`–`:57`.

The generic error lambda only replies. Invalid DATA, an unknown font command or
a client disappearing after BEGIN leaves `writer_` open. Unlike document uploads,
there is no timeout or abort operation. A subsequent BEGIN or END eventually
closes it, but the resource remains owned indefinitely if neither arrives.
This is one retained writer, not an unbounded allocation leak.

**Correction to consider:** give font transfers explicit cancellation and timeout
cleanup, aligned with song/document transfer ownership. Also rename the target's
`font_command` callback: it dispatches both FONT and SONGS commands, so its name
hides a second protocol owner (`target.cpp:539`, `splash/router.h`).

## M5. P3: font staging repeats whole-file checksums and duplicates active data

**Evidence:** `firmware/patch_switcher/font_storage.h:15` (`stage`),
`:106` (`verify`), `firmware/patch_switcher/font_asset.h` (`Cache`).

Each distinct size from one asset verifies the entire file again, synchronously.
The cache only deduplicates pending faces; selecting another setup with the same
faces allocates and reads them again while the old active faces remain resident.
Active and pending sets can each hold 96 KiB. `stage` also allocates a candidate
before `Cache::add` checks the remaining pending budget, so a rejected candidate
can temporarily request another allocation of up to 96 KiB. Allocation failure
is checked; these allocations are owned by `unique_ptr` and freed on failure or
cache completion. This is peak pressure and repeated CPU work, not a proven leak.

**Correction to consider:** check pending capacity before allocating, verify each
asset once per preparation transaction, and evaluate retaining unchanged faces
without duplicating ownership. Any sharing must preserve rollback and renderer
lifetime rules. The existing renderer invalidation before committing the new
cache is important; this audit did not establish a use-after-free in that path.
Measure heap high-water and fragmentation during repeated setup switches before
choosing a larger memory-management change.

## Deployment-status references

`firmware/patch-switcher.md` records the Panic combined-image upload and its
post-boot setup verification. Compare that record with the stale status text in
the generic plans; do not confuse installation with physical MIDI qualification.

## Additional work beyond the prompt

None. Documentation only; no device commands were sent.
