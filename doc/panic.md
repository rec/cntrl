# Panic page

Panic follows MIDI Cable Test in the page cycle. It uses the shared
background, heading and text style, with press/hold instructions and a
Ready/Sending indicator. The other two displays stay in place.

Press C to start and temporarily set the screens to maximum brightness. Release
C to restore the brightness that was active on entry. Browsing the page sends
nothing and leaves normal MIDI routing unchanged.

Triggering Panic also stops all MIDI-file playback, including infinite loops,
stream file sources and the MIDI looper. Playback stays stopped after Panic
finishes or you leave the page. A quick tap remains pending while playback and
queued MIDI finish their cleanup, then sends the reset sequence without another
press.

A press starts this sequence on every available MIDI output, on channels 1..16:

1. All Notes Off: CC 123, value 0.
2. All Sound Off: CC 120, value 0.
3. Reset All Controllers: CC 121, value 0.

A tap continues this first controller sweep after C is released, for up to one
second. If an output remains blocked, it stops and shows `Incomplete`; a completed
sweep returns to `Ready`. Once all
outputs have completed it, a continued hold starts Note Off messages with
velocity 0, covering every note on every channel and output. Notes described to
the user as 1..128 are encoded as MIDI values 0..127.

If C remains held after the Note Off sweep, repeat from the controller messages.
Release during Note Off or a subsequent controller sweep to stop scheduling
further messages. F cancels even the first sweep, restores brightness if needed,
and continues the page cycle. Re-entry is idle.

## Implementation boundaries

`firmware/include/cntrl/panic.h` owns the bounded state machine and brightness feedback. The
shared page renderer supplies its display. `PanicOutputs`
provides output availability and nonblocking three-byte transmission. The
module has no dependency on the cable tester or MIDI interface module.

Progress is independent per output, with a barrier between controller and Note
Off sweeps. A blocked output is retried without duplicating or skipping messages
and cannot starve another output. An unavailable output is skipped for that
sweep. Each poll attempts at most one message; no busy waits or large generated
message buffers are used.

The target pauses ordinary bridging while Panic owns the ports, then restores
the setup's bridge setting when sending finishes or the page is left. Messages go directly to bounded transmitter
buffers rather than building up a software backlog after release. Bytes already
accepted by a transmitter finish transmitting.

## Verification

Native tests cover the shared page layout, full controller and Note Off sequences on all
outputs/channels, repetition, tap/hold/release, brightness restoration, retry,
disconnection, cancellation, re-entry, playback cancellation and a quick tap
waiting for MIDI cleanup. Physical message capture and visible
hold feedback require pedal verification.
