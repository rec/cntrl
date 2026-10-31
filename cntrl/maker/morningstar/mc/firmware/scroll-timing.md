# Optical scroll timing diagnostic

Enter with `CNTRL DISPLAY EDGE` over USB serial. Once `PRESS C` is visible,
record at 240 fps and press C once. Keep the left label and both edges of the
centre display sharp and in view. Avoid exposure saturation. Let the entire
sequence finish and return to `PRESS C`, then stop recording. F exits to the
normal patch UI at any time; `CNTRL DISPLAY EDGE END` does the same over USB.
C and F are consumed by the diagnostic rather than sending or undoing patches.
The test does not change saved configuration, assets, or panel frame-rate settings.

The left display labels sixteen leftward trials, then sixteen rightward trials.
Each direction repeats the 2 ms / 5 ms pair eight times. For example, `L 2MS R1`
means leftward, 2 ms, repetition 1; `R 5MS R8` is the final trial. The whole
sequence takes approximately one minute in real time. For each trial:

1. All scroll offsets reset to zero. Each RAM column is painted incrementally,
   including the two hidden columns. Centre-display edge columns are black;
   short interior ticks identify the baseline.
2. The pattern settles for 800 ms plus 1,999 microseconds times the trial index.
   This varies the scheduling relative to the free-running panel scan; it does
   not measure or guarantee coverage of particular scan phases.
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
This diagnostic measures a different sequence from normal animation:
one pointer jump followed by one write, with no ongoing scroll commands.

One sequence samples only 32 uncontrolled scan phases. Repeat recordings if necessary;
a clean delay in one trial is not a guaranteed safe bound. Keep the original
video file, including its slow-motion timing information.

Native tests cover the complete order, both RAM addresses, all delay deadlines,
one marker write per trial, idle arming, repetition, stopping and micros rollover.
Bus tests verify that only the centre panel receives the trial offset. These
tests do not establish physical pointer application time.

The original twelve-trial version was installed 2026-10-31 with image SHA-256
`d88a1d3b158a483065e8f813a1ba45e846562abc749a3a926525448fa21fccd7`.
The helper completed the upload and reset, then USB returned
`CNTRL READY 20 21411 fa5ba7e8` and `CNTRL EDGE ARMED`. This verifies command
acceptance and setup loading, not the visible pattern or physical timings.
The recording sequence has not been started from the host.

## First recording and repetition follow-up

`/Users/tom/Desktop/signal-2026-10-31-19-08-33-369.mp4` contains 3,599 video
frames at 720x1280 and 30 playback fps, approximately 120 seconds. Rotating it
counterclockwise makes the labels upright. At playback 4.300 s (LEFT 0 MS),
striped fragments appear at the old left edge while the intended marker appears
at the right. Around 5.967 s (LEFT 2 MS), fragments again appear at both edges.
The inspected LEFT 5-20 ms onset frames did not show the same wrong-edge marker;
this is not a safe timing bound. The inspected RIGHT onsets did not supply an
equally clear wrong-edge transient. Export timing varies across the recording.

Offset reset between trials briefly moves the previous marker to the opposite
edge before clearing it; those frames must be excluded from onset analysis.
The repeated 2/5 ms sequence above follows this first recording and tests more
uncontrolled phases. No change to the normal animation algorithm is made.

The 32-trial version passed native tests and ARM image checks and was installed
with SHA-256
`7aeb13b8401ee11b9b32785ea4d181ddd3e860f4a878b47fbfddd6863eb4ef37`.
After reset it returned the same saved-setup status and `CNTRL EDGE ARMED`.
It is waiting for C; no recording sequence was started from the host.
