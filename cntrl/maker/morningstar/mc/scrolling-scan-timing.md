# MC6 Pro scrolling investigation

Investigation follow-up, updated 2026-11-01. The current findings below supersede
earlier proposed experiments and historical deployment statements in this file.
Read alongside [the experiment record](../../../../plan/scrolling-bug.md).
The leftward sliver remains unresolved. This is the single analysis and
experiment reference. The [plan](../../../../plan/scrolling-bug.md) remains
separate. Current instructions appear below; archived protocols describe
superseded firmware, not alternative current modes.

## Document map

- [Current evidence and limits](#current-evidence-and-limits)
- [Current diagnostic and controls](#current-diagnostic-panel-porch-comparison)
- [Baseline mapping and timing assumptions](#what-the-hardware-scroll-baseline-assumes)
- [Host progressive-scan model](#progressive-scanout-replay)
- [Video comparisons, speed sweeps and conclusions](#later-optical-comparisons-and-speed-sweeps-2026-11-01)
- [Historical diagnostic protocols](#historical-diagnostic-protocols)
- [Standalone diagnostic archive](#standalone-diagnostic-archive)
- [Original patch-switcher transition history](#original-patch-switcher-transition-history)

## Current evidence and limits

The current diagnostic is the panel-porch comparison described below. Its optical
result is pending. The preceding two-column, nominal 500 ms trial from `77bdc62`
produced a clearly visible fringe in all leftward passes and none visible in
rightward passes according to the user. Two-column batching alone is therefore
not a sufficient remedy. Normal patch scrolling is unchanged.

The evidence increasingly points to changed pixels reaching the outgoing edge
while scrolling, but neither exact SSA application timing nor the RAM fetch
order is established. Preloaded scrolling and same-pixel rewrites did not
clearly reproduce the characteristic failure; contrasting new pixels did.
Those comparisons also changed patterns and/or timing, so they are supporting
evidence rather than complete isolation of the cause. A dynamic addressing or
driver-sequencing error is not exhaustively excluded. There is no established
hardware fault or evidence that disassembly/modification is required.

Read the later sections as dated experiments. The original immediate-offset
model, delayed-offset snapshot model and progressive scan model have different
assumptions. Potential visible-region overwrites are not automatically optical
damage: an already-scanned column can be overwritten without changing that frame.
No model yet explains all observations, including changing directional results.

## Current diagnostic: panel porch comparison

This experiment changes only B1h panel timing while retaining the preceding
horizontal stripes, 232 steps, two-column batches, 4,311 us batch spacing and
nominal 500 ms motion. RTNA remains 5; SPI remains 8 MHz. Each pair moves left
then right. All three panels receive the same setting before baseline repaint,
followed by the existing 500 ms settle between movements.

| Pair | Front porch | Back porch | Purpose |
| --- | --- | --- | --- |
| 1 | 60 | 60 | Original timing control |
| 2 | 16 | 16 | Shorter equal porches |
| 3 | 1 | 31 | Short front porch, same total as pair 2 |
| 4 | 31 | 1 | Short back porch, same total as pair 2 |
| 5 | 16 | 16 | Repeat shorter equal porches |
| 6 | 60 | 60 | Repeat original timing control |

Press C once and record all twelve movements, allowing **35 seconds**. Inspect
the outgoing edge during motion, excluding baseline repaints. Count pairs in
the order above; no labels are drawn over the stripes. C repeats the sequence;
F exits and restores the saved UI. Original 60/60 timing is restored on exit
and after completion. Brightness stays constant.

The alternative hypothesis is an interval between SSA latching and the first
visible RAM fetch. The progressive replay currently makes these simultaneous;
a blank interval after latching could allow reuse to reach even the edge
scanned first. This is a hypothesis, not a documented latch phase. At the
nominal 850 kHz oscillator, the datasheet formula estimates approximately
60 Hz for 60/60 and 87 Hz for all three shortened settings (LINE=160..162).
Actual refresh and movement duration are unmeasured. Shortening porches changes
both blanking time and refresh rate; comparing 16/16, 1/31 and 31/1 holds the
nominal frame period constant while changing the distribution.

Native tests cover the full twelve-movement sequence and repeat, exact porch
settings, pixel payloads, two-column pacing, delayed polling, timer rollover,
completion restoration and exit restoration. Bus tests verify B1h payloads on
all panels and SPI/GPIO handoff. Native tests and ARM build/image inspection
passed. No Python source or Python-consumed data changed.

Diagnostic image SHA-256:
`561bc292a8de481dbe8ca8984639314968568f153cc429b395a3557ff019dbd1`.
Uploaded through the existing helper on 2026-11-01. The updater reported
reset/disappearance with status 1; subsequent USB responses were
`CNTRL READY 20 21411 fa5ba7e8` and `CNTRL EDGE ARMED`. The diagnostic is waiting
for C; no motion was triggered from the host. Optical results remain pending.
This experiment does not include the separately suggested static hidden-column
test.

## Additional documented evidence

The [ST7735S v1.1 datasheet](https://www.orientdisplay.com/wp-content/uploads/2020/11/ST7735S.pdf),
section 10.1.30, printed page 137 (PDF page 145), says:

> When new Pointer position and Picture Data are sent, the result on the display
> will happen at the next Panel Scan to avoid tearing effect.

Thus immediate scanout application of command 37h is not a safe assumption.
This supports deferred pointer application, but does not specify a complete
cycle-level RAM-fetch model or prove that these particular panels implement it.
The exact controller revision remains unconfirmed.

## What the hardware-scroll baseline assumes

`firmware/patch_switcher/renderer.h`, in `animate()`, increments `ring_`, stages
three incoming columns, and sends the new pointer. The original comment claiming
this never overwrote an outgoing edge has been qualified: hidden-column safety
is relative to the preceding **requested** pointer, not necessarily the pointer
currently used by scanout.

The original `firmware/test_renderer.cpp` model immediately applied each pointer
command to its simulated screens. Its hidden-column assertion could not detect
several requests arriving before application. Later delayed/progressive replays
are documented below; their timing remains hypothetical.

At an initially settled logical offset zero, forward writes logical RAM columns
160, 161, 0, 1, 2, ... . If scanout still uses zero, the third write overwrites
the visible leftmost column. Reverse writes 161, 160, 159, 158, 157, ... and
has the corresponding hazard at the right edge. These are logical column
indices before the driver's RASET conversion.

The default progression requests 232 steps in 30 batches: eight batches of
seven steps and 22 of eight. If a batch completes with the previous batch's
pointer still active, five or six writes per panel address its visible region.
Across all 30 batches that is 172 such writes per panel, or 516 across three.
These are arithmetic counts under an explicit hypothetical scan schedule,
not measured corrupt pixels or a reproduction of the physical artifact.

## Timing and limits of the explanation

Initialization in `firmware/usb_probe/displays.cpp` programs B1h with
`05 3c 3c`. Applying the datasheet's section 10.2.1 formula at its nominal
850 kHz oscillator gives approximately 16.59 ms for LINE=160 or 16.71 ms for
LINE=162. These are estimates, not measurements or guaranteed timing limits.

This makes the successful 20 ms experiment consistent with waiting out a scan,
while 2 ms spacing still allows reuse before pointer application. It does not
prove that interpretation. In particular, the simple deferred-pointer model
predicts address exposure in **both** directions. Scan order and the times at
which individual rows are fetched still need evidence to explain clean Undo.
The failed compensated ML experiments are a constraint on that explanation.

Restricting each acknowledged viewport to its two hidden columns at 60 Hz
would take approximately 1.93 seconds for 232 pixels. Therefore that conservative
strategy alone cannot preserve the requested 500 ms motion. This is not a
general lower bound on hardware scrolling: safely reusing already-scanned rows
could permit more work, if their timing is known.

## Initial proposed discriminating experiment (subsequently performed)

The useful missing observation is the time between a pointer command and its
visible application, together with when overwritten edge RAM is scanned.
A narrowly scoped optical test could paint distinctive static columns, issue
one pointer change with no subsequent RAM writes, and measure the visible
response against a command-time reference. A second trial would rewrite the
first reused column at controlled delays after that command, in both directions.
High-speed capture or an optical sensor with a timing reference would make this
more discriminating than another ordinary animation video. Repeat across scan
phases; a single clean delay is not a bound.

A verified accessible TE signal would supply a better direct timing reference,
but no such connection is established. Register receipt is not that reference.
Neither experiment was implemented at the time of this initial analysis.
The later single-jump diagnostic rejected a fixed 5 ms delay; preloaded,
unchanged-pixel and changed-pixel motion tests followed. See the later evidence
table rather than treating this original proposal as the next action.

Validation here was source inspection and independent arithmetic. No application,
device command, firmware upload, Python test suite, or hardware measurement ran.

## Slow-motion video and fixed-origin candidate

The supplied `signal-2026-10-31-17-02-13-890.mp4` was inspected from
`/Users/tom/Desktop/`. It contains 2,792 frames at 1280x720, approximately
30 playback fps and 93.06 seconds. Its middle sections are visibly slowed.
The user selected 240 fps capture, but the exported file alone does not prove
that every source frame survived or establish the capture-time scale.

After rotating the image upright, consecutive frames around playback 55-56 s
show a left-edge strip on the initially blank centre display during C. Its
contents change while the main window enters from the right. Around 77.7-78.2 s,
Undo also has a narrow, broken strip at the right edge of the rightmost display,
separated from the main window by a dark gap. Thus the new video does not
support treating Undo as completely artifact-free. This strengthens the
two-direction RAM-reuse hypothesis, without proving a common physical cause.

The candidate replaces hardware offset changes in both directions with writes
at fixed display addresses. It preserves the existing renderer lifecycle,
panorama geometry, bitmap buffers, default 500 ms duration and final repaint.
It targets 30 completed frames per second, skips obsolete frame targets when
late, and never switches target partway through a frame. This removes dependence
on scanout acknowledging an offset before RAM is reused at the opposite edge.
Ordinary scanout tearing and LCD response time are still possible.

Compared with the earlier rejected software trial, the candidate splits changed
vertical spans, joins only gaps cheaper than another command, and uses blocking
16-bit pixel transfers at the existing 8 MHz clock. It does not use the bulk
buffer transfer previously associated with a stall, DMA, or another framebuffer.
The 30 Hz target is deliberate headroom for software rendering, not a measured
panel or firmware rate. Physical smoothness remains the acceptance criterion.

Native tests verify every pixel of all 15 coloured-pane frames in both
directions, patterned frames with stippled bands, delayed polls during a frame,
clock rollover, custom duration, cancellation, zero-duration transitions,
candidate repaint interruption and final destination styling. Bus tests verify
partial-column addresses, pixel byte order and splash mode restoration.
The coloured forward transition sends 263,580 bytes including commands, giving
a 263.58 ms wire-only lower bound at 8 MHz; each frame fits its 33.33 ms wire
budget. Host fonts are test doubles, so this is not a real-font benchmark.
The older 395.814 ms software-trial wire figure used a different frame schedule
and is not an isolated measurement of the transfer optimization.

Native sanitizer tests and the ARM build/image checks pass. No Python source or
Python-consumed data changed, so the Python checks were not run. The candidate
was subsequently uploaded through the existing helper. The first upload hit
a USB write error; after confirming HalfKay again, the retry completed and
reset the pedal. The saved setup returned `CNTRL READY 20 21411 fa5ba7e8`.

The user confirmed that the edge strips disappeared, but reported painfully
jerky motion. The candidate is rejected: the wire-only budget did not establish
acceptable physical smoothness. Commit `31a0458` is reverted, restoring the
previous hardware-scrolling implementation and its tests. The underlying
scrolling issue remains unresolved.

Candidate image: `firmware/build/patch-switcher.hex`, SHA-256
`19d9f3b80f5910c9b74904fc8fdbee4e446368cb65f5453db239948faeff4c32`.
The build reports 21,984 bytes of RAM1 free for local variables and 292,064
bytes of RAM2 free for allocation. Test and build logs from this session are
`/private/tmp/cntrl-scroll-fix-native.log` and
`/private/tmp/cntrl-scroll-fix-build.log`; these temporary files may not persist.

The restored hardware-scroll sources match `4ea3253` exactly. Native tests and
ARM build/image checks passed again. Restored image SHA-256:
`dca55d0cbfe3be31cd24b47ccf9c54c4baef3759376ad79f4f411f614ed76d20`.
It was uploaded through the helper, reset successfully, and returned
`CNTRL READY 20 21411 fa5ba7e8`. This confirms setup loading after restoration;
the original moving-edge artifact remains unresolved.

## Existing switch-input route checked for TE

A temporary command enabled vertical-blank TE output (35h, parameter 00h)
on one panel at a time, with a disabled baseline and TEOFF on all panels when
finished. It sampled only the existing eight-bit switch reader, without
configuring unidentified GPIOs or changing the scrolling algorithm. Each of
five trials had four 100 ms windows: disabled, left, centre, right. Each window
returned 1,832 samples. Every raw sample was 3Fh; neither unknown bit 6 nor 7
had an edge. No buttons were pressed during the measurements.

This supplies no TE timing reference through the tested reader inputs. It
does not prove that the TE command took effect, that TE is disconnected, or
that no other MCU connection exists. Register readback was not part of this
probe. It does not establish a scanout bound or fix the moving strip.

The working probe image SHA-256 was
`86f057907103f692a26d92984dc9c040e85ac7180dd091adca1d5af91fa53e18`.
An initial image rejected the diagnostic command because its router had not
been extended; it produced no timing measurements. The corrected image's ARM
build and flash configuration/HEX-ELF checks passed. The probe was then removed
from the sources rather than retained as a production command.

The baseline native tests and ARM image checks passed after removing the probe.
The normal hardware-scrolling image, SHA-256
`dca55d0cbfe3be31cd24b47ccf9c54c4baef3759376ad79f4f411f614ed76d20`,
was restored on the pedal and returned `CNTRL READY 20 21411 fa5ba7e8` at that
historical stage. Later diagnostic deployments supersede that restoration.

## TE readback and passive MCU input search, 2026-10-31

The preceding repeated optical recording,
`signal-2026-10-31-19-17-59-393.mp4`, showed a wrong-edge marker at the
5 ms delay in leftward repetitions 5 through 8. In particular, the frame at
45.733 seconds shows the L 5MS R5 label and the old left-edge marker during
onset, rather than during the later reset. A fixed 5 ms delay is therefore
not safe. The exported video contains speed changes, so playback-frame
spacing must not be treated as uniform capture timing.

A further temporary diagnostic closed the missing TE-enable readback check.
It reused the previously hardware-verified bidirectional SDA read routine,
enabled vertical-blank TE with 35h/00h, and read RDDSM (0Eh). All three panels
returned raw `80000000` while enabled and `00000000` after TEOFF (34h).
The [ST7735S datasheet](https://www.orientdisplay.com/wp-content/uploads/2020/11/ST7735S.pdf),
section 10.1.9, defines the leading bit as TE enable. This verifies the
controller setting, not a voltage waveform at its TE output.

The probe sampled `digitalRead(0..54)` without changing unknown pins' mux,
direction or pull configuration. Each run had seven 200 ms phases: all off,
left on/off, centre on/off, right on/off. Each phase performed 56,414 or
56,415 complete pin sweeps; the largest measured interval between sweep
starts was 5 microseconds. No pin transitions were observed. Interrupts
remained enabled; normal display rendering and switch polling were paused
during each synchronous capture. Reported intervals are firmware measurements,
not an independently measured acquisition bandwidth.

Four runs covered both read-back MADCTL A8h (initial landscape state) and
88h (native scrolling state). The final two added a positive control:
with all displays deselected, driving the established shared clock pin 13
high then low read back 1 then 0 in every phase. They also repeated the
existing switch-reader capture with TE enabled on the selected panel:
3,657 samples per 200 ms capture, final value 3Fh, and zero changes in all
eight bits. There were seven such captures per run, with panels 0/1/2
sampled three/two/two times respectively. This strengthens the earlier
negative switch-input result; it does not measure the TE output electrically.

Mux inventory, hexadecimal values:

- Peripheral mux 2: pins 0, 1, 6, 7, 8.
- Peripheral mux 0: pins 42 through 47 (SD interface).
- Mux 13h: pin 11 at the initial inventory, before bit-banged readback.
- GPIO mux 15h: pins 12, 13, 15, 20 through 25, 27 through 29, and 34.
- GPIO mux 5: all remaining logical pins. Their GPIO direction bits were input.

The non-GPIO mux readings cannot be treated as observations of those physical
pads. Even for GPIO inputs this is a software sampling result, not continuity
evidence. Pins outside the Teensy-style 55-entry map were not examined.
Nothing was remuxed to search for signals; in particular, peripheral and
unknown board connections were not repurposed.

Initial probe image SHA-256:
`ca540ad280886aa999b7e6236abaa193722353367900f9666ea8ffa0e65697da`.
Probe with positive control and switch capture:
`ba90296716925d7bb79b2ff1cd9fce1d9071138fa8de461302cec122a13f3cd9`.
Both ARM builds passed binary/HEX comparison and the stock flash-header check.
Local raw captures are `/private/tmp/cntrl-frame-probe-{1,2,3,4}.txt`;
temporary source is `/private/tmp/cntrl-frame-probe.h` and the associated diff
is `/private/tmp/cntrl-frame-probe.patch`. These local artifacts are not durable
repository fixtures. The temporary diagnostic was removed after testing.
The rebuilt previous firmware reproduced SHA-256
`7aeb13b8401ee11b9b32785ea4d181ddd3e860f4a878b47fbfddd6863eb4ef37`.

No usable frame reference has been found. The next discriminating evidence is
the display module/flex identification and continuity of its TE contact, if
exposed, to the board. The controller datasheet describes TE as a separate
output; register readback reports its configuration, not its live level.
Do not infer a frame boundary from successful TE enable, immediately readable
MADCTL changes, or a nominal refresh-period calculation. A connection to one
panel would also need qualification before being used to schedule the other
two independently initialized panels.

## Production batch audit and replacement fixture

The follow-up audit exercises `DisplayRenderer` directly. Four patterned
panes are rendered separately, then copied into a literal panorama with
72-column black gaps. That reference does not use `panorama_sample()`.
Every animation write is compared with the incoming reference column, and
every visible RAM pixel is checked after each commanded offset. Each offset
must follow exactly three writes in panel order 0, 1, 2. The initially hidden
columns are poisoned so exposing one without writing it fails the comparison.

Four complete 500 ms transitions pass: left from ring 0, left from 70,
right from 140, right from 70. Each has 232 one-column steps in 30 time
batches: eight batches of seven steps and 22 batches of eight steps. Tests
drive eight renderer polls per simulated millisecond; this checks scheduling
logic, not actual MCU throughput. Font rendering is the existing host test
double, so these are column/source checks rather than real glyph qualification.

Representative trace, logical RAM columns before the driver's native mapping:

| Direction / initial ring | Step | Written column on all panels | New commanded ring |
| --- | ---: | ---: | ---: |
| Left / 0 | 1 | 160 | 1 |
| Left / 0 | 2 | 161 | 2 |
| Left / 0 | 3 | 0 | 3 |
| Left / 0 | 7 | 4 | 7 |
| Left / 0 | 162 | 159 | 0 |
| Left / 0 | 163 | 160 | 1 |
| Left / 0 | 232 | 67 | 70 |
| Right / 140 | 1 | 139 | 139 |
| Right / 140 | 2 | 138 | 138 |
| Right / 140 | 3 | 137 | 137 |
| Right / 140 | 7 | 133 | 133 |
| Right / 140 | 162 | 140 | 140 |
| Right / 140 | 163 | 139 | 139 |
| Right / 140 | 232 | 70 | 70 |

The driver maps a logical RAM column to RASET `(column + 1) % 162`, and
the commanded ring to SSA `(162 - ring) % 162`. Existing bus tests check
these boundaries separately. A step stages 267 bytes per panel, then sends
three 3-byte scroll commands: 810 bytes total, or a wire-only minimum of
810 microseconds at the configured 8 MHz. Computation, GPIO, transaction
overhead and other work add time; no new physical timing measurement was made.

No wrong source column, unprepared commanded image, or wrap error was found
in these cases. This does not prove physical scanout safety: the test's panel
model applies each command immediately. Within a seven-step batch, five
rewritten columns belonged to the visible image at the batch's starting
offset; an eight-step batch reuses six. That is safe only if the earlier
scroll commands have displaced those columns before their writes become
visible. The old renderer comment claiming outgoing edges were never
overwritten has been qualified to refer to the last commanded offset.

The replacement [optical fixture](#historical-diagnostic-protocols) uses the actual
renderer and its normal ordering, unlike the preceding pointer-jump test.
It provides pane/band identity and initially hidden four-pixel stripes, with
constant illumination. It does not change the normal animation algorithm.
Native tests pass, including the fixture's eight transitions and timer wrap.
The next physical recording is still required to determine whether the
reported artifact reproduces with these identifiable patterns and which
data appears at the wrong edge. These results do not establish a hardware
fault or the need for a hardware modification.

## Production recording and delayed-offset replay

`signal-2026-10-31-19-46-49-065.mp4` reproduces incoming-pattern fragments
at the outgoing edge using the production renderer. At approximately
55.5-57 seconds, the centre panel's narrow left-edge fragments match the
incoming bright bands on the right, including heights where the outgoing
pane is dark. At approximately 73.3 seconds the reverse transition shows
matching incoming-pattern fragments at the right edge. These examples support
premature reuse of still-visible memory rather than exposure of the initially
seeded fine-stripe pattern. They do not rule out other artifacts elsewhere.
The file has 3,586 video frames over 183.833 seconds with nonuniform timing;
nominal 30 fps metadata must not be used to infer physical scan timing.

`test_renderer.cpp` now records each real renderer step's three column payloads,
RAM address and offset, and replays those exact data through separate commanded
and visible offsets. The existing immediate-offset pixel checks remain in place.
The replay covers both directions from the four audited starting rings.

Explicit model assumptions:

- Either commands apply immediately, or the latest requested offset applies
  at hypothetical 17 ms frame boundaries. Seventeen starting phases spaced
  1 ms apart are tested. This period and phase grid are not measurements.
- RAM writes take effect immediately at the end of a whole column transfer.
  A column takes the wire-only 267 microseconds; the three scroll commands
  are grouped as one 9-microsecond event. CPU and GPIO costs are omitted.
- All three panels share the model phase. Separate physical panel oscillators,
  per-row scanning, byte-by-byte RAM updates, LCD response and camera exposure
  are not simulated. The replay models memory visibility hazards, not optical
  image formation or an exact reconstruction of the video.
- Frame boundaries at an event timestamp occur before that event. Source
  bitmaps are the same independent four-pane panorama as the immediate audit.

Compared schedules:

| Replay schedule | Immediate offsets | Hypothetical delayed offsets |
| --- | --- | --- |
| Current 60 Hz progress batches | Clean | Incoming data overwrites still-visible outgoing columns |
| Same 232 steps evenly spaced over 500 ms | Clean | Still produces outgoing-edge overwrites |
| Scroll command before each step's three writes | Exposes unprepared columns | Both outgoing overwrites and unprepared images occur |
| One step every 20 ms, write then scroll | Clean | Clean, but approximately 4.6 seconds long |

The tests require current and evenly spaced schedules to produce nonblack
incoming-pattern corruption at every sampled delayed phase, with all corrupting
writes confined to the outgoing edge. They separately count wrong columns
when an offset becomes visible, catching the command-first tradeoff. Every
replay must settle to the correct complete final image. The immediate and
slow schedules are controls that must remain clean, not proposed remedies.

All four starting-ring/direction cases produced the same aggregate counts
across the 17 delayed phases. Current batches had 8,974 corrupting panel-column
writes, including 6,258 with nonblack incoming pixels, reaching at most eight
columns into the outgoing edge. Even pacing had 8,943 / 6,220 respectively,
reaching seven columns. Command-first had 8,464 / 5,901 plus 344 wrong
panel-columns at offset-latch events. These are repeated model event counts,
not distinct pixels, camera-frame counts or measured artifact widths.

This reproduces the observed kind of failure under a specific assumption and
filters out simple scheduling candidates under that assumption. It does not
validate the assumption, establish a hardware fault, or prove that all smooth
firmware schedules are impossible. A successful candidate must satisfy both
correct incoming preparation and safe outgoing reuse, and then pass a physical
test. No production scheduling change or firmware upload accompanies this
host-only experiment.

## Progressive scanout replay

The same replay now also samples one visible column at a time, comparing all
128 pixels against the independent panorama only when that column is scanned.
Writes to a column that has already been scanned do not alter that frame's
observed result. The old immediate and whole-image snapshot cases remain as
controls, with their original event counts preserved.

The progressive cases retain the hypothetical 17 ms frame period and the
17 starting phases. They test both fixed screen-space scan directions and
two active windows: 17 ms with no blank interval, and 10 ms followed by 7 ms
without scanning. The latest requested offset latches immediately before the
first visible column is sampled. These timings and the relationship between
latching and the first visible column are assumptions, not measurements.
The final image must be fully and correctly scanned after writes finish.

All four schedules still depend only on step number, elapsed time and modeled
bus occupancy. They do not receive the simulated frame phase or scan position.
The replay compares all 128 pixels per column with contiguous column arrays
and byte comparisons to keep the expanded phase sweep practical.

For current batches and evenly spaced 500 ms steps, both active windows give:

| Fixed screen-space scan order | Leftward scroll | Rightward scroll |
| --- | --- | --- |
| Left edge first | Clean at all 17 sampled phases | Corrupt at all 17 sampled phases |
| Right edge first | Corrupt at all 17 sampled phases | Clean at all 17 sampled phases |

Command-first also fails with the outgoing edge scanned last. The 20 ms/step
slow control remains clean in both scan directions. None of the tested fast
schedules is clean for both motion directions with one fixed scan order.

This materially qualifies the preceding snapshot-model conclusions: its
potential RAM overwrite counts are not observed scanout damage counts. In
the progressive model, thousands of those writes can occur after the affected
edge has already been read, producing no incorrect sampled pixels.

More importantly, this simplest progressive model does not reproduce the
production recording's strips in both directions on the same panel with a
single fixed scan order. That is a limitation of the model, not evidence
against the recording. The relationship between SSA application, visible
scan order and RAM reads remains unresolved. A clean case here is insufficient
justification for a firmware change, including another scan-direction change:
the earlier compensated MADCTL trial already failed on the device (see
`plan/scrolling-bug.md`, refresh-order retry with settling and readback).

Native tests assert both the already-scanned clean cases and corruption at
every sampled phase when the outgoing edge is scanned last, as well as clean
slow controls and complete final frames. This remains a host-only experiment;
the diagnostic and normal scrolling algorithm were unchanged by that model
commit. No new physical timing measurement or hardware-fault conclusion
follows from this model.

## Later optical comparisons and speed sweeps, 2026-11-01

All video filenames below refer to originals supplied from `/Users/tom/Desktop/`.
Playback timestamps are locating aids, not physical timing measurements. Exports
contain speed changes; nominal playback fps does not establish preservation of
all 240 fps source samples. Local crops/contact sheets under `/private/tmp/` are
inspection aids, not durable repository fixtures.

| Experiment | Evidence | Supported conclusion and limit |
| --- | --- | --- |
| Scroll preloaded RAM without pixel writes (`574454f`) | User described smooth, impressive motion with no apparent glitches | Scrolling a fixed pattern can look clean. Pattern and command spacing differed from production; this was not proof of an artifact-free scan. |
| Scroll-only versus unchanged-pixel rewrites (`9fdf915`) | `signal-2026-11-01-13-15-01-587.mp4`; inspected rewrite movements around 28-32 and 44-48 s, controls around 60-64 and 76-80 s | No clear return of the original wrong-pattern fringe. Initial camera motion and compressed timing weaken early trials; the final reverse trial is not captured. The block/ruler pattern limits negative conclusions. |
| Contrasting changed-pixel writes (`a60372e`) | `signal-2026-11-01-13-36-41-071.mp4`; about 24 s leftward and 45-46 s rightward | Fine incoming stripe fragments appear at the outgoing edge while old blocks remain, separately from reset painting. This reproduces changed-content fringes in both directions. |
| Broad duration sweep (`947e667`, corrected to horizontal stripes by `9877d06`) | User saw a large fringe at 0.5 s, medium at 1 s, nothing visible at 2 or 4 s; that video was not supplied | Supports speed sensitivity, not an exact width measurement or safe threshold. Vertical bars and staggered checkerboards were rejected as confusing; horizontal stripes were preferred. |
| Refined 1/1.25/1.5/1.75 s sweep (`799a9ef`) | `signal-2026-11-01-14-20-40-925.mp4`, 7,330 video frames over about 244.246 s | Inspected slowed sections show a narrow intermittent fringe through 1.5 s; the tail gives less detailed coverage of 1.75 s. The user reports a hint at 1.75 s. No exact native-column width or linear speed/width fit was established. |
| Two-column batches at nominal 0.5 s (`77bdc62`) | User reports every leftward pass visibly fringed and every rightward pass visually clean | Rejects the proposed batch-size-only fix. A recording and actual device duration for this trial remain unavailable. |

The user reports that fringe size wavers within a pass. Inspect repeated frames
and distinguish the incoming leading boundary from a separate outgoing-edge
fragment. A fully replaced striped image at the end and the baseline repaint
between trials are expected. Camera exposure, perspective and LCD response
limit physical-column width estimates from these recordings.

### Batch size versus average speed

For the normal 60 Hz progression, the exact software batch counts are:

| Target duration | Column batches per transition |
| --- | --- |
| 0.5 s | 8 batches of 7, 22 of 8 |
| 1 s | 8 of 3, 52 of 4 |
| 1.25 s | 68 of 3, 7 of 4 |
| 1.5 s | 38 of 2, 52 of 3 |
| 1.75 s | 83 of 2, 22 of 3 |
| 2 s | 8 of 1, 112 of 2 |

The apparent clean 2 s observation coincided with a maximum batch size of two,
matching the two hidden RAM columns. That motivated the preceding 500 ms experiment:
116 two-column batches with starts at least 4,311 us apart, without 60 Hz catch-up
bursts. A delayed second step also prevents a new batch for at least 1 ms from
that step's start. All three panel columns are written before each scroll command.
Native tests enforce the schedule and payloads; they do not acknowledge scanout.

The continued leftward fringe shows that fitting a batch into two hidden columns
is insufficient. A new batch can still reuse RAM before the actual display has
moved far enough. The two-column experiment changes burst size/cadence while
retaining a nominal 500 ms duration; it does not measure or synchronize panel
refresh. Longer transitions vary both average speed and batch sizes. Neither
comparison alone proves a fixed latency, six-step lag, exact four-column fringe,
or strictly proportional fringe width. Panel refresh rate itself was not varied.

### Source buffering, preloading and unresolved mechanism

Normal scrolling already renders into MCU memory: `previous_` holds outgoing
images, `cache_` prepared images, and `moving_pixel()` chooses source pixels.
The diagnostic markers are generated from fixed formulas. There is no display
RAM readback used as the animation source. More source buffering alone cannot
prevent the destination panel RAM from changing while that RAM is being scanned.

Under the current full-height mapping, 160 of 162 columns are visible. The two
hidden columns permit partial preloading, not storage of an arbitrary full next
image alongside the outgoing image. Preloaded scrolling rotates an existing
162-column ring; after 162 steps the RAM mapping repeats. A real transition to
different content must eventually replace pixels. The two arbitrary 160-column
images require 320 columns of data, before considering the virtual gap. This is
a limitation of full preloading in this mapping, not a proof that safe concurrent
updates or smooth hardware scrolling are impossible.

The leading hypothesis is a mismatch between RAM reuse and the mapping/scan
actually in use. Deferred SSA application, progressive RAM reads and dynamic
addressing/driver sequencing remain unresolved possibilities. Register receipt,
SPI completion and host-model success are not acknowledgements that a column
has left the visible scan. Scan-direction changes, SSA-first ordering and 2 ms
pacing have already failed in earlier device trials; do not repeat them as if
untested. The latest rightward-clean result does not erase earlier rightward
fringes in different sequences.

### Constraints and code entry points

Preserve continuous visible content and smooth motion. Software-rendered trials
were rejected as jerky. Backlight blanking and fade-out/change/fade-in were
rejected as visible interruptions, not offered as remedies. The user does not
want to open the unit; TE readback/input results do not require disassembly as
a next step. No specific smooth, safe update schedule has been verified.

- Current diagnostic: [scroll_timing.h](firmware/patch_switcher/scroll_timing.h).
- Normal animation and MCU source buffers: [renderer.h](firmware/patch_switcher/renderer.h), `animate()` and `moving_pixel()`.
- Normal progress and source coordinates: [panorama.h](firmware/patch_switcher/panorama.h).
- Native column writes and SSA command 37h: [displays.cpp](firmware/patch_switcher/displays.cpp).
- Actual-payload audits and hypothetical scan replays: [test_renderer.cpp](firmware/test_renderer.cpp).

In the diagnostic, stage 2 is idle awaiting C; `start()` leaves that state, so
`poll()` has no stage-2 body. C repeats, F exits and redraws the saved setup.
Arming is not persistent across a power cycle. Current source/image provenance
is recorded in the current diagnostic section; a successful `CNTRL READY` response checks
saved-setup loading, not firmware identity or visual quality. Further C++ edits
should use multiline control flow with braces and separate state changes, per
the user's readability request. This documentation update does not refactor code.

## Historical diagnostic protocols

### Previous two-column half-second test

The previous `CNTRL DISPLAY EDGE` diagnostic keeps the horizontal grey baseline
and offset white incoming stripes, but changes scheduling to 116 two-column
batches. Batch starts are spaced at least 4,311 microseconds apart, targeting
500,076 microseconds for 232 steps. Each step still writes one incoming column
on all three panels, then issues the three scroll commands. One step runs per
poll, so other pedal work can run between the two steps in a batch.

There is no 60 Hz progress backlog in this experiment. A delayed poll does not
trigger multiple overdue batches. After a delayed second step, a new batch
also waits at least 1 ms from that step's start. Actual transfer and polling
costs may stretch the nominal half-second transition. This is a bounded batch
size, not an acknowledgement that the panel has applied the preceding offsets.
It does not establish that two-column batching alone is safe.

**Observed result, 2026-11-01:** the user reports the fringe clearly visible in
all leftward passes and completely invisible in rightward passes. This rejects
two-column batching as a complete fix at the nominal half-second speed. No
video of that trial or actual on-device duration has been measured.
See [the consolidated evidence and limitations](#later-optical-comparisons-and-speed-sweeps-2026-11-01).

Press C once for four left/right pairs, all using the same half-second target.
Allow **25 seconds** including resets and recording margin. Every movement
starts with a fully restored baseline. Completed movements hold for one second;
the next baseline settles for 500 ms. C repeats; F restores the saved UI.
Inspect the outgoing left edge during leftward motion and the outgoing right
edge during rightward motion, excluding reset/repaint frames.

Native tests check two steps per batch, batch-start spacing, inter-batch quiet
time, the approximately 500 ms target at 100 us polling, exact addresses and
payloads, all eight transitions, resets, repeat and micros rollover. A delayed
second-step case verifies that an overdue batch is not started immediately.
Normal patch scrolling is unchanged; this remains an unsuccessful diagnostic candidate.

Installed and armed on 2026-11-01 with image SHA-256
`be176066b42e537555a38fea4dc384dd44cefc9ad25fb5b54fe761fe3ffa1bdc`.
Native tests and ARM image checks passed; USB returned the saved-setup status
`CNTRL READY 20 21411 fa5ba7e8` and `CNTRL EDGE ARMED`. The user observation
above follows that upload; actual on-device transition duration remains unmeasured.


These protocols preserve image provenance, controls and test conditions. They
are superseded by the current panel-porch diagnostic. Their result statements
are dated observations, not current device-state checks.

### Previous horizontal-stripe speed sweep

This previous `CNTRL DISPLAY EDGE` experiment used only horizontal stripes.
The baseline has eight-pixel-high grey stripes separated by eight black rows.
Incoming columns use the earlier eight-white/eight-black horizontal marker,
shifted vertically by eight rows relative to the baseline. This replaces the
vertical-bar pattern after the user found it difficult to read. The refined
speed sequence below retains the same scheduling. All three panels use the same pattern;
brightness remains constant.

Press C once. Two left/right pairs run at each target duration, fastest first:

| Pairs | Target duration per movement | Approximate rate |
| --- | --- | --- |
| 1-2 | 1 second | 232 columns/second |
| 3-4 | 1.25 seconds | 186 columns/second |
| 5-6 | 1.5 seconds | 155 columns/second |
| 7-8 | 1.75 seconds | 133 columns/second |

The user reported visible effects at 0.5 and 1 second and none at 2 and 4
seconds in the preceding sweep. That video has not yet been supplied for
analysis. This narrower sweep investigates the reported interval; the report
is not a measured safe timing bound.

Every movement has 232 one-column steps, with the production 60 Hz progress
targets and write-three-panels-then-scroll ordering. The previous artificial
1.5 ms reservation is removed. Transfer, computation and polling overhead can
make the fastest passes finish late; durations and rates above are targets,
not measured hardware timings. Changing duration also changes batch sizes,
so this probes the combined production timing behaviour, not only a uniform
interval between individual writes.

The full 162-column baseline reloads before each movement, followed by a
500 ms settle. Completed movements hold for one second. The first movement
uses the baseline already prepared while armed. Allow **60 seconds** for the
complete sequence and recording margin. After the final reset it waits for C;
C repeats everything, and F exits to the saved UI.

During leftward motion, incoming white bars belong on the right; inspect the
far left for premature fragments. Reverse those sides during rightward motion.
The incoming pattern contains black intervals too, so not every unsafe write
would make a white fringe. Exclude the visible reset/repaint between trials.
Compare the widest clearly identified fringe across repetitions at each speed.
A wider fringe at higher speed supports a timing explanation. Uncontrolled
scan phase, coarse step batches and optical response prevent assuming exact
proportionality or interpreting one clean pass as proof.

Native tests check every baseline/marker pixel, all incoming addresses and
offsets, each speed group's timing targets, resets, both repetitions of the
full sequence, stop and micros rollover. Normal patch scrolling is unchanged.

Installed and armed on 2026-11-01 with image SHA-256
`7891583d298702c636e04b05f9e5d5b349723a81f3248723e07e40ce13ed31b7`.
Native tests and ARM image checks passed. The later supplied recording
`signal-2026-11-01-14-20-40-925.mp4` shows intermittent fringes in inspected
sections through 1.5 s. The user reports a hint at 1.75 s; the tail's compressed
playback gives weaker evidence there. Exact widths and a safe threshold were
not measured. The target duration varies scroll speed/batch size, not panel
refresh rate.

### Previous contrasting changed-pixel comparison

This previous `CNTRL DISPLAY EDGE` experiment compared scroll-only movement
with writes of a distinct incoming pattern: eight-pixel white/black horizontal
stripes. The control uses the same preloaded block/ruler pattern as before.
Brightness stays constant, and both conditions retain the same 1.5 ms step
reservation and 500 ms / 60 Hz progress targets. Actual timing equality remains
unmeasured if transfers exceed their reserved interval.

Press C once. Four left/right pairs run in this order:

| Pair | Condition |
| --- | --- |
| 1 | Scroll only |
| 2 | Write contrasting striped columns while scrolling |
| 3 | Scroll only |
| 4 | Write contrasting striped columns while scrolling |

Each individual movement starts from offset zero and the fully restored
baseline, including all 162 RAM columns on all panels. The reset visibly
repaints the screen, then settles for 700 ms before the next movement.
The first movement uses the baseline already prepared while armed. Each
completed motion is held for 1.5 seconds before reset. After the last motion
the baseline is restored and the test waits for C again. Allow **30 seconds**
for watching/recording, including preparation overhead. F exits to the saved UI.

During a leftward rewrite trial, striped content should enter from the right;
look for a narrow striped fragment at the far left while original blocks
remain there. During a rightward rewrite trial, stripes should enter from the
left; inspect the far right. Once the old image has scrolled out, a fully
striped display is expected. Reset/repaint frames are not evidence of the bug.

This replaces the preceding unchanged-pixel comparison. It deliberately
changes RAM contents at the production incoming-column addresses. It retains
the diagnostic's pattern, target timing and pair order, with per-movement
resets necessary to keep later trials from inheriting striped RAM. Normal
patch scrolling is unchanged.

Native tests check the full baseline and resets, contrasting payloads and
incoming addresses for every rewrite step, zero motion writes in controls,
offset sequences, minimum spacing, C repeat, stopping and micros rollover.
Expected RAM changes are tracked independently and checked at each command.

Installed and armed on 2026-11-01 with image SHA-256
`82c08c65919f0d929066682f8f53845b1e19004bd75b4033af0bbb23df6f758a`.
Native tests and ARM image checks passed. After upload/reset, USB returned
`CNTRL READY 20 21411 fa5ba7e8` and `CNTRL EDGE ARMED`. The later recording
`signal-2026-11-01-13-36-41-071.mp4` shows incoming stripe fragments at the
outgoing left edge around 24 s and the outgoing right edge around 45-46 s,
before reset painting. This confirmed the changed-content fringe in both
directions under this protocol; it did not measure controller latency.

### Previous unchanged-pixel rewrite comparison

The preceding `CNTRL DISPLAY EDGE` sequence alternated two conditions using the
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
`CNTRL READY 20 21411 fa5ba7e8` and `CNTRL EDGE ARMED` at installation.
The later `signal-2026-11-01-13-15-01-587.mp4` did not clearly reproduce the
original fringe in inspected unchanged-pixel rewrite trials. The pattern made
negative judgments difficult; exact matching of device timings was not verified.

### Previous scroll-only diagnostic

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
and `CNTRL EDGE ARMED`. No motion was triggered from the host. The user later
described the motion as impressive and visually free of glitches. This is an
unaided observation, not a guaranteed clean scanout measurement. This protocol
was superseded by the write comparisons above. A power cycle clears diagnostic
mode; the same USB command rearms the currently installed version.

### Previous production-sequence diagnostic

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
`CNTRL EDGE ARMED` at installation. The subsequent
`signal-2026-10-31-19-46-49-065.mp4` shows incoming-pattern fragments at the
outgoing left edge around 55.5-57 s and right edge around 73.3 s. This
reproduced the artifact through the production renderer; it was not a safe
timing bound. That fixture has been superseded.

### Historical single-jump timing diagnostic

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

### First recording and repetition follow-up

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
It was left waiting for C at that installation; no sequence was started from
the host. The later repeated recording
`signal-2026-10-31-19-17-59-393.mp4` showed wrong-edge markers at 5 ms in
leftward repetitions 5-8, rejecting 5 ms as a safe fixed delay. This protocol
has been superseded by the later comparisons above.

## Standalone diagnostic archive

The following records the separate `firmware/scroll_probe` experiments from
2026-10-24 and the subsequent edge-staging change. That standalone image is
not the currently installed patch-switcher diagnostic. Runtime control changes
below are a historical sequence; later entries supersede earlier controls.
Build instructions remain in [its README](firmware/scroll_probe/README.md).

The diagnostic uses the stock panel initialization, followed by native-coordinate
`MADCTL = 0x88` (MY/BGR retained, MV cleared), `SCRLAR = 1,160,1` and scroll start
1. It assumes a 162-line controller with the visible gate lines 1..160. This
mapping is an experimental hypothesis, not identification of the installed chip.
Unexpected orientation or edge lines must be distinguished from absent scrolling.

### Speed comparison

Initial painting happens while dark. Tiles are 32 pixels wide, numbered 00..39,
with a shared grid/diagonal pattern in a repeating 1280x128 image. It reuses the
installed ST7735 library's classic font data. Each panel initially shows 160
consecutive columns. With the default 72-pixel gap the windows are left 0..159,
centre 232..391 and right 464..623. Columns 160..231 and 392..463 are hidden
behind the two physical gaps, rather than squeezed out of the image.

All three panels show windows at world positions `x`, `x+232` and `x+464`,
modulo 1280. The artwork keeps the same pixel scale; it continues behind the
estimated gaps. Updates initially target 16,667 microseconds, approximately 60 Hz.
Each update fills the entering circular-buffer columns per panel, then changes
all three scroll offsets once. Pixel traffic is 768 bytes times the stride per
update, plus command traffic. Tiles should continue across panel boundaries.

Initially the speed is one pixel per update (60 pixels/second). Press and release
C to cycle through 1, 2, 4, 8, 16 and 32 pixels per update, then back to 1, without
blanking, repainting or resetting position. These correspond to 60, 120, 240,
480, 960 and 1,920 pixels/second if the update fits within its time budget.
The current isolation baseline cannot maintain 60 Hz at the highest strides.
Press and release B to toggle the panel update order. The initial order is
left, centre, right (`LTR`); the alternate is right, centre, left (`RTL`). Both
the entering-pixel writes and the subsequent scroll commands use that order.
The image's position, direction, speed and physical window assignments do not
change, and toggling B does not blank or repaint the screens.

Press and release A to cycle the target update rate through 60, 50, 40 and 30 Hz,
then back to 60. The gaps stay fixed at the configured width, default 72 pixels.
A does not blank, repaint or reset position. Keep C at one pixel per update
initially to compare cadence; movement speed in pixels/second will also decrease
with update rate. D, E and F do nothing. Panel refresh registers are unchanged.
All SPI calls finish and restore GPIO 13/12 before the next button scan.

The complete source pattern is cached once at startup in 327,680 bytes of RAM.
The isolation baseline restores 8 MHz byte transfers and addresses each column
separately, even when the caller supplies several columns. This removes the
faster clock, 16-bit transfers and controller multi-column addressing from the
rendering path while keeping the cache and six speeds. Each source column wraps
independently, so gaps need not be multiples of a tile or controller width.
Initialization uses the shared driver; the normal application's driver is unchanged.

`CNTRL STATUS` returns fields such as:

```text
CNTRL SCROLL READY order=LTR spi=8 gap=64 rate=60 stride=1 x=110 ssa=51 steps=110 pixels=84480 us=... period=... max_period=... max_us=... overruns=...
```

`pixels` counts streamed pixel bytes after the baseline, not baseline painting or
command bytes. `us` measures the most recent step's SPI writes and scroll
commands. `period` is the interval between the latest two update starts, in
microseconds. `max_period` is the longest start-to-start interval. `max_us` is
the worst step duration and `overruns` counts steps exceeding the selected rate's
time budget. Measurements reset on A, B or C; the first start-to-start interval
after a setting change is excluded from the maximum because it spans settings.
These are elapsed times, not sampled CPU load. `rate` is the target, not a
measurement. `gap` reports the configured fixed gap.
`CNTRL SCROLL NEXT` toggles speed from the host. The usual `cntrl status --port
/dev/cu.usbmodem144682801` command can read status without new host tooling.

### Historical standalone build validation and staging

Paths in this historical staging record are relative to `firmware/scroll_probe`.

Native tests cover all six strides, including unaligned starts and source-image
wraps, the update-rate cycle and interval calculations, pixel byte order,
per-column register payloads, both panel command orders, selected-panel isolation,
coordinate bounds and SPI/GPIO handoff. These establish software behaviour, not
whether the physical controller implements scrolling. Inspection compares HEX
with ELF bytes and checks the stock flash-configuration digest. No build target
uploads anything.

The image is `../build/scroll-probe.hex`. To use the existing fixed-image upload
helper, it is copied to `../build/patch-switcher.hex` only after inspection. This
does not change the normal application sources or the helper's target restriction.
The normal build was preserved as `../build/application-before-scroll.hex` before
staging the diagnostic. That saved build has SHA-256
`60ebbcc450560197396faecdc9e6c96827fb078737ec73d82585ec9fb27353cd`.
Restore it through the same software bootloader/helper workflow, or build and
inspect the current patch-switcher target before restoration. Neither restoration
path needs to modify saved card files.

### Original upload record, 2026-10-24

Diagnostic HEX SHA-256:
`977daac74c91f826d1da56238a2029b75d358b4aeb59621dca18494d6fa54851`.
Both native test targets and stock flash configuration/HEX-ELF checks passed.
Software bootloader entry and helper upload completed; the uploader returned
status 1 after its usual reset/disappearance, then USB confirmed the ONE status
above. Before upload the application reported `CNTRL READY 7 0 00000000`:
no compatible show was active. The diagnostic made no changes to that state.
The user subsequently reported that both demonstrations looked good. This is
visual confirmation, not an instrumented measurement of panel synchronization.

### 60 Hz speed-comparison upload, 2026-10-24

HEX SHA-256:
`1ffaca937276d46776e006bfec7415a5565bca4e84582147ff6c3335fe68f8a3`.
Both diagnostic tests, the existing button/display/renderer tests, and stock
flash configuration/HEX-ELF checks passed. After software bootloader entry and
upload, USB confirmed the `rate=60 stride=1` report above. The uploader returned
its usual status 1 on reset/disappearance; the serial response confirms the new
firmware booted. Visual comparison of the two speeds remains a physical test.

### Optimized six-speed upload, 2026-10-24

HEX SHA-256:
`019c1c671fb4a4e2a12bd6f0c3a6cb5a63059ee2e1bf0c7e4f4ee0caa1d18735`.
All native tests and image checks passed. The first attempted optimization used
the SPI library's bulk-buffer transfer; the device enumerated but stopped
responding to status. Software bootloader entry still worked. Replacing that
call with blocking 16-bit transfers restored normal USB responses. The precise
cause of the bulk-transfer stall was not established; that path is not retained.

After upload, each stride ran for two seconds under USB observation:

| Pixels/update | Measured updates/second | Worst update, microseconds |
| --- | --- | --- |
| 1 | 59.86 | 438 |
| 2 | 60.32 | 858 |
| 4 | 59.89 | 1,745 |
| 8 | 60.31 | 3,452 |
| 16 | 59.83 | 6,727 |
| 32 | 59.93 | 13,739 |

All six reported a latest start-to-start period of 16,667 microseconds and zero
overruns. The host rate estimates use short sample windows and integer frame
counts, hence the small spread around 60 Hz. The test ended back at stride 1.
Pixel transfer throughput is confirmed; visual quality at the increased clock
and higher strides still needs the user's observation. Saved card data was not
touched. At the highest stride, button scanning can be delayed by a whole update
(about 14 ms); the diagnostic does not claim a 3 ms worst-case scan interval.

The user subsequently reported intermittent glitches on the left panel and
stable rendering garbage on the other two. The timing results did not establish
correct rendering. The exact cause has not been isolated.

### Rendering isolation baseline

Restore the previously working 8 MHz byte-wise, single-column write protocol,
keeping the cached image and six-speed controls. At high strides, frame timing
will exceed the 60 Hz budget and button scanning can be delayed by that longer
update. This test prioritizes rendering correctness over speed. A clean result
would narrow the fault to the removed transfer/addressing changes, not identify
which one caused it. Saved card files remain untouched.

Built and uploaded with HEX SHA-256
`3c79bbc0f3a017aa98782f98c210bb0e9108af4da41bc2b1e2ce4387a284b7f2`.
Diagnostic and button/display/renderer native tests passed, as did the stock
flash-configuration and HEX/ELF checks. The uploader returned its usual status 1
on reset/disappearance; USB then confirmed this baseline running at stride 1:

```text
CNTRL SCROLL READY spi=8 rate=60 stride=1 x=333 ssa=148 steps=333 pixels=255744 us=965 period=16667 max_us=965 overruns=0
```

The user subsequently confirmed that rendering was clean again. This restores
a visually confirmed baseline, without identifying which removed optimization
caused the earlier corruption.

### Gap-aware panorama, 2026-10-24

The image now contains 40 numbered tiles over 1,280 columns, with a configurable
equal gap between the three windows. The uploaded build uses `SCROLL_GAP=64`.
The known-working 8 MHz single-column transfer path is unchanged. The cache
grows to 327,680 bytes, leaving 156,704 bytes free in RAM1 according to the build.

Native tests passed for gaps of 64 and 17 pixels, including all six strides and
source-image wraparound. Button/display/renderer tests and stock flash-config
and HEX/ELF checks passed. Uploaded HEX SHA-256:
`f60225f7b146f561994525c3c85d8812313415cbf0a67129c62dea9026c538dd`.
After the uploader's usual reset/disappearance status 1, USB confirmed:

```text
CNTRL SCROLL READY spi=8 gap=64 rate=60 stride=1 x=500 ssa=141 steps=500 pixels=384000 us=965 period=16667 max_us=965 overruns=0
```

The physical continuity illusion at the estimated gap width awaits visual
confirmation. The diagnostic still does not open the SD card or send MIDI.

### Panel update-order comparison, 2026-10-24

B toggles both pixel-write and scroll-command order; C retains its six speeds.
This tests whether the observed left/centre motion hitch follows transaction
order. It does not synchronize the three independent panel refresh cycles or
establish the cause of the hitch by itself.

Native tests verify both panel-index sequences and actual chip-select order for
forward/reverse scroll commands, including a selected-panel subset. Diagnostic,
button/display/renderer tests and stock flash-config/HEX-ELF checks passed.
Uploaded HEX SHA-256:
`5bea12f8816fd8576788914d78735fd9c9b0fe00e49e48539b78f9428ab8df91`.
The uploader returned its usual status 1 on reset/disappearance; USB then
confirmed the new build running in the original left-to-right order:

```text
CNTRL SCROLL READY order=LTR spi=8 gap=64 rate=60 stride=1 x=449 ssa=32 steps=449 pixels=344832 us=964 period=16667 max_us=965 overruns=0
```

The effect of B on visible motion awaits physical observation. Saved card data
remains untouched.

The user subsequently reported no visible difference from B: the rightmost
panel remained smooth and the left/centre panels retained small motion jumps.
This does not establish a cause. The gap change is the next comparison target.

### Runtime gap comparison, 2026-10-24

A now toggles between the configured gap and zero. B and C retain their existing
functions. Both cases use the same cached image and update target. Repainting
preserves the current world offset and writes each visible source column into
the corresponding circular-buffer position, with the backlight off until done.

Native tests cover repeated gap changes/repaints at nonzero ring positions and
continued scrolling at all six strides, including source-image wraparound.
Diagnostic, button/display/renderer tests and stock flash-config/HEX-ELF checks
passed. Uploaded HEX SHA-256:
`38ae0a74fd860c107319fa7d712a2363b7bb33557185da8e30bc7c42dcf30061`.
Following the uploader's usual reset/disappearance status 1, USB confirmed:

```text
CNTRL SCROLL READY order=LTR spi=8 gap=64 rate=60 stride=1 x=497 ssa=144 steps=497 pixels=381696 us=965 period=16667 max_us=965 overruns=0
```

The effect of gap toggling on the observed jumps awaits physical comparison.
Saved card data remains untouched.

The user subsequently reported identical behaviour with and without gaps: the
rightmost display was steady and the other two retained subtle motion jumps.

### Update-rate comparison, 2026-10-24

The gap is fixed again at the configured width. A replaces its gap toggle with
a 60/50/40/30 Hz cycle; B and C are unchanged. The new `max_period` records the
longest interval between update starts, rather than only timing the drawing
work. Setting changes reset measurements and exclude the transitional interval
from that maximum. No panel refresh-register changes are made.

Native tests cover the rate cycle and integer interval calculations alongside
the existing window, pixel and protocol tests. Diagnostic and button/display/
renderer tests and stock flash-config/HEX-ELF checks passed. Uploaded HEX SHA-256:
`09ff20326659f304201912d1eea153ed545c7bd4491bffe199f492b3d889ee1e`.
After the uploader's usual reset/disappearance status 1, USB confirmed:

```text
CNTRL SCROLL READY order=LTR spi=8 gap=64 rate=60 stride=1 x=629 ssa=12 steps=629 pixels=483072 us=965 period=16667 max_period=16720 max_us=965 overruns=0
```

In this initial observation the longest period exceeded the 16,667-microsecond
target by 53 microseconds, not a full update interval. This is not a correlation
with a visibly observed jump. The effect of changing rates awaits physical
comparison. Saved card data remains untouched.

### Patch-switcher edge staging, 2026-10-31

The production patch switcher previously replaced outgoing, still-visible RAM
columns with incoming pixels, then advanced the scroll register after a batch.
This exposed a wrong-image strip on the left during sends and on the right
during undo. Black empty windows made the contrast more apparent.

The patch switcher now scrolls across all 162 controller rows, retaining the
160-column visible window and staging each incoming column in hidden RAM. It
writes all three panels before advancing the offset by one column. Native mode
uses scroll area `0,162,0`, with zero as its neutral start; writes retain the
one-row panel origin and wrap through addresses 161 and 0. Splash output restores
the previous `1,160,1` landscape area and start 1. The standalone diagnostic
probe above retains its original geometry.

The mapping follows the MY row-address reversal and scroll-area/start commands
in the [ST7735S datasheet](https://www.orientdisplay.com/wp-content/uploads/2020/11/ST7735S.pdf),
sections 9.11.3, 10.1.26 and 10.1.30. No new MCU image buffers are required.

Native tests check the image after every scroll step in both directions and
reject writes to currently visible columns before scrolling. Bus tests check
the hidden-row addresses, wraparound, neutral start and splash restoration.
That was the pending qualification at this historical stage. Later experiments
above show that the edge artifact remains unresolved; native hidden-column
checks did not establish physical scanout safety.

## Original patch-switcher transition history

This archive preserves the original application transition deployments and
default-duration changes. Current behaviour and unresolved qualification are
stated at the top of this document.

### Sliding send/undo panes, 2026-10-24

C sends MIDI and pushes history before requesting a leftward transition. F
performs the existing per-instrument undo, selects the undone patch as R's
browsing candidate, and requests the reverse transition. Failed sends and an
empty undo stack do not request animation. There is no MIDI delay waiting for
graphics, and no persistent history or graphics-format change.

The screen pitch is 232 pixels: 160 visible plus a 72-pixel physical-gap
estimate. Over a show-configured duration at 60 Hz (default 1 second after preparation), the
old panes move left on C and right on F. The new R candidate is rendered using
the existing grey-name rule before entering from the right. On F, the next
undo target, or a blank pane, enters from the left. Virtual gaps are black.

The RAM-only library still renders fonts. Preparing destination bitmaps is
incremental and leaves the old screen contents visible. Native ST7735 column
writes use the tested 8 MHz byte-wise protocol and `MADCTL=0x88`; each animation
poll advances at most one pixel and writes one column per panel. Button scans,
USB and queued MIDI output run between these polls. Scroll commands are issued
at frame targets. Destination screen-specific styling is reconciled afterward.
The target duration is not a hard real-time guarantee under SD/USB stalls.

Ordinary UI repainting also uses native columns and retains the current ring
offset. Splash output switches back to landscape row addressing and resets the
scroll offset. On return, renderer invalidation restores native mode and
repaints behind the existing first-image/blackout gate. New actions during an
animation retain their MIDI/history effects and coalesce into the latest pending
view; they do not build an unbounded animation queue.

Verification: 194 Python tests, the sanitizer-enabled portable controller suite,
and native button/display/renderer tests passed. New tests cover forward/reverse
pane mapping, nonzero ring offsets, the native-column protocol and return to
splash addressing. Controller tests cover browsing away before undo and exclude
failed sends/empty undo from transition events. HEX/ELF comparison and the stock
flash-configuration digest passed. RAM1 has 111,680 bytes free, RAM2 309,312,
with no external RAM linked.

Uploaded main-image SHA-256:
`6baf5b03d9ff0de30d854c5fce4547f1e4c585084a8a388dd5dc2cce4cc05872`.
After the uploader's usual reset/disappearance status 1, USB confirmed format 7.
The setup catalogue was empty. The existing pman example was recompiled,
copied with readback verification and selected; status then reported
`CNTRL READY 7 23390 0718301d`.

At that deployment, `instruments/pman.toml` mixed an initial
`[[patches.pman.0]]` header with `[[patches]]` entries and lacked explicit setup
fields. It was not edited for that task. Upload used a temporary copy with the
flat initial header and `setup = "default"` on each entry, matching the example's
existing default-setup routing. No factory EEPROM or vendor backups were changed.
The current repository table now uses the flat header and explicit default setup.
Physical transition/splash/text verification remains pending user observation.

The user subsequently confirmed that the original transition build looked
correct. A timing fixup changes the default from 400 ms to 2 seconds. Set
`transition_ms = 2000` at the top level of a show to specify it explicitly, or
choose another integer duration in 1..65535 milliseconds. The compiler stores
the setting in the metadata header's previously reserved u16; zero denotes the
firmware default and preserves existing compiled shows and their checksums.
Resolved TOML exports retain this setting. Both send and undo receive the same
duration, without changing MIDI timing. The two-second fixup was uploaded and
boot-verified. A subsequent fixup sets the default to 1000 ms (one second);
explicit `transition_ms` values still take precedence.
The current implementation keeps an omitted duration unspecified through the
model, compiled metadata (zero), and firmware (`std::nullopt`). The renderer
alone resolves it to `DEFAULT_TRANSITION_MS`, currently 500 ms for both send
and undo, when starting a transition.
Resolved TOML omits unspecified durations; explicit values, including 1000,
remain explicit throughout.
The one-second image was flashed and boot-verified with pman and its checksum
unchanged. SHA-256: `b2c97ed563419a8a308193f7c104ad0150ea6af69ecfd1e3fadf423c42f03b2e`.

```bash
make -C firmware test
uv run --frozen pytest
make -C cntrl/maker/morningstar/mc/firmware test inspect-patch-switcher
```

The build creates `build/patch-switcher.hex` in this directory, plus the ELF and
raw image under `build/patch-switcher/`. The inspect target compares HEX and ELF
bytes and checks the complete stock flash configuration. Neither target invokes
an updater. The diagnostic `usb_probe` build remains available for hardware
investigation; it shares button scanning and display transactions with the new
adapter, not the patch-switcher application logic.

Use the generic [host instructions](../../../../firmware/README.md) with
`--target cntrl/maker/morningstar/mc/patch-switcher-target.json`. The Python CLI
and compiled show format are independent of the controller. Install firmware
separately using the established recovery/upload workflow only when authorized.
