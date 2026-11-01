# Unchanged-pixel rewrite comparison

The current `CNTRL DISPLAY EDGE` sequence alternates two conditions using the
same preloaded 162-column pattern. No labels are repainted during the test.
Brightness remains constant. Each pair consists of one leftward and one
rightward scroll:

| Pair | Condition |
| --- | --- |
| 1 | Scroll commands only |
| 2 | Rewrite existing pixels before every scroll step |
| 3 | Scroll commands only |
| 4 | Rewrite existing pixels before every scroll step |

Press C once and watch the four pairs, approximately 16-17 seconds overall.
The 1.5-second holds separate individual movements. Allow 20 seconds if recording.
C repeats all four pairs; F or `CNTRL DISPLAY EDGE END` restores the saved UI.
If a power cycle clears diagnostic mode, rearm with `CNTRL DISPLAY EDGE`.

Both conditions keep the production 500 ms / 60 Hz progress targets. Each step
reserves 1.5 ms from step start to its scroll command. The rewrite condition
uses that interval for the usual three panel-column writes, in panel order,
at the production incoming-column addresses. Every pixel written is regenerated
from its fixed RAM coordinate and exactly matches what was preloaded there.
The control condition waits through the same interval without writing pixels.
If the actual writes take longer than 1.5 ms, the command runs late; equality
of physical timings has not been measured. This spacing replaces the preceding
scroll-only diagnostic's 1 ms minimum between commands.

Native tests check the expected write/no-write pair order, all three addresses
and payloads at every step, zero RAM changes, minimum command spacing, all
eight offset sequences, repeat, stopping and micros rollover. The original
preload pattern and the normal patch-scrolling algorithm are unchanged.

A repeatable visible difference between control and rewrite pairs is useful
without a slow-motion camera. Corruption during same-pixel rewrites would
reproduce a write-operation interaction without changing the RAM image.
Clean rewrite pairs would strengthen the changed-content hypothesis, subject
to the different pattern and unmeasured timing. A visually clean result is
provisional until the later slow-motion recording.

Installed and armed on 2026-11-01 with image SHA-256
`6a172d08c0dc517ce93e5d582fe06305bbbec61ab1052de8a03876be6d134145`.
Native tests and ARM image checks passed. After upload/reset, USB returned
`CNTRL READY 20 21411 fa5ba7e8` and `CNTRL EDGE ARMED`. The sequence awaits C;
visual comparison remains pending.

## Previous scroll-only diagnostic

The preceding `CNTRL DISPLAY EDGE` experiment preloaded all 162 RAM columns on
each panel, then changes only the scroll offset during motion. It replaces
the production-sequence fixture described below. There are no pixel writes,
repaints, backlight changes or RAM resets during the sequence or a C-triggered
repeat. Normal patch scrolling is unchanged.

The stationary pattern has broad horizontal bands and narrow vertical rulers.
Each panel has three differently phased 54-column zones across its 162-column
RAM ring. The two initially hidden columns are deliberately filled too, using
the same pattern. There are no A/B/C labels or `PRESS C` prompt in this version.

When ready to record:

1. Keep all three patterned displays, including both edges, sharp and in view.
2. Record at 240 fps and press C once.
3. Allow 20 seconds for four left/right pairs and their 1.5-second holds.
4. Stop once the original pattern stays still. C repeats without repainting;
   F or `CNTRL DISPLAY EDGE END` exits and redraws the saved setup.

Each motion uses the production 500 ms / 60 Hz progress targets, one-column
offset steps, and 232 steps. Offset commands are spaced at least 1 ms apart
to avoid compressing each batch after removing the three column transfers.
This spacing approximates the removed work; it is not measured production
timing. The final batch can extend a few milliseconds beyond the 500 ms target.
The full sequence takes approximately 16 seconds, with 20 seconds allowing
recording margin. It remains armed indefinitely awaiting C.

This is circular motion of the preloaded pattern, not replacement of panes
across the virtual 72-column gaps. Rulers and band boundaries legitimately
wrap from one edge to the other. Inspect whether the *whole pattern* translates
consistently, rather than treating any narrow feature at an edge as a defect.
A fixed-edge fragment that disagrees with the translating pattern is the
relevant observation.

