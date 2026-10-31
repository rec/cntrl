# Optical scroll timing diagnostic

Enter with `CNTRL DISPLAY EDGE` over USB serial. Once `PRESS C` is visible,
record at 240 fps and press C once. Keep the left label and both edges of the
centre display sharp and in view. Avoid exposure saturation. Let the entire
sequence finish and return to `PRESS C`, then stop recording. F exits to the
normal patch UI at any time; `CNTRL DISPLAY EDGE END` does the same over USB.
C and F are consumed by the diagnostic rather than sending or undoing patches.
The test does not change saved configuration, assets, or panel frame-rate settings.

The left display labels six LEFT trials, then six RIGHT trials. Delays are
0, 2, 5, 10, 15 and 20 ms in each direction. For each trial:

1. All scroll offsets reset to zero. Each RAM column is painted incrementally,
   including the two hidden columns. Centre-display edge columns are black;
   short interior ticks identify the baseline.
2. The pattern settles for at least 800 ms, with a small trial-dependent offset.
3. Backlight PWM rises from 96 to 192, followed by a single three-column hardware
   scroll command to the centre display only.
4. After the labelled delay, one previously visible column is replaced with a
   white marker broken by horizontal black bands. LEFT rewrites RAM column 0;
   RIGHT rewrites column 159. The result is held until 700 ms after the trigger.

With a settled LEFT offset, the rewritten marker belongs at the right edge.
With a settled RIGHT offset it belongs at the left edge. A transient marker at
the opposite edge indicates that the write was visible using the old mapping.
Inspect the transient sequence, not just the final held pattern.

The brightness step is an approximate optical reference, not a TE signal.
Backlight circuitry, PWM phase, camera exposure and rolling shutter affect the
observed brightness change. Software deadlines use the poll timestamp immediately
before the brightness/scroll commands; transfer time and scheduling add error.
The 0 ms trial means write immediately after the scroll command, not simultaneous
application. This diagnostic measures a different sequence from normal animation:
one pointer jump followed by one write, with no ongoing scroll commands.

One sequence samples only twelve scan phases. Repeat recordings if necessary;
a clean delay in one trial is not a guaranteed safe bound. Keep the original
video file, including its slow-motion timing information.

Native tests cover the complete order, both RAM addresses, all delay deadlines,
one marker write per trial, idle arming, repetition, stopping and micros rollover.
Bus tests verify that only the centre panel receives the trial offset. These
tests do not establish physical pointer application time.

Installed 2026-10-31 with image SHA-256
`d88a1d3b158a483065e8f813a1ba45e846562abc749a3a926525448fa21fccd7`.
The helper completed the upload and reset, then USB returned
`CNTRL READY 20 21411 fa5ba7e8` and `CNTRL EDGE ARMED`. This verifies command
acceptance and setup loading, not the visible pattern or physical timings.
The recording sequence has not been started from the host.
