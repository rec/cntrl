# Panic module

Open Panic from the module menu. All three screens have black backgrounds.
The center screen has only a white Panic label at the bottom above B; Back is
at the top-right of the right screen near F.

Press B to set the screens to maximum brightness using the same temporary
brightness-feedback path as the patch manager. Release B to restore the
brightness that was active when entering Panic. Opening the page sends nothing.

A press starts this sequence on every available MIDI output, on channels 1..16:

1. All Notes Off: CC 123, value 0.
2. All Sound Off: CC 120, value 0.
3. Reset All Controllers: CC 121, value 0.

A tap completes this first controller sweep even after B is released. Once all
outputs have completed it, a continued hold starts Note Off messages with
velocity 0, covering every note on every channel and output. Notes described to
the user as 1..128 are encoded as MIDI values 0..127.

If B remains held after the Note Off sweep, repeat from the controller messages.
Release during Note Off or a subsequent controller sweep to stop scheduling
further messages. F cancels even the first sweep, restores brightness if needed,
and returns to the module menu. Re-entry is idle.

## Implementation boundaries

`firmware/include/cntrl/panic.h` owns the page and bounded state machine. The
hardware interface supplies brightness and display access. `PanicOutputs`
provides output availability and nonblocking three-byte transmission. The
module has no dependency on the cable tester or MIDI interface module.

Progress is independent per output, with a barrier between controller and Note
Off sweeps. A blocked output is retried without duplicating or skipping messages
and cannot starve another output. An unavailable output is skipped for that
sweep. Each poll attempts at most one message; no busy waits or large generated
message buffers are used.

The target pauses ordinary bridging while Panic owns the ports, then restores
the setup's bridge setting on exit. Messages go directly to bounded transmitter
buffers rather than building up a software backlog after release. Bytes already
accepted by a transmitter finish transmitting.

## Verification

Native tests cover the page, full controller and Note Off sequences on all
outputs/channels, repetition, tap/hold/release, brightness restoration, retry,
disconnection, cancellation and re-entry. Physical message capture and visible
hold feedback require pedal verification.