If a matching defect persists, it has been reproduced without concurrent RAM
writes. If it disappears, the write interaction remains a candidate, but the
different data pattern and changed inter-command timing are also variables.
Neither outcome alone proves the precise controller timing or a hardware fault.

Native tests assert exactly 486 preload column writes, check all pattern pixels
including hidden RAM, then forbid writes during two complete eight-transition
sequences. They check each offset, minimum step spacing, immutable RAM, return
to offset zero, indefinite idle, C repeat, stop and micros rollover.

Installed and armed on 2026-10-31 with image SHA-256
`1449ff6eaaffd015c23fc10bd64dc08e9172fec756f5a63cb5928dce10486a00`.
Native tests, ARM compilation, binary/HEX comparison and the stock flash-header
check passed. After upload/reset, USB returned `CNTRL READY 20 21411 fa5ba7e8`
and `CNTRL EDGE ARMED`. No motion was triggered from the host. Visual results
remain pending the user's recording. A power cycle clears diagnostic mode;
the same USB command rearms it.

## Previous production-sequence diagnostic

The preceding `CNTRL DISPLAY EDGE` fixture used the actual `DisplayRenderer`,
including its normal one-step-per-poll writes, 60 Hz progress batches, 500 ms
duration, 72-column gap and three-panel scroll commands. It replaces the
single-jump/delayed-write experiment documented below. Normal scrolling code
is unchanged apart from correcting a comment that overstated scanout safety.

Arm over USB, wait for the three patterned panes A/B/C to finish painting,
then record at 240 fps and press C. There is no `PRESS C` label in this version.
It performs four left/right pairs, holding each completed image for 1.5 seconds.
Allow approximately 20 seconds, until A/B/C remain still. C repeats; F or
`CNTRL DISPLAY EDGE END` exits and redraws the saved setup. Buttons are consumed
by the fixture. It does not send patches or write saved configuration.

Keep all three displays and their edges visible. Preserve the whole recording's
slow-motion section. The backlight stays at the configured brightness throughout
the test; there are no optical brightness markers, fades or blanking.

Each pane has a distinct arrangement of 16-pixel horizontal light/dark bands
and repeated column digits. A leftward transition moves A/B/C to B/C/D;
rightward reverses that change. Before the first transition only, both hidden
RAM columns on each panel are seeded with contrasting four-pixel stripes.
Every subsequent transition runs without resetting the ring offset or reseeding.

Interpretation:

- Four-pixel stripes appearing on the first transition identify exposure of
  the initial hidden RAM without replacement by the expected staged column.
- A fragment with the incoming pane's band pattern at the outgoing edge
  identifies incoming data visible on the wrong side. An early black cut into
  an outgoing band can similarly identify a gap write, but must be compared
  against the expected moving boundary.
- Pane identity, column digits and frame-to-frame motion must be considered
  together. Camera scan/exposure and LCD response can mix adjacent states;
  a single ambiguous edge fragment is not enough to assign a cause.

Native tests use the real renderer and a literal four-pane reference panorama,
not its coordinate helper. They check every incoming pixel, all three writes
before each offset command, all visible pixels after each command, poisoned
hidden columns, consecutive transitions through ring wrap, and the 30 batches
(eight of seven steps, 22 of eight steps). Fixture tests cover eight transitions,
idle arming, restart, stop and millis rollover. The host model still applies
offset commands immediately, so passing it does not establish scanout safety.

Installed and armed with image SHA-256
`961409bf3e9536c22f4ddd6dc5a280a7a4da207bfde385a814c3a853d8ac6e36`.
The ARM build passed binary/HEX comparison and the stock flash-header check.
After upload/reset, USB returned `CNTRL READY 20 21411 fa5ba7e8` and
`CNTRL EDGE ARMED`. The sequence is waiting for C; no physical recording
of this fixture has yet been inspected.

## Historical single-jump timing diagnostic

The following describes the earlier firmware and its recordings, not the
current `CNTRL DISPLAY EDGE` sequence.

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
